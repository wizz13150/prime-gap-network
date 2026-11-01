$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Numerics
Import-Module (Join-Path $PSScriptRoot '../tools/finite-counter-publications.psm1') -Force
function Reject([scriptblock]$Action){$rejected=$false;try{$null=& $Action}catch{$rejected=$true};if(!$rejected){throw 'Bad counter publication accepted'}}
$lines=@(1..17|ForEach-Object {'[pgn-counters] time_ns=1 positions='+($_*100)+' gaps='+$_+' tests='+($_*10)+' batches='+$_+' processed_gaps='+$_})
$partial='[pgn-counters] time_ns=1 positions=700 gaps=7 tests=80 batches=8 processed_gaps=7'
$withPartial=@($lines[0..6]+$partial+$lines[7..16])
$result=Get-PgnFiniteCounterPublications -Lines $withPartial
if($result.batches[8] -cne 'positions=800 gaps=8 tests=80 batches=8 processed_gaps=8' -or $result.changes.Count -ne 1 -or $result.changes[0].before -cne ($partial -creplace '^\[pgn-counters\] time_ns=1 ','') -or $result.fabricated){throw 'Actual changing publication was lost/fabricated'}
Reject {$bad=$withPartial.Clone();$bad[8]='[pgn-counters] time_ns=1 positions=699 gaps=8 tests=80 batches=8 processed_gaps=8';Get-PgnFiniteCounterPublications -Lines $bad}
Reject {Get-PgnFiniteCounterPublications -Lines $lines[0..14]}
Reject {Get-PgnFiniteCounterPublications -Lines $lines[0..15]}
$stable=Get-PgnFiniteCounterPublications -Lines $lines
if($stable.changes.Count -ne 0 -or $stable.batches.Count -ne 17){throw 'Stable legacy counter reader regression'}
'PASS actual partial+closing publications retained, exact17 closing records, no fabricated sample, raw regression and missing final/coverage rejected'
