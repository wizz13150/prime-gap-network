$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
$pgnTokens=$null;$pgnErrors=$null
$pgnAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../tools/native-control.ps1'),[ref]$pgnTokens,[ref]$pgnErrors)
if($pgnErrors.Count){throw 'Controller syntax error'}
foreach($pgnFunctionName in @('Get-PgnSnapshotSourceCompletion','Get-PgnSnapshotCompletion')){
 $pgnFunctions=@($pgnAst.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $pgnFunctionName},$true))
 if($pgnFunctions.Count -ne 1){throw 'Actual controller must define each snapshot source function exactly once'}
 $pgnFunction=$pgnFunctions[0]
 . ([scriptblock]::Create($pgnFunction.Extent.Text))
}
$b=@{start='100000000000000000000';end='100001000000000000000';length='1000000000000000';uuid='GPU-fixture'}
$m=@{run=('a'*32);uuid=$b.uuid;start=$b.start;end=$b.end}
$exit=@{run=$m.run;exit_code=0}
$tail="[pgn-counters] time_ns=1 positions=1000000000000000 gaps=1 tests=1 batches=1`n[phase0-gpu] range complete; state file removed`n[phase0-gpu] gaps reported=1  verification_failures=0  records_new=1`n[phase0-gpu] --- end ---`n"
$receipt=Get-PgnSnapshotCompletion $b $m $false $exit $tail $false $false $false
if(!$receipt -or $receipt.run -cne $m.run -or !$receipt.source_range_complete -or $receipt.source_exit_code -ne 0){throw 'Natural complete source must derive without a published last-completion'}
foreach($flags in @(@($true,$false,$false,$false),@($false,$true,$false,$false),@($false,$false,$true,$false),@($false,$false,$false,$true))){if(Get-PgnSnapshotCompletion $b $m $flags[0] $exit $tail $flags[1] $flags[2] $flags[3]){throw 'Owned/checkpoint/STOP/hold accepted'}}
foreach($bad in @(@{run=('b'*32);exit_code=0},@{run=$m.run;exit_code=1},@{run=$m.run;exit_code=$null})){if(Get-PgnSnapshotCompletion $b $m $false $bad $tail $false $false $false){throw 'Wrong/failed/unknown exit accepted'}}
foreach($badTail in @($tail.Replace('positions=1000000000000000','positions=1'),$tail.Replace('range complete; state file removed','STOPPED early'),$tail.Replace('verification_failures=0','verification_failures=1'))){if(Get-PgnSnapshotCompletion $b $m $false $exit $badTail $false $false $false){throw 'Incomplete or invalid source accepted'}}
foreach($pgnStopPresent in @($false,$true)){foreach($pgnOperatorStop in @($false,$true)){
 $pgnObserved=Get-PgnSnapshotSourceCompletion $b $m $false $exit $tail $false $pgnStopPresent $pgnOperatorStop
 if(!$pgnObserved -or !$pgnObserved.source_range_complete -or $pgnObserved.independent_coverage -ne $false -or
  $pgnObserved.source_observed_operator_stop -ne $pgnOperatorStop -or $pgnObserved.source_observed_stop_file -ne $pgnStopPresent){throw 'Full source result lost or STOP observation invented'}
 if(($pgnStopPresent -or $pgnOperatorStop) -and (Get-PgnSnapshotCompletion $b $m $false $exit $tail $false $pgnStopPresent $pgnOperatorStop)){throw 'Source-only proof authorized automatic succession while stopped'}
}}
foreach($pgnBadExit in @('0',$null,$false,1)){
 if(Get-PgnSnapshotSourceCompletion $b $m $false @{run=$m.run;exit_code=$pgnBadExit} $tail $false $false $true){throw 'Unknown/wrong exit admitted as source-only completion'}
}
foreach($pgnBadFlag in @($null,0,'false')){
 if(Get-PgnSnapshotSourceCompletion $b $m $pgnBadFlag $exit $tail $false $false $true){throw 'Unknown ownership admitted as stopped source evidence'}
 if(Get-PgnSnapshotSourceCompletion $b $m $false $exit $tail $false $false $pgnBadFlag){throw 'Unknown operator stop admitted as source evidence'}
 if(Get-PgnSnapshotCompletion $b $m $pgnBadFlag $exit $tail $false $false $false){throw 'Unknown ownership admitted by automatic completion'}
 if(Get-PgnSnapshotCompletion $b $m $false $exit $tail $pgnBadFlag $false $false){throw 'Unknown checkpoint admitted by automatic completion'}
 if(Get-PgnSnapshotCompletion $b $m $false $exit $tail $false $pgnBadFlag $false){throw 'Unknown stop file admitted by automatic completion'}
}
if(Get-PgnSnapshotSourceCompletion $b $m $false $exit ($tail.Replace('positions=1000000000000000','positions=1')) $false $false $true){throw 'Partial stopped source invented as full source completion'}
'PASS CLOSED snapshot SOURCE-only full completion retains real STOP flags; automatic candidate still rejects STOP; unknown exit/owner/partial held; no runtime writes'
