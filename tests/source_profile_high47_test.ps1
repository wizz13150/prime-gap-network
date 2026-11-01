$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/run-source-profile.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/configured-stop-binding.psm1') -Force
$script:checks=0;$script:hashes=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw $why}}
function Clone($x){$x|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$sdk='C:/PGN/native/20261018-fused-high47-v1'
$q='C09786DACA5D6A6200BC71352BFAE319AB7B731AC0D9F79045BB494B25975EE9'
$image='DA55D109D9E5BD366DC6A9D8E3AE29F7A5D831B4839AC17FD891ADF0F8B23906'
$dll='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'
$b=Clone @{uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$sdk;executable=$sdk+'/bin/pgn-continuous-gpu.exe';start='878591000000000000000';end='878591000000000000424';length='424';source_boundary_protocol='pgn-source-legacy-fused-high47-v1';engine_upgrade=@{policy='GPU_OWNED_HIGH47_BARRETT_TWO_LANES_FUSED_SQUARE_V1';qualification_sha256=$q};pins=@(@{path=$sdk+'/bin/pgn-continuous-gpu.exe';sha256=$image},@{path=$sdk+'/bin/cudart64_12.dll';sha256=$dll});unknown_extension=@{keep='original'}}
$hash={param($path);$script:hashes++;foreach($pin in $b.pins){if($path-ceq$pin.path){return $pin.sha256}};throw 'Unexpected hash input'}
$before=$b|ConvertTo-Json -Depth 12 -Compress
$profile=Get-PgnVerifiedRunSourceProfile $b $hash
Check ($script:hashes-eq2-and$profile.protocol-ceq$b.source_boundary_protocol-and$profile.image_sha256-ceq$image.ToLowerInvariant()) 'exact fused high47 capture after both pin reads'
Check (($b|ConvertTo-Json -Depth 12 -Compress)-ceq$before) 'profile capture must preserve binding'
$capacity=& (Get-Module configured-stop-binding) {param($binding);Get-PgnConfiguredSourceCapacity $binding} $b
Check ($capacity.policy-ceq$b.engine_upgrade.policy-and$capacity.sdk-ceq$sdk-and$capacity.qualification_sha256-ceq$q) 'fused high47 exact capacity metadata'
foreach($poison in @('policy','qualification','sdk','exe','image','runtime','duplicate','extra','missingprotocol','wrongprotocol','nullprotocol')){
 $bad=Clone $b
 switch($poison){
  policy {$bad.engine_upgrade.policy='GPU_OWNED_UNKNOWN'}
  qualification {$bad.engine_upgrade.qualification_sha256='0'*64}
  sdk {$bad.sdk='C:/PGN/native/20261012-gpu-owned-source-v1'}
  exe {$bad.executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe'}
  image {$bad.pins[0].sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'}
  runtime {$bad.pins[1].sha256='0'*64}
  duplicate {$bad.pins=@($bad.pins[0],$bad.pins[0])}
  extra {$bad.pins+=@{path='C:/UNKNOWN';sha256='0'*64}}
  missingprotocol {$bad.PSObject.Properties.Remove('source_boundary_protocol')}
  wrongprotocol {$bad.source_boundary_protocol='pgn-source-legacy-a73'}
  nullprotocol {$bad.source_boundary_protocol=$null}
 }
 $prior=$script:hashes;$failed=$false
 try{Get-PgnVerifiedRunSourceProfile $bad $hash|Out-Null}catch{$failed=$true}
 Check $failed ('poison must refuse: '+$poison)
 Check ($script:hashes-eq$prior) 'metadata poison refuses before filesystem callback'
}
$badHash={param($path);'0'*64};$failed=$false;try{Get-PgnVerifiedRunSourceProfile $b $badHash|Out-Null}catch{$failed=$true};Check $failed 'actual pin mismatch refuses'
Write-Output ('PASS '+$script:checks+' fused high47 declaration/capacity/poison checks; no filesystem image or process/driver access; injected hash observations only')

Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
$first=[Numerics.BigInteger]::Parse('878591000000000000000');$end=$first+[Numerics.BigInteger]::Pow(10,15)
Check ($capacity.scope_start-ceq'866996971464348925952'-and$capacity.scope_end-ceq'885443715538058477568') 'high47 capacity never advertises high10'
Assert-PgnQualifiedSourceRange $b $first $end
$g=Get-PgnSourceLaunchGeometry $first $end '0' -HighWord 47
Check ($g.requested_start-ceq$first.ToString()-and$g.descriptors-ceq'15523'-and$g.remainder-ceq'20') 'actual native allocation exact fulljob geometry'
$g=Get-PgnSourceLaunchGeometry $first $end '257698037740' -HighWord 47
Check ($g.resume_start-ceq'878591000257698037740'-and$g.remaining-ceq'999742301962260') 'actual GPU stopped-position resume retains original native scope'
foreach($case in @(
 @('199999990000000000000','199999991000000000000'),
 @('866996971464348925953','866996971464348925954'),
 @('885443715538058477468','885443715538058477469'),
 @('878591000000000000000','878592000000000000001')
)){$failed=$false;try{Assert-PgnQualifiedSourceRange $b ([Numerics.BigInteger]::Parse($case[0])) ([Numerics.BigInteger]::Parse($case[1]))}catch{$failed=$true};Check $failed 'high47 wrongdomain/floor/tail/capacity must refuse'}
$failed=$false;try{Get-PgnSourceLaunchGeometry $first $end|Out-Null}catch{$failed=$true};Check $failed 'existing geometry callers keep default high10 contract'
Write-Output ('PASS '+$script:checks+' high47 declaration/domain/capacity/poison checks; no CUDA/process/driver access')
