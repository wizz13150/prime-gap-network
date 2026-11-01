param([Parameter(Mandatory=$true)][string]$ProfileModule,[Parameter(Mandatory=$true)][string]$RangeModule,[Parameter(Mandatory=$true)][string]$ConfiguredModule)
$ErrorActionPreference='Stop'
Import-Module $RangeModule -Force
Import-Module $ProfileModule -Force
$script:checks=0;$script:hashes=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw "CHECK $script:checks $why"}}
function Clone($v){$v|ConvertTo-Json -Depth 10 -Compress|ConvertFrom-Json}
function Binding([bool]$v2){
 $sdk=if($v2){'C:/PGN/native/20261015-source-boundary-v2'}else{'C:/PGN/native/20261012-gpu-owned-source-v1'}
 $image=if($v2){'74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B'}else{'A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'}
 $policy=if($v2){'GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2'}else{'GPU_OWNED_HIGH10_SOURCE_V1'}
 $qualification=if($v2){'2DB06EAD094579A18588BBEF6429102A5E59052BCF2D09E3A13E0E8E84964C52'}else{'99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'}
 $b=Clone @{uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$sdk;executable=($sdk+'/bin/pgn-continuous-gpu.exe');start='199932000019059617039';end='199932000019059617463';length='424';engine_upgrade=@{policy=$policy;qualification_sha256=$qualification};pins=@(@{path=($sdk+'/bin/pgn-continuous-gpu.exe');sha256=$image},@{path=($sdk+'/bin/cudart64_12.dll');sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'});community=@{source_sha256='fixture';observed_utc='original'};unknown_extension=@{original='preserved'}}
 if($v2){$b|Add-Member source_boundary_protocol 'pgn-source-boundary-v2'}
 return $b
}
$hash={param($p);$script:hashes++;foreach($b in @((Binding $false),(Binding $true))){foreach($pin in $b.pins){if($pin.path -ceq $p){return $pin.sha256}}};throw 'Unexpected fake hash path'}
function RefusedBeforeHash($b){$before=$script:hashes;$failed=$false;try{Get-PgnVerifiedRunSourceProfile $b $hash|Out-Null}catch{$failed=$true};Check $failed 'metadata mismatch refuses';Check ($script:hashes -eq $before) 'metadata refuses before any hash callback'}
function Refused([scriptblock]$body){$failed=$false;try{& $body|Out-Null}catch{$failed=$true};Check $failed 'completion mismatch refuses'}
$v2=Binding $true;$before=$v2|ConvertTo-Json -Depth 10 -Compress
$errorText='';$capture=$null
try{$capture=Get-PgnVerifiedRunSourceProfile $v2 $hash}catch{$errorText=$_.Exception.Message}
# Baseline goes first: old A73-only gate refuses this exact new root-qualified
# declaration without opening an image or launching anything.
Check (!$errorText) ('exact v2 declaration must capture: '+$errorText)
Check ($script:hashes -eq 2 -and $capture.protocol -ceq 'pgn-source-boundary-v2' -and $capture.image_sha256 -ceq '74a0fdd01ccf242fa3ca8714e4765c23022987c6785889cd8b1ecfa3aa27472b') 'same two observed hash callbacks and canonical v2 profile'
Check (($capture.Keys|Sort-Object) -join ',' -ceq 'executable,image_sha256,protocol,schema') 'unchanged four-string capture ABI'
Check (($v2|ConvertTo-Json -Depth 10 -Compress) -ceq $before) 'mapping preserves all original binding extensions'
Assert-PgnQualifiedSourceRange $v2 ([Numerics.BigInteger]::Parse($v2.start)) ([Numerics.BigInteger]::Parse($v2.end))
foreach($kind in @('policy','nullpolicy','sdk','exe','qualification','imagepin','dllpin','duplicate','extra','missingprotocol','nullprotocol','legacyprotocol','numericprotocol')){
 $b=Clone $v2
 switch($kind){
  policy {$b.engine_upgrade.policy='GPU_OWNED_UNKNOWN'}
  nullpolicy {$b.engine_upgrade.policy=$null}
  sdk {$b.sdk=(Binding $false).sdk}
  exe {$b.executable=(Binding $false).executable}
  qualification {$b.engine_upgrade.qualification_sha256=(Binding $false).engine_upgrade.qualification_sha256}
  imagepin {$b.pins[0].sha256=(Binding $false).pins[0].sha256}
  dllpin {$b.pins[1].sha256='0'*64}
  duplicate {$b.pins=@($b.pins[0],$b.pins[0])}
  extra {$b.pins+=@{path='C:/UNKNOWN';sha256='0'*64}}
  missingprotocol {$b.PSObject.Properties.Remove('source_boundary_protocol')}
  nullprotocol {$b.source_boundary_protocol=$null}
  legacyprotocol {$b.source_boundary_protocol='pgn-source-legacy-a73'}
  numericprotocol {$b.source_boundary_protocol=2}
 }
 RefusedBeforeHash $b
}
$a73=Binding $false;$legacy=Get-PgnVerifiedRunSourceProfile $a73 $hash
Check ($legacy.protocol -ceq 'pgn-source-legacy-a73' -and $legacy.image_sha256 -ceq 'a73a6e383308560edc36ce351b09eb449fa099275361be74be89020b03864b43') 'historical A73 absent declaration remains explicit legacy capture'
$a73|Add-Member source_boundary_protocol 'pgn-source-legacy-a73';Get-PgnVerifiedRunSourceProfile $a73 $hash|Out-Null
$bad=Clone $a73;$bad.source_boundary_protocol='pgn-source-boundary-v2';RefusedBeforeHash $bad
$bad=Clone $a73;$bad.source_boundary_protocol=$null;RefusedBeforeHash $bad
$manifest=Clone @{uuid=$v2.uuid;start=$v2.start;end=$v2.end;run=('a'*32);stdout='fixture-only';source_profile=$capture}
$exit=Clone @{run=$manifest.run;utc='2026-10-15T00:00:00.0000000Z';exit_code=0}
$tail="[pgn-counters] time_ns=1 positions=424 gaps=1 tests=9007199254740997 batches=1 processed_gaps=1`n[phase0-gpu] gaps reported=1 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
$r=Get-PgnSourceCompletionEvidence $v2 $manifest $tail $exit $false
Check ($r.source_range_complete -and !$r.independent_coverage -and $r.source_tests -ceq '9007199254740997' -and $r.proof_status -ceq 'SOURCE_PRP_INDEPENDENT_PROOF_PENDING') 'v2 completes without obsolete state-file footer, not independent proof'
$csv="Start,End,Status,Owner`n199900,200000,Open,`n"
$nextStart=[Numerics.BigInteger]::Parse($v2.end);$nextEnd=$nextStart+424
$queued=Get-PgnQueuedBinding $v2 $r $csv '11111111-2222-4333-8444-555555555555' $nextStart.ToString() $nextEnd.ToString()
Check ($queued.start -ceq $nextStart.ToString() -and $queued.source_boundary_protocol -ceq 'pgn-source-boundary-v2') 'equal-length queued continuation retains explicit v2 guard/profile'
$bad=Clone $v2;$bad.source_boundary_protocol=$null
Refused {Get-PgnQueuedBinding $bad $r $csv '11111111-2222-4333-8444-555555555555' $nextStart.ToString() $nextEnd.ToString()}
foreach($kind in @('absent','null','image','protocol','extra','schema')){
 $m=Clone $manifest
 switch($kind){
  absent {$m.PSObject.Properties.Remove('source_profile')}
  null {$m.source_profile=$null}
  image {$m.source_profile.image_sha256=$legacy.image_sha256}
  protocol {$m.source_profile.protocol='pgn-source-legacy-a73'}
  extra {$m.source_profile|Add-Member extra 'not capture'}
  schema {$m.source_profile.schema='unknown'}
 }
 Refused {Get-PgnSourceCompletionEvidence $v2 $m $tail $exit $false}
}
foreach($value in @($null,'0',0.0,$false,1)){$e=Clone $exit;$e.exit_code=$value;Refused {Get-PgnSourceCompletionEvidence $v2 $manifest $tail $e $false}}
Refused {Get-PgnSourceCompletionEvidence $v2 $manifest $tail $exit $true}
Refused {Get-PgnSourceCompletion $v2 $manifest $tail $exit $false $true}
Refused {Get-PgnSourceCompletionEvidence $v2 $manifest ($tail.Replace('positions=424','positions=423')) $exit $false}
Refused {Get-PgnSourceCompletionEvidence $v2 $manifest ($tail.Replace('verification_failures=0','verification_failures=7')) $exit $false}
Refused {Get-PgnSourceCompletionEvidence $v2 $manifest ($tail+"[phase0-gpu] stopped positions=424 checkpoint=fixture`n") $exit $false}
$historical=Clone $manifest;$historical.PSObject.Properties.Remove('source_profile')
$r=Get-PgnSourceCompletionEvidence $a73 $historical $tail $exit $false
Check ($r.source_range_complete -and !$r.independent_coverage) 'A73 historical profile absence remains un-enriched'
Check (!$historical.PSObject.Properties['source_profile']) 'historical manifest never synthesized/enriched'
$plain=Clone @{uuid=$v2.uuid;start=$v2.start;end=$v2.end;length=$v2.length}
$genericTail=$tail.Replace('[phase0-gpu] gaps reported=',"[phase0-gpu] range complete; state file removed`n[phase0-gpu] gaps reported=")
$r=Get-PgnSourceCompletionEvidence $plain $historical $genericTail $exit $false
Check $r.source_range_complete 'generic historical footer path preserved without new profile claim'
$configured=Import-Module $ConfiguredModule -Force -PassThru
$capacity=& $configured {param($b) Get-PgnConfiguredSourceCapacity $b} $v2
Check ($null -ne $capacity -and $capacity.policy -ceq 'GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2' -and $capacity.sdk -ceq $v2.sdk -and $capacity.qualification_sha256 -ceq $v2.engine_upgrade.qualification_sha256) 'configured source budget is exact v2, not null or inherited A73'
Check ($capacity.schema -ceq 'pgn-qualified-source-capacity-1' -and $capacity.guard -ceq 'Assert-PgnQualifiedSourceRange' -and $capacity.scope_start -ceq '184467440737095516160' -and $capacity.scope_end -ceq '202914184810805067776' -and $capacity.max_length -ceq '1000000000000000') 'unchanged capacity budget/domain schema, actual geometry still required at launch'
$a73Capacity=& $configured {param($b) Get-PgnConfiguredSourceCapacity $b} $a73
Check ($null -ne $a73Capacity -and $a73Capacity.policy -ceq 'GPU_OWNED_HIGH10_SOURCE_V1' -and $a73Capacity.qualification_sha256 -ceq $a73.engine_upgrade.qualification_sha256) 'A73 budget remains qualified despite new class30 floor guard'
foreach($kind in @('policy','protocol','qualification','image')){
 $b=Clone $v2
 switch($kind){policy {$b.engine_upgrade.policy='unknown'};protocol {$b.source_boundary_protocol=$null};qualification {$b.engine_upgrade.qualification_sha256=$a73.engine_upgrade.qualification_sha256};image {$b.pins[0].sha256=$a73.pins[0].sha256}}
 $badCapacity=& $configured {param($b) Get-PgnConfiguredSourceCapacity $b} $b
 Check ($null -eq $badCapacity) 'unknown/mixed budget stays unavailable, not A73 fallback'
}
Write-Output "PASS checks=$script:checks failures=0; pure exact A73/v2 declaration, synthetic hashes/counters, no image/process/SSH/GPU or RAW lineage proof"
