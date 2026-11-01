$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/source-stop-resume-diagnostic.psm1') -Force
$script:checks=0
function Check($Ok,[string]$Why){$script:checks++;if(!$Ok){throw $Why}}
function Held([scriptblock]$Code,[string]$Why){$failed=$false;try{$null=&$Code}catch{$failed=$true};Check $failed $Why}
function Utf8([string]$Text){return ,([Text.UTF8Encoding]::new($false).GetBytes($Text))}
function Checkpoint([string]$Start,[string]$Length,[string]$Position){
 $bytes=New-Object byte[] 64;[Array]::Copy([Text.Encoding]::ASCII.GetBytes('PGNCG001'),$bytes,8);$s=[Numerics.BigInteger]::Parse($Start);$word=[Numerics.BigInteger]::Pow(2,64)
 $values=@([uint64]($s%$word),[uint64]10,[uint64]::Parse($Length),[uint64]::Parse($Position),[uint64]9007199254740993,[uint64]1,[uint64]1)
 for($i=0;$i -lt 7;$i++){[Array]::Copy([BitConverter]::GetBytes($values[$i]),0,$bytes,8+8*$i,8)};return ,$bytes
}
$start='199932000019059617039';$range=Get-PgnDiagnosticTwoDescriptorRange $start;$p=[Numerics.BigInteger]::Parse($range.first_prefix);$s=[Numerics.BigInteger]::Parse($start);$l=$range.length;$label='C:/PUBLIC-TESTONLY/stop/current-job/checkpoint'
Check ($range.descriptors -eq 2 -and $p -gt 0 -and [Numerics.BigInteger]::Parse($l) -gt $p) 'two real descriptor range, no small fabricated positive prefix'
$frontier=($s+$p).ToString();$oldP=($s+$p-100).ToString();$oldQ=($s+$p+700).ToString();$newP=([Numerics.BigInteger]::Parse($range.end)-100).ToString();$newQ=([Numerics.BigInteger]::Parse($range.end)+700).ToString()
function Pair([string]$P,[string]$Q){return "[phase0-gpu] GAP merit=1.9000 gap=800 lower=$P upper=$Q verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication`n"}
function Boundary([string]$First,[string]$End,[string]$P,[string]$Q,[int]$Status=1){return "[pgn-source-boundary-v2] descriptor=0 owned_first=$First owned_end=$End status=$Status terminal_p=$P terminal_q=$Q examined=1600 source_tests=2 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE`n"}
$header="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=$start length=$l proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2`n"
$oldBoundary=Boundary $start $frontier $oldP $oldQ;$boundary=Boundary $frontier $range.end $newP $newQ
$stopped=$header+(Pair $oldP $oldQ)+$oldBoundary+"[pgn-counters] time_ns=100 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1`n[phase0-gpu] stopped positions=$p checkpoint=$label`n"
$resumeHeader=$header.Replace('descriptors=2','descriptors=1')
$complete=$resumeHeader+(Pair $newP $newQ)+$boundary+"[pgn-counters] time_ns=200 positions=$l gaps=2 tests=9007199254741003 batches=2 processed_gaps=2`n[phase0-gpu] gaps reported=2 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
$bytes=Checkpoint $start $l $p.ToString();$before=[Convert]::ToBase64String($bytes);$reference=@(($oldP+"`t"+$oldQ),($newP+"`t"+$newQ))
function Verify([string]$Old=$stopped,[string]$New=$complete,[byte[]]$State=$bytes,[byte[]]$ResumeBytes=$bytes,[string[]]$Expected=$reference,[switch]$NoReference){
 Test-PgnDiagnosticResumeConservation -Checkpoint $State -StoppedSource (Utf8 $Old) -ResumeInput $ResumeBytes -ResumedSource (Utf8 $New) -Start $start -Length $l -CheckpointLabel $label -ExpectedPairs $Expected -ReferenceProvided:(!$NoReference)
}
$ok=Verify;Check ($ok.accepted -and $ok.checkpoint_positive -and $ok.resume_conserved -and $ok.pairset_matches_supplied_reference) 'real64B fixture plus exact cumulative source and pairset comparison'
Check ([Numerics.BigInteger]::Parse($oldQ) -ge [Numerics.BigInteger]::Parse($frontier) -and [Numerics.BigInteger]::Parse($oldQ) -lt [Numerics.BigInteger]::Parse($range.end)) 'internal descriptor crossing also binds to its exact first-group terminal tuple'
Check ($ok.positions_before -ceq $p.ToString() -and $ok.positions_final -ceq $l -and $ok.gaps_before -ceq '1' -and $ok.gaps_final -ceq '2') 'no double arithmetic or invented counter zero'
Check ([Convert]::ToBase64String($bytes) -ceq $before) 'pure validation never mutates checkpoint input'
$pending=Verify -NoReference;Check (!$pending.accepted -and $pending.resume_conserved -and $null -eq $pending.pairset_matches_supplied_reference -and $pending.state -ceq 'CONSERVATION_OBSERVED_REFERENCE_MISSING') 'missing oracle is explicit, never promoted by counters alone'
Held {Verify -State (Checkpoint $start $l '0')} 'zero-prefix capture is not the requested positive partial witness'
Held {Verify -Old $complete} 'job finished before STOP is not a successful stop/checkpoint test'
$changed=[byte[]]$bytes.Clone();$changed[40]=$changed[40] -bxor 1;Held {Verify -ResumeBytes $changed} 'changed resume64B baseline held'
$bad=[byte[]]$bytes.Clone();$bad[0]=0;Held {Verify -State $bad} 'checkpoint magic poison held'
Held {Verify -New $complete.Replace("positions=$l","positions=0")} 'resume cannot report a smaller/partial cursor as completion'
Held {Verify -New $complete.Replace('processed_gaps=2','processed_gaps=1')} 'processed/source gap counter disagreement held'
Held {Verify -New $complete.Replace($newP,$oldP).Replace($newQ,$oldQ)} 'overlapping/duplicate lower endpoint before paid frontier held'
Held {Verify -Expected @($oldP+"`t"+$oldQ)} 'extra observed pair versus reference held'
Held {Verify -Expected @($reference+"999`t1799")} 'omitted reference pair held'
$missing=$complete.Replace((Pair $newP $newQ),'').Replace($boundary,(Boundary $frontier $range.end 'none' 'none' 2)).Replace('gaps=2','gaps=1').Replace('gaps reported=2','gaps reported=1')
$counterOnly=Verify -New $missing -NoReference;Check (!$counterOnly.accepted -and $counterOnly.resume_conserved) 'self-consistent counters cannot establish an omitted prime/GAP witness'
Held {Verify -New $missing} 'reference independently detects self-consistent omitted GAP'
$edgeP=([Numerics.BigInteger]::Parse($range.end)-100).ToString();$edgeQ=([Numerics.BigInteger]::Parse($range.end)+700).ToString()
$edge=$complete.Replace($newP,$edgeP).Replace($newQ,$edgeQ);$edgeResult=Verify -New $edge -Expected @(($oldP+"`t"+$oldQ),($edgeP+"`t"+$edgeQ))
Check $edgeResult.accepted 'WP lower ownership permits real next q outside requested end with paid Pair terminal marker'
Held {Verify -New $edge.Replace('status=1 terminal_p=','status=2 terminal_p=')} 'NoOwnedPrime terminal cannot qualify an outside-end GAP'
Held {Verify -Old $stopped.Replace($oldBoundary,'')} 'positive prefix without terminal marker held'
Held {Verify -New $complete.Replace($boundary,'')} 'full resume without terminal marker held'
Held {Verify -New $complete.Replace('status=1 terminal_p=','status=7 terminal_p=')} 'unpaid/stopped/failed boundary status held'
Held {Verify -New $complete.Replace('source_tests=2 ','source_tests=18446744073709551616 ')} 'terminal SOURCE counter outside uint64 held'
Held {Verify -New $complete.Replace('descriptor=0 owned_first=','descriptor=1 owned_first=')} 'noncontiguous terminal descriptor held'
Held {Verify -New $complete.Replace($boundary,($boundary+$boundary))} 'duplicate terminal marker held'
Held {Verify -New $complete.Replace('descriptors=1 ','descriptors=2 ')} 'resume cannot claim an unexplored second descriptor complete'
$repeatStopped=$stopped.Replace('[phase0-gpu] stopped ',("[pgn-counters] time_ns=101 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1`n[phase0-gpu] stopped "))
Check (Verify -Old $repeatStopped).accepted 'actual STOP may repeat exact accepted tuple with newer monotonic time'
Held {Verify -Old $repeatStopped.Replace('time_ns=101 positions='+$p,'time_ns=101 positions='+($p+1))} 'repeat STOP counter cannot invent unmarked progress'
$none=$missing;Check (Verify -New $none -Expected @(($oldP+"`t"+$oldQ))).accepted 'NoOwnedPrime marker permits an empty newly accepted descriptor, no fabricated GAP'
$parsed=Read-PgnDiagnosticSource (Utf8 $complete) $start $l -InitialPositions $p.ToString()
Check ($parsed.boundaries.Count -eq 1 -and $parsed.boundaries[0].source_tests -ceq '2' -and $parsed.last.tests -eq [Numerics.BigInteger]::Parse('9007199254741003')) 'terminal probe costs remain separate from historical cumulative walk tests'
Check ($ok.acceptance_layer -ceq 'OBSERVED_SOURCE_PRP_PAIRSET_STOP_RESUME_INVARIANCE' -and $ok.boundary_schema -ceq 'pgn-source-boundary-v2') 'accepted refers only to observed SOURCEPRP differential invariance'
Held {Verify -New $complete.Replace('owned_first='+$frontier,'owned_first='+($s+$p+1))} 'shifted descriptor first cannot hide a boundary hole'
Held {Verify -New $complete.Replace('owned_end='+$range.end,'owned_end='+([Numerics.BigInteger]::Parse($range.end)+1))} 'descriptor end must be paid by exact counter prefix'
Held {Verify -New $complete.Replace('terminal_q='+$newQ,'terminal_q='+([Numerics.BigInteger]::Parse($newQ)+2))} 'out-of-end GAP must match the exact terminal tuple, not status alone'
Held {Verify -New $complete.Replace('terminal_p='+$newP,'terminal_p='+([Numerics.BigInteger]::Parse($newP)-1))} 'foreign terminal lower cannot borrow the current GAP group'
Held {Verify -New $none.Replace('terminal_p=none','terminal_p='+$newP)} 'NoOwnedPrime requires both none tokens'
Held {Verify -New $complete.Replace('owned_first='+$frontier,'owned_first=340282366920938463463374607431768211456')} 'decimal128 overflow held, not low64 truncation'
Held {Verify -New $complete.Replace('owned_first='+$frontier,'owned_first=0'+$frontier)} 'noncanonical coordinate held'
Held {Verify -New $complete.Replace((Pair $newP $newQ),'')} 'qualifying terminal Pair must have its exact GAP row'
$v1="[pgn-source-boundary] descriptor=0 status=1 examined=1600 source_tests=2 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE`n";$legacy=$complete.Replace($boundary,$v1).Replace(' boundary_protocol=pgn-source-boundary-v2','')
Held {Verify -New $legacy} 'legacy v1 cannot waive new v2 qualification'
$historical=Read-PgnDiagnosticSource (Utf8 $legacy) $start $l -InitialPositions $p.ToString() -HistoricalV1
Check ($historical.historical_only -and $historical.boundary_schema -ceq 'HISTORICAL_V1_NOT_QUALIFIED') 'old v1 remains explicitly readable as historical evidence only'
Held {Read-PgnDiagnosticSource (Utf8 $complete) $start $l -InitialPositions $p.ToString() -HistoricalV1} 'historical path does not silently mix v2 markers'
Held {Verify -New $complete.Replace(' boundary_protocol=pgn-source-boundary-v2','')} 'v2 marker cannot repair a missing producer protocol declaration'
$zero=$header+"[pgn-counters] time_ns=100 positions=0 gaps=0 tests=0 batches=0 processed_gaps=0`n[phase0-gpu] stopped positions=0 checkpoint=$label`n"
$zeroParsed=Read-PgnDiagnosticSource (Utf8 $zero) $start $l
Check ($zeroParsed.boundaries.Count -eq 0 -and $zeroParsed.last.positions -eq 0 -and $zeroParsed.boundary_schema -ceq 'pgn-source-boundary-v2') 'declared v2 zero-prefix STOP conserves the actual zero tuple without a fabricated marker/progress'
Held {Read-PgnDiagnosticSource (Utf8 $zero.Replace(' boundary_protocol=pgn-source-boundary-v2','')) $start $l} 'zero-prefix protocol cannot be inferred from absent markers'
Held {Verify -Old $zero -State (Checkpoint $start $l '0')} 'valid declared zero STOP is still insufficient for the positive hardware witness'
$noResumeProgress=$resumeHeader+"[pgn-counters] time_ns=200 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1`n[pgn-counters] time_ns=201 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1`n[phase0-gpu] stopped positions=$p checkpoint=$label`n"
$noProgress=Read-PgnDiagnosticSource (Utf8 $noResumeProgress) $start $l -InitialPositions $p.ToString()
Check ($noProgress.boundaries.Count -eq 0 -and $noProgress.last.positions -eq $p) 'resumed STOP may repeat its real saved prefix with zero newly paid groups'
Held {Read-PgnDiagnosticSource (Utf8 $stopped.Replace("[phase0-gpu] stopped positions=$p checkpoint=$label","[phase0-gpu] gaps reported=1 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---")) $start $l} 'one of two groups cannot be relabelled full completion'
Held {Read-PgnDiagnosticSource (Utf8 ($complete+'late data')) $start $l} 'partial/unpaid tail held'
Held {Read-PgnDiagnosticCheckpoint ($bytes+0) $start $l} 'extra checkpoint bytes held'
$bom=[byte[]](@(0xef,0xbb,0xbf)+(Utf8 $complete));Held {Read-PgnDiagnosticSource $bom $start $l} 'literal BOM rejected ordinally, never silently normalized'
$badUtf8=Utf8 $complete;$badUtf8[0]=255;Held {Read-PgnDiagnosticSource $badUtf8 $start $l} 'invalid SOURCE UTF8 held'
Held {Get-PgnDiagnosticTwoDescriptorRange '199932000019059617039.0'} 'noncanonical numeric input held'
$null=Test-PgnDiagnosticCompletedArchive $bytes $bytes (Utf8 $complete) $start $l
Check $true 'unchanged input can legitimately be the completed working archive'
$updated=[byte[]]$bytes.Clone();$updatedWords=@([uint64]::Parse($l),[uint64]9007199254741003,[uint64]2,[uint64]2);for($i=0;$i -lt 4;$i++){[Array]::Copy([BitConverter]::GetBytes($updatedWords[$i]),0,$updated,32+8*$i,8)}
$null=Test-PgnDiagnosticCompletedArchive $bytes $updated (Utf8 $complete) $start $l;Check $true 'periodic save may archive full final tuple, never confuse it with immutable resume input'
$updated[40]=$updated[40] -bxor 1;Held {Test-PgnDiagnosticCompletedArchive $bytes $updated (Utf8 $complete) $start $l} 'changed completed archive counters held'
$fullReference=$stopped.Replace("[phase0-gpu] stopped positions=$p checkpoint=$label`n",'')+$complete.Substring($resumeHeader.Length).Replace('descriptor=0 owned_first=','descriptor=1 owned_first=')
$fullBefore=[Convert]::ToBase64String((Utf8 $fullReference));$observed=Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference) $start $l
Check ($observed.reference_ready -and !$observed.accepted -and !$observed.stop_resume_accepted -and !$observed.independent_coverage -and $observed.state -ceq 'SOURCE_PRP_UNINTERRUPTED_REFERENCE_ONLY') 'uninterrupted reference is useful SOURCE output, never stop/resume or independent coverage acceptance'
Check ($observed.positions -ceq $l -and $observed.batches -ceq '2' -and $observed.boundary_groups -ceq '2' -and $observed.gaps -ceq '2') 'reference requires full original range and both actual paid groups'
Check ($observed.pairs_text -ceq ($oldP+"`t"+$oldQ+"`n"+$newP+"`t"+$newQ+"`n") -and $observed.pair_count -ceq '2') 'canonical tab-separated reference uses exact large integers and final LF'
Check ([Convert]::ToBase64String((Utf8 $fullReference)) -ceq $fullBefore) 'reference creation never rewrites the journal supplied to it'
$insideP=($s+$p+10).ToString();$insideQ=($s+$p+810).ToString()
$insideBP=($s+$p+1610).ToString();$insideBQ=($s+$p+2410).ToString()
$unsorted=$fullReference.Replace((Pair $newP $newQ),((Pair $newP $newQ)+(Pair $insideBP $insideBQ)+(Pair $insideP $insideQ))).Replace('gaps=2','gaps=4').Replace('gaps reported=2','gaps reported=4')
$sorted=Get-PgnDiagnosticUninterruptedReference (Utf8 $unsorted) $start $l
Check ($sorted.pairs_text -ceq ($oldP+"`t"+$oldQ+"`n"+$insideP+"`t"+$insideQ+"`n"+$insideBP+"`t"+$insideBQ+"`n"+$newP+"`t"+$newQ+"`n")) 'unordered GPU rows within one double ULP produce exact numeric order without journal reordering'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $stopped) $start $l} 'positive partial prefix is not an uninterrupted reference'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $complete) $start $l} 'a resumed one-descriptor footer is not a fresh two-descriptor reference'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace('owned_end='+$range.end,'owned_end='+([Numerics.BigInteger]::Parse($range.end)+1))) $start $l} 'reference completed under wrong descriptor bounds held'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace((Pair $newP $newQ),((Pair $newP $newQ)+(Pair $newP $newQ)))) $start $l} 'duplicate reference pair held even if counters look complete'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace('[phase0-gpu] --- end ---','')) $start $l} 'reference without exact terminal footer held'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace($boundary.Replace('descriptor=0 ','descriptor=1 '),'')) $start $l} 'reference missing one boundary marker held'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace("time_ns=100 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1","time_ns=100 positions=$p gaps=2 tests=9007199254740993 batches=1 processed_gaps=2")) $start $l} 'first reference counter cannot include an unseen baseline GAP'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace("time_ns=100 positions=$p gaps=1 tests=9007199254740993 batches=1 processed_gaps=1","time_ns=100 positions=$p gaps=1 tests=9007199254740993 batches=2 processed_gaps=1")) $start $l} 'reference fresh epoch cannot borrow an old cumulative batch'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace('gaps=2','gaps=3').Replace('gaps reported=2','gaps reported=3')) $start $l} 'reference final GAP counter equals actual entire observed set'
Held {Get-PgnDiagnosticUninterruptedReference (Utf8 $fullReference.Replace(' boundary_protocol=pgn-source-boundary-v2','')) $start $l} 'historical grammar is never relabelled as a v2 reference'
$emptyReference=$fullReference.Replace((Pair $oldP $oldQ),'').Replace((Pair $newP $newQ),'').Replace($oldBoundary,(Boundary $start $frontier 'none' 'none' 2)).Replace($boundary.Replace('descriptor=0 ','descriptor=1 '),(Boundary $frontier $range.end 'none' 'none' 2).Replace('descriptor=0 ','descriptor=1 ')).Replace('gaps=1','gaps=0').Replace('gaps=2','gaps=0').Replace('gaps reported=2','gaps reported=0')
$emptyObserved=Get-PgnDiagnosticUninterruptedReference (Utf8 $emptyReference) $start $l
Check ($emptyObserved.pair_count -ceq '0' -and $emptyObserved.pairs_text -ceq '' -and $emptyObserved.pairs_bytes -eq 0 -and $emptyObserved.reference_ready) 'actual empty output has an empty canonical reference, no invented GAP/zero fallback'
$runner=Join-Path $PSScriptRoot '../tools/test-terminal-boundary-stop-resume.ps1';$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$null,[ref]$errors)
Check (!$errors) 'maintained runner parses without executing any hardware/host path'
$gapCopy=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'ExactGapCopy'},$true))
Check ($gapCopy.Count -eq 1) 'use the actual unique pure stdout-gaplog verifier, not a test facsimile'
. ([scriptblock]::Create($gapCopy[0].Extent.Text))
ExactGapCopy (Utf8 $fullReference) (Utf8 ((Pair $oldP $oldQ)+(Pair $newP $newQ)));Check $true 'actual runner verifies every reference GAP row against its unchanged gaplog'
Held {ExactGapCopy (Utf8 $fullReference) (Utf8 (Pair $oldP $oldQ))} 'dropped gaplog row prevents reference publication'
Held {ExactGapCopy (Utf8 $fullReference) (Utf8 ((Pair $oldP $oldQ)+(Pair $newP $newQ)+(Pair $newP $newQ)))} 'duplicated gaplog row prevents reference publication'
$forbidden=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -in @('Stop-Process','Invoke-Command','Enter-PSSession','ssh','nvcc')},$true))
Check ($forbidden.Count -eq 0) 'runner contains no kill/remote/compile command'
$unarmed=& $runner|ConvertFrom-Json;Check ($unarmed.state -ceq 'NOT_ARMED' -and $unarmed.gpu_launches -eq 0) 'no execute flag returns before imports/host paths/driver/process/locks'
$referenceUnarmed=& $runner -ReferenceOnly|ConvertFrom-Json;Check ($referenceUnarmed.state -ceq 'NOT_ARMED' -and $referenceUnarmed.gpu_launches -eq 0) 'ReferenceOnly does not implicitly arm the hardware'
Held {& $runner -ReferenceOnly -ReferencePairs 'PUBLIC-TESTONLY-NOT-READ.tsv'} 'ReferenceOnly and imported reference are rejected before any host operation'
Held {& $runner -ReferenceOnly -ReferenceSha256 ('A'*64)} 'ReferenceOnly cannot carry an ambiguous external reference hash'
Held {& $runner -ReferenceOnly -AllowReferencePending} 'ReferenceOnly cannot ask to qualify stop/resume without its reference'
$runnerText=[IO.File]::ReadAllText($runner)
$unassigned=$runnerText.Contains('$image=''''') -and $runnerText.Contains('$imagePin=''''') -and $runnerText.Contains('$imageBytes=0')
$assigned=$runnerText.Contains('$image=''C:/PGN/diagnostics/root555-terminal-boundary-20261015-v2/pgn-terminal-boundary.exe''') -and $runnerText.Contains('$imagePin=''74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B''') -and $runnerText.Contains('$imageBytes=644096')
Check (($unassigned -or $assigned) -and $runnerText.IndexOf('V2_CANDIDATE_PIN_UNASSIGNED_NO_LAUNCH') -lt $runnerText.IndexOf("Explicit finite prefix/resume")) 'v2 runner is unassigned or has the exact root diagnostic build/readback pin; no arbitrary override or historical D500 admission'
Check ($runnerText.Contains('SAME_IMAGE_PIN_CHANGED_BEFORE_LAUNCH') -and $runnerText.Contains('if(!(AssertOwn $epoch))') -and $runnerText.Contains('SAME_IMAGE_PIN_CHANGED_AFTER_EXIT')) 'uninterrupted mode does not borrow OWN identity or same-image checks from a STOP that never happened'
Check (!$runnerText.Contains('GetFullPath($p.Path)') -and $runnerText.Contains('Wait-PgnDiagnosticOwnIdentity') -and $runnerText.Contains('CollectExitReceipt $epoch')) 'startup identity is observed boundedly and finally collects actual exit independent of admission'
# Extract actual observational writer only, never LaunchEpoch/AssertSlot or a
# diagnostic invocation. All paths passed below are our own private fixture.
foreach($name in @('Regular','NewBytes','NewJson','ReadHeld','CollectExitReceipt')){
 $defs=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true));Check ($defs.Count -eq 1) ('unique actual helper '+$name);. ([scriptblock]::Create($defs[0].Extent.Text))
}
Import-Module (Join-Path $PSScriptRoot '../tools/diagnostic-child-process.psm1') -Force
Check ($runnerText.Contains((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../tools/diagnostic-child-process.psm1') -Algorithm SHA256).Hash)) 'runner requires the exact delivered identity/exit reader, never an unpinned substitute'
# Real existing capture helper, private files only: file acquisition is not
# synthetic ownership proof. Owned=false below is explicitly TESTONLY input.
Import-Module (Join-Path $PSScriptRoot '../tools/source-stop-checkpoint-evidence.psm1') -Force
$temp=Join-Path ([IO.Path]::GetTempPath()) ('pgn-stop-resume-pure-'+[guid]::NewGuid().ToString('N'));$null=[IO.Directory]::CreateDirectory((Join-Path $temp 'current-job'));$run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';$localLabel=Join-Path $temp 'current-job/checkpoint';$fixturePassed=$false
$quiet=$null;$early=$null
try{
 $manifest=[pscustomobject]@{schema='pgn-native-process-1';run=$run;start=$start;end=$range.end;stdout=(Join-Path $temp ('current-job/'+$run+'.stdout'));stop=(Join-Path $temp 'current-job/stop.request')};$exit=[pscustomobject]@{run=$run;exit_code=0}
 [IO.File]::WriteAllBytes($localLabel,$bytes);[IO.File]::WriteAllBytes($manifest.stdout,(Utf8 $stopped.Replace($label,$localLabel)))
 [IO.File]::WriteAllText((Join-Path $temp 'process.json'),($manifest|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $temp 'last-exit.json'),($exit|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
 $capture=Get-PgnStoppedCheckpointEvidence -Root $temp -Manifest $manifest -LastExit $exit -Owned $false -ExpectedRun $run
 Check ($capture.status -ceq 'captured' -and $capture.checkpoint_base64 -ceq $before) 'production capture reads actual private held64B/source files, not a copied DTO'
 Check ($capture.source_tail_utf8 -ceq $stopped.Replace($label,$localLabel) -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($localLabel)) -ceq $before) 'fresh capture preserves exact source bytes/label/checkpoint, no journal rewrite'
 $heldCapture=Get-PgnStoppedCheckpointEvidence -Root $temp -Manifest $manifest -LastExit $exit -Owned $null -ExpectedRun $run;Check ($heldCapture.status -ceq 'unavailable') 'unknown ownership is never captured/stopped'
 # Real quiet bounded Windows children. They execute only a short managed
 # wait/exit, never a miner, GPU/driver/control command, network or user data.
 $windowsPs='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe'
 $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('[Threading.Thread]::Sleep(1500); exit 0'))
 $quiet=Start-Process -FilePath $windowsPs -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$encoded) -WindowStyle Hidden -PassThru;$null=$quiet.Handle
 $observed=Wait-PgnDiagnosticOwnIdentity -Process $quiet -ExpectedImage $windowsPs
 Check ($observed.status -ceq 'Observed' -and $observed.pid -eq $quiet.Id -and $observed.source -ceq 'RETAINED_PROCESS_HANDLE_OS_OBSERVATION') 'actual retained handle pays OS PID/birth/full image, not Process.Path cache or expected substitution'
 Check ($null -eq (Get-PgnDiagnosticChildExit -Process $quiet -Run $run)) 'a running quiet child has no invented exit code'
 $module=Get-Module diagnostic-child-process
 $invalidPaths=&$module { @('',' ','not:a:valid|path','C:/bad|path.exe')|ForEach-Object { $null -eq (DiagnosticObservedPath $_) } }
 Check (@($invalidPaths|Where-Object{!$_}).Count -eq 0) 'empty/nonabsolute/invalid observed paths remain unknown before GetFullPath'
 $delayed=&$module {param($process,$expected)
  $handle=$process.Handle;$state=@{calls=0};$observe={++$state.calls;$r=[PgnDiagnosticChildNativeV1]::Read($handle);if($state.calls -eq 1){$r.Image=''};return $r}.GetNewClosure()
  $ended={$process.HasExited}.GetNewClosure();$wait={param($ms) $process.WaitForExit($ms)}.GetNewClosure()
  WaitDiagnosticIdentity $observe $ended $wait $process.Id $expected '' 1000
 } $quiet $windowsPs
 Check ($delayed.status -ceq 'Observed' -and $delayed.attempts -ge 2 -and $delayed.image -ieq [IO.Path]::GetFullPath($windowsPs)) 'initial empty path waits for a subsequent actual OS path, never GetFullPath(empty) or blind CUDA sleep'
 $unknown=&$module {param($process,$expected)
  $handle=$process.Handle;$observe={$r=[PgnDiagnosticChildNativeV1]::Read($handle);$r.Image='';return $r}.GetNewClosure();$ended={$process.HasExited}.GetNewClosure();$wait={param($ms) $process.WaitForExit($ms)}.GetNewClosure()
  WaitDiagnosticIdentity $observe $ended $wait $process.Id $expected '' 20
 } $quiet $windowsPs
 Check ($unknown.status -ceq 'Held' -and $unknown.error -ceq 'OWN_IDENTITY_UNAVAILABLE_AT_DEADLINE') 'unobserved path remains Held at a finite deadline, never expected-image fallback'
 $mismatch=Wait-PgnDiagnosticOwnIdentity -Process $quiet -ExpectedImage 'C:/PUBLIC-TESTONLY-NOT-OWNER.exe'
 Check ($mismatch.status -ceq 'Held' -and $mismatch.error -ceq 'OWN_BIRTH_OR_IMAGE_MISMATCH') 'actual foreign image cannot be admitted'
 $birthMismatch=Wait-PgnDiagnosticOwnIdentity -Process $quiet -ExpectedImage $windowsPs -ExpectedBirth '2000-01-01T00:00:00.0000000Z'
 Check ($birthMismatch.status -ceq 'Held') 'observed birth mismatch cannot target another epoch'
 Check ($quiet.WaitForExit(5000)) 'quiet child ends itself inside its own finite test budget'
 $quietRoot=Join-Path $temp 'quiet';$null=[IO.Directory]::CreateDirectory($quietRoot)
 $quietEpoch=[pscustomobject]@{root=$quietRoot;process=$quiet;manifest=[pscustomobject]@{run=('b'*32);pid=$quiet.Id;birth=$observed.birth};exitReceipt=$null}
 $quietExit=CollectExitReceipt $quietEpoch
 Check ($quietExit.exit_code -is [int] -and $quietExit.exit_code -eq 0 -and $quietExit.pid -eq $quiet.Id) 'actual quiet exit0 also conserved as a physical observation, not admission/completion acceptance'
 $encodedExit=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes('exit 7'))
 $early=Start-Process -FilePath $windowsPs -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$encodedExit) -WindowStyle Hidden -PassThru;$null=$early.Handle
 Check ($early.WaitForExit(5000)) 'very short OWN child exits itself, no stop/kill'
 $earlyHeld=&$module {param($process,$expected)
  $handle=$process.Handle;$observe={$r=[PgnDiagnosticChildNativeV1]::Read($handle);$r.Image='';return $r}.GetNewClosure();$ended={$process.HasExited}.GetNewClosure();$wait={param($ms) $process.WaitForExit($ms)}.GetNewClosure()
  WaitDiagnosticIdentity $observe $ended $wait $process.Id $expected '' 1000
 } $early $windowsPs
 Check ($earlyHeld.status -ceq 'Held' -and $earlyHeld.error -ceq 'OWN_EXITED_IDENTITY_UNOBSERVED') 'early exit with unknown startup identity is Held, not silently admitted'
 $earlyRoot=Join-Path $temp 'early';$null=[IO.Directory]::CreateDirectory($earlyRoot)
 $failedAdmission=[pscustomobject]@{root=$earlyRoot;process=$early;manifest=[pscustomobject]@{run=$run;pid=$early.Id;birth=$null};exitReceipt=$null}
 $actualExit=CollectExitReceipt $failedAdmission
 Check ($actualExit.exit_code -is [int] -and $actualExit.exit_code -eq 7 -and $actualExit.pid -eq $early.Id) 'actual exit7 conserved even when admission failed before process.json; never fake zero/launcher exit2'
 Check (!(Test-Path -LiteralPath (Join-Path $earlyRoot 'process.json')) -and (Test-Path -LiteralPath (Join-Path $earlyRoot 'last-exit.json'))) 'exit collection is independent of successful process manifest admission'
 $exitPath=Join-Path $earlyRoot 'last-exit.json';$exitHash=(Get-FileHash -LiteralPath $exitPath -Algorithm SHA256).Hash
 $again=CollectExitReceipt $failedAdmission;$failedAdmission.exitReceipt=$null;$reopened=CollectExitReceipt $failedAdmission
 Check ($again.exit_code -eq 7 -and $reopened.exit_code -eq 7 -and (Get-FileHash -LiteralPath $exitPath -Algorithm SHA256).Hash -ceq $exitHash) 'actual exit receipt is idempotent/cached/reopened without overwrite'
 $fixturePassed=$true
}finally{
 foreach($ownChild in @($quiet,$early)){if($ownChild -and !$ownChild.HasExited){if(!$ownChild.WaitForExit(5000)){$fixturePassed=$false;[Console]::Error.WriteLine('OWN quiet child still running, preserved; no kill: '+$ownChild.Id)}}}
 if($fixturePassed){$resolved=[IO.Path]::GetFullPath($temp);$parent=[IO.Path]::GetDirectoryName($resolved);$expectedParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]@('/', '\'));if($parent -ine $expectedParent -or [IO.Path]::GetFileName($resolved) -cnotmatch '\Apgn-stop-resume-pure-[0-9a-f]{32}\z'){throw 'Private cleanup target guard'};Remove-Item -LiteralPath $resolved -Recurse -Force}else{[Console]::Error.WriteLine('Failed private fixture preserved: '+$temp)}
}
"SOURCE_STOP_RESUME_DIAGNOSTIC $script:checks checks / 0 failures; private files + bounded OWN quiet Windows children, no miner/GPU/remote/production DB, no independent prime coverage"
