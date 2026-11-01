$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/run-source-profile.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/configured-stop-binding.psm1') -Force
$script:checks=0;$script:hashes=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw $why}}
function Clone($x){$x|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$sdk='C:/PGN/native/20261017-lazy-shared-periodic-bank-v1'
$q='FC752A691686BA693EAFBB1273E0C1EB7B9CC5E47B9217B2E4F69E329101A5A8'
$image='9A76E632FA1761B26B68B33D6D7EEE15C1D7A0FF6E0D0D5772DA9C6F0C13171E'
$dll='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'
$b=Clone @{uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$sdk;executable=$sdk+'/bin/pgn-continuous-gpu.exe';start='199932000019059617039';end='199932000019059617463';length='424';source_boundary_protocol='pgn-source-legacy-lazy-shared-periodic-bank-v1';engine_upgrade=@{policy='GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_PERIODIC_BANK_V1';qualification_sha256=$q};pins=@(@{path=$sdk+'/bin/pgn-continuous-gpu.exe';sha256=$image},@{path=$sdk+'/bin/cudart64_12.dll';sha256=$dll});unknown_extension=@{keep='original'}}
$hash={param($path);$script:hashes++;foreach($pin in $b.pins){if($path-ceq$pin.path){return $pin.sha256}};throw 'Unexpected hash input'}
$before=$b|ConvertTo-Json -Depth 12 -Compress
$profile=Get-PgnVerifiedRunSourceProfile $b $hash
Check ($script:hashes-eq2-and$profile.protocol-ceq$b.source_boundary_protocol-and$profile.image_sha256-ceq$image.ToLowerInvariant()) 'exact lazy capture after both pin reads'
Check (($b|ConvertTo-Json -Depth 12 -Compress)-ceq$before) 'profile capture must preserve binding'
$capacity=& (Get-Module configured-stop-binding) {param($binding);Get-PgnConfiguredSourceCapacity $binding} $b
Check ($capacity.policy-ceq$b.engine_upgrade.policy-and$capacity.sdk-ceq$sdk-and$capacity.qualification_sha256-ceq$q) 'lazy exact capacity metadata'
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
Write-Output ('PASS '+$script:checks+' lazy declaration/capacity/poison checks; no filesystem image or process/driver access; injected hash observations only')
