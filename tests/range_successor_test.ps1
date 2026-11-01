$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
$binding=[pscustomobject]@{uuid='physical3090';start='199998000000000000000';end='199999000000000000000';length='1000000000000000';community=[pscustomobject]@{source_sha256='old';observed_utc='old'}}
$manifest=[pscustomobject]@{uuid=$binding.uuid;start=$binding.start;end=$binding.end;run=('a'*32);stdout='immutable-source'}
$exit=[pscustomobject]@{run=$manifest.run;utc='2026-10-11T01:00:00Z';exit_code=$null}
$tail="[pgn-counters] time_ns=1 positions=1000000000000000 gaps=10 tests=20 batches=30`n[phase0-gpu] range complete; state file removed`n[phase0-gpu] gaps reported=10 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
$receipt=Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false
if($receipt.independent_coverage -or !$receipt.source_range_complete){throw 'Source promotion boundary broken'}
$csv="Start,End,Status,Owner`n199900,200000,Open,`n"
$next=Get-PgnSuccessorBinding $binding $receipt $csv
if($next.start -cne $binding.end -or $next.end -cne '200000000000000000000' -or $binding.start -cne '199998000000000000000'){throw 'Exact ascending/non-mutating successor failed'}
function Reject([scriptblock]$Test){$rejected=$false;try{& $Test|Out-Null}catch{$rejected=$true};if(!$rejected){throw 'Unsafe successor was accepted'}}
Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $true $false}
Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $true}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('positions=1000000000000000','positions=999')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('verification_failures=0','verification_failures=1')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($tail.Replace('--- end ---','partial')) $exit $false $false}
Reject {Get-PgnSuccessorBinding $binding $receipt ($csv.Replace('Open','Reserved'))}
$exit.exit_code=1;Reject {Get-PgnSourceCompletion $binding $manifest $tail $exit $false $false}
$exit.exit_code=0
$lagged=$tail.Replace('positions=1000000000000000','positions=999997235527660').Replace('[phase0-gpu] gaps reported=',"[phase0-gpu] wall=8058.02 s  ints=1000000000000000  end_to_end=1.241e+11 ints/s`n[phase0-gpu] gaps reported=")
$closed=Get-PgnSourceCompletion $binding $manifest $lagged $exit $false $false
if($closed.source_positions -cne $binding.length -or $closed.source_periodic_positions -cne '999997235527660' -or $closed.source_tests_complete -or $closed.source_position_evidence -cne 'exact_final_wall_summary' -or $closed.independent_coverage){throw 'Lagged publication provenance boundary broken'}
Reject {Get-PgnSourceCompletion $binding $manifest ($lagged.Replace('ints=1000000000000000','ints=999')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($lagged.Replace('positions=999997235527660','positions=1000000000000001')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($lagged.Replace('gaps reported=10','gaps reported=11')) $exit $false $false}
Reject {Get-PgnSourceCompletion $binding $manifest ($lagged.Replace('ints/s', 'ints/s  (STOPPED early)')) $exit $false $false}
$exit.exit_code=$null;Reject {Get-PgnSourceCompletion $binding $manifest $lagged $exit $false $false}
$exit.exit_code=1;Reject {Get-PgnSourceCompletion $binding $manifest $lagged $exit $false $false}
$exit.exit_code=0
$queue=Get-PgnQueuedBinding $binding $receipt $csv '12345678-1234-1234-1234-123456789abc' '199996000000000000000' '199996500000000000000'
if($queue.length -cne '500000000000000' -or $queue.start -cne '199996000000000000000' -or $binding.length -cne '1000000000000000'){throw 'Queued exact length/source preservation failed'}
Reject {Get-PgnQueuedBinding $binding $receipt $csv 'bad' '199996000000000000000' '199997000000000000000'}
Reject {Get-PgnQueuedBinding $binding $receipt $csv '12345678-1234-1234-1234-123456789abc' '199998000000000000000' '199999000000000000000'}
$forward=Get-PgnQueuedBinding $binding $receipt $csv '12345678-1234-1234-1234-123456789abc' '199999000000000000000' '200000000000000000000'
if($forward.start -cne $binding.end -or $forward.length -cne $binding.length){throw 'Forward reserved block refused or modified'}
Reject {Get-PgnQueuedBinding $binding $receipt $csv '12345678-1234-1234-1234-123456789abc' '199896000000000000000' '199897000000000000000'}
Reject {Get-PgnQueuedBinding $binding $receipt ($csv.Replace('Open','Reserved')) '12345678-1234-1234-1234-123456789abc' '199996000000000000000' '199997000000000000000'}
'PASS exact predecessor/adjacent successor, no proof promotion, original binding preserved, checkpoint/STOP/partial/error/reservation refusal'
