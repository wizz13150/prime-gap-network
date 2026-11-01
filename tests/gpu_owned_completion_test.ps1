$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
$binding=[pscustomobject]@{uuid='physical3090';start='199910000000000000000';end='199911000000000000000';length='1000000000000000';
 executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';
 engine_upgrade=[pscustomobject]@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'}}
$manifest=[pscustomobject]@{uuid=$binding.uuid;start=$binding.start;end=$binding.end;run=('a'*32);stdout='immutable-source'}
$exit=[pscustomobject]@{run=$manifest.run;utc='2026-10-12T20:23:22Z';exit_code=0}
$tail="[pgn-counters] time_ns=215318591470500 positions=1000000000000000 gaps=160756 tests=3331748876007 batches=15523 processed_gaps=160756`n[phase0-gpu] gaps reported=160756 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
$r=Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false
if(!$r.source_range_complete -or $r.independent_coverage -or $r.source_positions -cne $binding.length -or
 $r.source_tests -cne '3331748876007'){throw 'Actual SDK completion contract not conserved'}
$count=1
function Reject([scriptblock]$Action){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true}
 if(!$rejected){throw 'Unsafe GPU-owned completion accepted'};$script:count++}
Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $true $false}
Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $true}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('positions=1000000000000000','positions=999')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('processed_gaps=160756','processed_gaps=160755')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('gaps reported=160756','gaps reported=160755')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('--- end ---','partial')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('verification_failures=0','verification_failures=1')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ("[phase0-gpu] stopped positions=1 checkpoint=x`n"+$tail) $exit $false $false}
$exit.exit_code=1;Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false}
$exit.exit_code=$null;Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false};$exit.exit_code=0
$binding.engine_upgrade.policy='OTHER';Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false}
$binding.engine_upgrade.policy='GPU_OWNED_HIGH10_SOURCE_V1';$binding.engine_upgrade.qualification_sha256='F'*64
Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false}
@{status='PASS';checks=$count;scope='Closed metadata contract; no GPU or runtime actions'}|ConvertTo-Json -Compress
