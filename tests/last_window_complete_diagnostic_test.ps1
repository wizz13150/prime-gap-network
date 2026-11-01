# Closed synthetic SOURCE journals + immutable historical reference only.
# No child, driver, GPU, remote endpoint, production DB or stage is accessed.
$ErrorActionPreference='Stop';$script:checks=0
function Check($Ok,[string]$Why){++$script:checks;if(!$Ok){throw $Why}}
function Held([scriptblock]$Code,[string]$Why){$failed=$false;try{$null=&$Code}catch{$failed=$true};Check $failed $Why}
function Bytes([string]$Text){return ,([Text.UTF8Encoding]::new($false).GetBytes($Text))}
Import-Module (Join-Path $PSScriptRoot '../tools/full-request-prefix-diagnostic.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/last-window-complete-diagnostic.psm1') -Force
Check ($PSVersionTable.PSVersion.Major -eq 5) 'PS5 closed qualification required'
$referencePath=Join-Path $PSScriptRoot '../evidence/root555-legacy-last-window-reference-20261015.tsv'
$reference=[IO.File]::ReadAllBytes($referencePath)
Check ((Get-FileHash -LiteralPath $referencePath -Algorithm SHA256).Hash -ceq 'B12BAE107B8BA6004F59219572B7C0244A3606419E7F4E27ED5E9B184D31680E') 'Exact retained reference bytes'
$g=Get-PgnLastWindowGeometry;$s=[Numerics.BigInteger]::Parse($g.start);$end=[Numerics.BigInteger]::Parse($g.end);$width=[Numerics.BigInteger]::Parse($g.width)
Check ($g.count -ceq '3' -and $g.last_owned_length -ceq '2764472320') 'Analytical descriptor count and terminal clip'
Check ((2*$width+[Numerics.BigInteger]::Parse($g.last_owned_length)).ToString() -ceq $g.length) 'Exact three-piece window sum'
Check (([Numerics.BigInteger]::Parse('199932000000000000000')+15520*$width).ToString() -ceq $g.start) 'True global descriptor15520 alignment'
Check ((15520%2) -eq 0 -and (15522%2) -eq 0) 'Global/local slot0/1/0 parity'
$refText=[Text.UTF8Encoding]::new($false,$true).GetString($reference)
$refPairs=@($refText.Split([char]10)|Where-Object{$_ -cne ''}|ForEach-Object{$v=$_ -split "`t";[pscustomobject]@{p=$v[0];q=$v[1]}})
function Gap([string]$P,[string]$Q){$gap=([Numerics.BigInteger]::Parse($Q)-[Numerics.BigInteger]::Parse($P)).ToString();return "[phase0-gpu] GAP merit=1.9000 gap=$gap lower=$P upper=$Q verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown time_basis=source_batch_publication time_status=unavailable`n"}
function Journal([switch]$NoCross,[string]$OmitP,[switch]$ExtraInterior,[switch]$Duplicate,[switch]$ExtraCross){
 $text="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=3 original_start=$($g.start) length=$($g.length) proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2`n";$log='';$count=0
 for($i=0;$i -lt 3;$i++){
  $first=$s+$i*$width;$ownedEnd=[Numerics.BigInteger]::Min($end,$s+($i+1)*$width)
  foreach($pair in $refPairs){$p=[Numerics.BigInteger]::Parse($pair.p);if($p -ge $first -and $p -lt $ownedEnd -and $pair.p -cne $OmitP){$line=Gap $pair.p $pair.q;$text+=$line;$log+=$line;++$count;if($Duplicate -and $pair.p -ceq $refPairs[0].p){$text+=$line;$log+=$line;++$count}}}
  if($i -eq 0 -and $ExtraInterior){$line=Gap ($s+1000).ToString() ($s+1800).ToString();$text+=$line;$log+=$line;++$count}
  $tp=($ownedEnd-1).ToString();$tq=($ownedEnd+1).ToString()
  if($i -eq 2){
   if($NoCross){$tp=($end-97).ToString();$tq=($end+3).ToString()}
   else{$tp=($end-100).ToString();$tq=($end+744).ToString();$line=Gap $tp $tq;$text+=$line;$log+=$line;++$count}
   if($ExtraCross){$line=Gap ($end-300).ToString() ($end+600).ToString();$text+=$line;$log+=$line;++$count}
  }
  $text+="[pgn-source-boundary-v2] descriptor=$i owned_first=$first owned_end=$ownedEnd status=1 terminal_p=$tp terminal_q=$tq examined=7 source_tests=13 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE`n"
  $positions=($ownedEnd-$s).ToString();$batches=$i+1;$tests=100*$batches;$time=1000*$batches
  $text+="[pgn-counters] time_ns=$time positions=$positions gaps=$count tests=$tests batches=$batches processed_gaps=$count`n"
 }
 $text+="[phase0-gpu] gaps reported=$count verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
 [pscustomobject]@{text=$text;source=(Bytes $text);log=(Bytes $log)}
}
$a=Journal;$proof=Test-PgnLastWindowComplete $a.source $a.log $reference
Check ($proof.accepted -and $proof.positions -ceq '131613491200' -and $proof.batches -ceq '3') 'Three fully paid groups completed'
Check ($proof.interior_pairs -eq 17 -and $proof.terminal_crossing_pairs -eq 1 -and $proof.gaps -ceq '18') 'Exactly seventeen interiors plus one allowed terminal crossing'
Check ($proof.last_owned_length -ceq '2764472320' -and $proof.slot_sequence -ceq '0/1/0') 'Actual last marker pays terminal clip'
Check ($proof.window_source_complete -and !$proof.independent_coverage -and !$proof.full_original_block_coverage -and !$proof.stop_resume_accepted -and !$proof.performance_qualified) 'Claims remain restricted to this SOURCE window'
$no=Journal -NoCross;$p=Test-PgnLastWindowComplete $no.source $no.log $reference
Check ($p.gaps -ceq '17' -and $p.terminal_crossing_pairs -eq 0) 'Terminal gap below800 needs no extra GAP'
$crlf=[Text.Encoding]::UTF8.GetString($a.source).Replace("`n","`r`n")
Check ((Test-PgnLastWindowComplete (Bytes $crlf) $a.log $reference).accepted) 'Actual Windows stdout line endings accepted unchanged'
$omit=Journal -OmitP $refPairs[0].p
Held {Test-PgnLastWindowComplete $omit.source $omit.log $reference} 'Missing reference pair held even with self-consistent counters'
$extra=Journal -ExtraInterior
Held {Test-PgnLastWindowComplete $extra.source $extra.log $reference} 'Extra interior pair held even with self-consistent counters'
$dup=Journal -Duplicate
Held {Test-PgnLastWindowComplete $dup.source $dup.log $reference} 'Duplicate lower held'
$extra=Journal -ExtraCross
Held {Test-PgnLastWindowComplete $extra.source $extra.log $reference} 'Second terminal crossing held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('terminal_q='+($end+744).ToString(),'terminal_q='+($end+745).ToString())) $a.log $reference} 'Crossing must equal exact final marker tuple'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('owned_end='+$g.end,'owned_end='+($end-1).ToString())) $a.log $reference} 'Wrong final clipped boundary held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('positions='+$g.length,'positions='+([Numerics.BigInteger]::Parse($g.length)-1).ToString())) $a.log $reference} 'Incomplete final positions held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('descriptors=3','descriptors=4')) $a.log $reference} 'Wrong remaining descriptor count held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('original_start='+$g.start,'original_start='+($s+1).ToString())) $a.log $reference} 'Different window cannot reuse historical proof'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('boundary_protocol=pgn-source-boundary-v2','')) $a.log $reference} 'Historical undeclared header cannot qualify v2'
Held {Test-PgnLastWindowComplete (Bytes ([regex]::Replace($a.text,'(?m)^\[pgn-source-boundary-v2\] descriptor=2[^\n]*\n',''))) $a.log $reference} 'Missing last boundary held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('descriptor=2 ','descriptor=3 ')) $a.log $reference} 'Wrong local descriptor ordinal held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('status=1 ','status=7 ')) $a.log $reference} 'Nonaccepted terminal status held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('gaps reported=18','gaps reported=17')) $a.log $reference} 'Wrong completion summary held'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace("[phase0-gpu] --- end ---`n",'')) $a.log $reference} 'Missing end footer held'
Held {Test-PgnLastWindowComplete (Bytes ($a.text+"[phase0-gpu] --- end ---`n")) $a.log $reference} 'Duplicate footer held'
Held {Test-PgnLastWindowComplete (Bytes ([regex]::Replace($a.text,'\[phase0-gpu\] gaps reported=18[^\n]*\n\[phase0-gpu\] --- end ---\n',('[phase0-gpu] stopped positions='+$g.length+" checkpoint=TESTONLY`n")))) $a.log $reference} 'Even stopped-at-length is not natural completion'
Held {Test-PgnLastWindowComplete (Bytes $a.text.Replace('batches=1 processed','batches=2 processed')) $a.log $reference} 'Nonzero baseline/mispaid first cumulative group held'
Held {Test-PgnLastWindowComplete $a.source (Bytes ($a.log+'unexpected')) $reference} 'Stdout and actual gap log must match exactly'
$bad=[byte[]]$reference.Clone();$bad[0]=[byte]50
Held {Test-PgnLastWindowComplete $a.source $a.log $bad} 'Changed retained reference bytes held'
Held {Test-PgnLastWindowComplete $a.source $a.log ([byte[]]@())} 'Absent reference held'
$runner=Join-Path $PSScriptRoot '../tools/test-last-window-complete-source.ps1'
$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$null,[ref]$errors)
Check ($errors.Count -eq 0) 'New runner parses under PS5'
$notArmed=& $runner|ConvertFrom-Json
Check ($notArmed.state -ceq 'NOT_ARMED' -and $notArmed.gpu_launches -eq 0) 'Default runner returns before imports/host access'
foreach($budget in @(0,61)){
 $heldResult=& $runner -ExecuteDiagnostic -DeadlineSeconds $budget -StopWaitSeconds 1|ConvertFrom-Json
 Check ($heldResult.state -ceq 'Held' -and !$heldResult.accepted -and $heldResult.gpu_launches -eq 0 -and $heldResult.error -like 'Explicit completion1..60s*') 'Invalid bounded budget fails before host admission'
}
# Exercise the exact runner wait/exit/absence/validator statements with fake
# callbacks, never fake SOURCE physical qualification or launch a child.
$try=@($ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.TryStatementAst]})[0]
$statements=@($try.Body.Statements);$from=-1;$to=-1
for($i=0;$i -lt $statements.Count;$i++){if($statements[$i].Extent.Text -ceq '$one=LaunchEpoch ''complete'' $null'){$from=$i};if($statements[$i].Extent.Text -ceq '$proof=Test-PgnLastWindowComplete $source $log $reference'){$to=$i}}
Check ($from -ge 0 -and $to -gt $from) 'Actual launch-through-proof AST slice exists'
$code=[scriptblock]::Create((@($statements[$from..$to]|ForEach-Object{$_.Extent.Text}) -join "`n"))
function MockExecution([bool]$Finishes,[bool]$StopPresent,[bool]$CheckpointPresent,[bool]$ExitFails=$false){
 $script:calls=[ordered]@{launch=0;stop=0;await=0;exit=0;collect=0;wait_ms=0};$DeadlineSeconds=60;$script:mockFinishes=$Finishes
 $process=[pscustomobject]@{HasExited=$Finishes};$process|Add-Member ScriptMethod WaitForExit {param($ms) $script:calls.wait_ms=$ms;return $script:mockFinishes}
 $mock=[pscustomobject]@{process=$process;manifest=[pscustomobject]@{stop='TESTONLY_STOP';stdout='TESTONLY_STDOUT'};state='TESTONLY_CHECKPOINT';gaplog='TESTONLY_GAPLOG'}
 function LaunchEpoch($Name,$Checkpoint){Check ($Name -ceq 'complete' -and $null -eq $Checkpoint) 'Exactly a fresh epoch, never manufactured checkpoint';++$script:calls.launch;return $mock}
 function StopOwn($Epoch){++$script:calls.stop;return $true}
 function AwaitOwnStop($Epoch){++$script:calls.await;$Epoch.process.HasExited=$true;return $true}
 function CollectExitReceipt($Epoch){++$script:calls.collect;return @{exit_code=0}}
 function ExitReceipt($Epoch){++$script:calls.exit;if($ExitFails){throw 'Own diagnostic exited nonzero; preserved, not completion'};return @{exit_code=0}}
 function Test-Path {param($LiteralPath) if($LiteralPath -ceq 'TESTONLY_STOP'){return $StopPresent};if($LiteralPath -ceq 'TESTONLY_CHECKPOINT'){return $CheckpointPresent};throw 'Unexpected fixture path'}
 function ReadHeld($Path){if($Path -ceq 'TESTONLY_STDOUT'){return ,$a.source};if($Path -ceq 'TESTONLY_GAPLOG'){return ,$a.log};throw 'Unexpected read callback'}
 $message='';try{. $code}catch{$message=$_.Exception.Message}
 Check ($script:calls.launch -eq 1 -and $script:calls.wait_ms -eq 60000) 'Single bounded epoch only'
 if(!$Finishes){Check ($message -like 'COMPLETION_DEADLINE_NORMAL_OWN_STOP*' -and $script:calls.stop -eq 1 -and $script:calls.await -eq 1 -and $script:calls.collect -eq 1 -and $script:calls.exit -eq 0) 'Timeout normal OWN STOP/actual collection then Held, no qualification'}
 elseif($ExitFails){Check ($message -like 'Own diagnostic exited nonzero*' -and $script:calls.exit -eq 1 -and $script:calls.stop -eq 0) 'Nonzero actual exit cannot qualify even complete SOURCE'}
 elseif($StopPresent -or $CheckpointPresent){Check ($message -like $(if($StopPresent){'STOP_PRESENT*'}else{'UNEXPECTED_FINAL_CHECKPOINT*'}) -and $script:calls.exit -eq 1 -and $script:calls.stop -eq 0) 'Residual STOP/checkpoint holds without deletion or relaunch'}
 else{Check (!$message -and $proof.accepted -and $script:calls.exit -eq 1 -and $script:calls.stop -eq 0) 'Natural completion pays validation without STOP'}
}
MockExecution $true $false $false
MockExecution $false $false $false
MockExecution $true $true $false
MockExecution $true $false $true
MockExecution $true $false $false $true
foreach($pin in @(
 @('test-terminal-boundary-stop-resume.ps1','5DD1A42935B26AF11242162560395A87DDEBEFFB204BFC99E44D304AC543F968'),
 @('source-stop-resume-diagnostic.psm1','73E7E360EC14B31C5250419EC610F60B00B465FBEC527283FFE322C0A85C911B'),
 @('diagnostic-child-process.psm1','8EC24A342507B04816228180FC4E37F0C485EEF1D5E64A418728579B91A59E86'),
 @('source-stop-checkpoint-evidence.psm1','6CE6A91BA962D491329DAF248D03B69F81CBDB789FE9BE95EE871BE88718ACD8'),
 @('full-request-prefix-diagnostic.psm1','1C12FC7554ECA5CBD8019E688AAB10014FC2B02A26BBD8C40DC4F8195559B7AB'),
 @('test-full-request-prefix-stop-resume.ps1','2AB640453CFB13B64FF864B975185CA11D78A37220E246D1E4CA8F99E8BBCD90')
)){Check ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot ('../tools/'+$pin[0])) -Algorithm SHA256).Hash -ceq $pin[1]) ('Frozen dependency unchanged: '+$pin[0])}
'last_window_complete_diagnostic_test: '+$script:checks+' checks, 0 failures (PS5 synthetic SOURCE/fixture callbacks only)'
