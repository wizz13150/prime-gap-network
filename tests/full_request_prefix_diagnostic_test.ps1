$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/source-stop-resume-diagnostic.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/full-request-prefix-diagnostic.psm1') -Force
$script:checks=0
function Check($Ok,[string]$Why){++$script:checks;if(!$Ok){throw $Why}}
function Held([scriptblock]$Code,[string]$Why){$failed=$false;try{$null=&$Code}catch{$failed=$true};Check $failed $Why}
function Bytes([string]$Text){return ,([Text.UTF8Encoding]::new($false).GetBytes($Text))}
$start='199932000000000000000';$length='1000000000000000';$s=[Numerics.BigInteger]::Parse($start);$fresh=[pscustomobject]@{positions='0';tests='0';gaps='0';batches='0';time_ns='0'};$oldLabel='C:/PUBLIC-TESTONLY/first/checkpoint';$newLabel='C:/PUBLIC-TESTONLY/second/checkpoint'
$geometry=Get-PgnFullRequestGeometry $start $length;$width=[Numerics.BigInteger]::Parse($geometry.width);$rem=[Numerics.BigInteger]::Parse($geometry.remainder)
function Counter($c){return "[pgn-counters] time_ns=$($c.time_ns) positions=$($c.positions) gaps=$($c.gaps) tests=$($c.tests) batches=$($c.batches) processed_gaps=$($c.gaps)`n"}
function Pair([string]$P,[string]$Q){return "[phase0-gpu] GAP merit=1.9000 gap=800 lower=$P upper=$Q verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown time_basis=source_batch_publication time_status=unavailable`n"}
function Checkpoint($c){
 $raw=New-Object byte[] 64;[Array]::Copy([Text.Encoding]::ASCII.GetBytes('PGNCG001'),$raw,8);$word=[Numerics.BigInteger]::Pow(2,64)
 $words=@([uint64]($s%$word),[uint64]10,[uint64]::Parse($length),[uint64]::Parse($c.positions.ToString()),[uint64]::Parse($c.tests.ToString()),[uint64]::Parse($c.gaps.ToString()),[uint64]::Parse($c.batches.ToString()))
 for($i=0;$i -lt 7;$i++){[Array]::Copy([BitConverter]::GetBytes($words[$i]),0,$raw,8+8*$i,8)};return ,$raw
}
function Frame($Base,[int]$Groups,[string]$Label,[switch]$Complete,[string[]]$ExtraFirst,[string[]]$ExtraThird){
 $g=Get-PgnFullRequestGeometry $start $length $Base.positions;$initial=[Numerics.BigInteger]::Parse($Base.positions);$remaining=[Numerics.BigInteger]::Parse($g.remaining);$remainder=[Numerics.BigInteger]::Parse($g.remainder)
 $c=@{};foreach($name in @('positions','tests','gaps','batches','time_ns')){$c[$name]=[Numerics.BigInteger]::Parse($Base.$name)}
 $text="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=$($g.count) original_start=$start length=$length proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2`n"
 for($i=0;$i -lt $Groups;$i++){
  $first=$s+$c.positions;$position=$initial+[Numerics.BigInteger]::Min($remaining,($i+1)*$width-$remainder);$end=$s+$position;$p=($end-100).ToString();$q=($end+700).ToString()
  if($i -eq 0 -and $ExtraFirst){foreach($pairLine in $ExtraFirst){$text+=$pairLine;$c.gaps+=1}}
  if($i -eq 2 -and $ExtraThird){foreach($pairLine in $ExtraThird){$text+=$pairLine;$c.gaps+=1}}
  $text+=(Pair $p $q)+"[pgn-source-boundary-v2] descriptor=$i owned_first=$first owned_end=$end status=1 terminal_p=$p terminal_q=$q examined=1600 source_tests=2 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE`n"
  $c.positions=$position;$c.tests+=[Numerics.BigInteger]::Parse('9007199254740993');$c.gaps+=1;$c.batches+=1;$c.time_ns+=10;$text+=Counter $c
 }
 if($Complete){$text+="[phase0-gpu] gaps reported=$($c.gaps) verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"}
 else{$c.time_ns+=1;$text+=(Counter $c)+"[phase0-gpu] stopped positions=$($c.positions) checkpoint=$Label`n"}
 [pscustomobject]@{text=$text;bytes=(Bytes $text);last=$c;checkpoint=(Checkpoint $c)}
}
Check ($geometry.count -ceq '15523' -and $geometry.end -ceq '199933000000000000000') 'actual full1e15 analytical descriptor count'
Check ($geometry.last_owned_length -ne $geometry.width -and [Numerics.BigInteger]::Parse($geometry.last_owned_length) -gt 0) 'last analytical descriptor is clipped, not an invented full descriptor'
$floor=Get-PgnFullRequestMemoryFloor $geometry.count
Check ($floor.bitmap_bytes -ceq '4362076160' -and $floor.device_result_bytes -ceq '1017315328' -and $floor.device_proof_bytes -ceq '254328832') 'source-pinned major allocation geometry, not small-run scaling guess'
Check ($floor.source_device_guard_floor -ceq '5767938048' -and $floor.mapped_host_known_bytes -ceq '1018246716') 'exact engine source guard and known mapped host lower bound'
$extra=@();$windowStart='199932000019059617039';$windowEnd='199932000147908635890'
for($i=0;$i -lt 18;$i++){$p=([Numerics.BigInteger]::Parse($windowStart)+100+1000*$i).ToString();$q=([Numerics.BigInteger]::Parse($p)+800).ToString();$extra+=Pair $p $q}
$thirdP=($s+[Numerics.BigInteger]::Parse('130000000000')).ToString();$thirdQ=([Numerics.BigInteger]::Parse($thirdP)+800).ToString();$thirdPair=Pair $thirdP $thirdQ
$a=Frame $fresh 3 $oldLabel -ExtraFirst $extra -ExtraThird @($thirdPair);$first=Test-PgnFullRequestStopped $a.checkpoint $a.bytes $start $length $oldLabel $fresh 3
$base=[pscustomobject]@{};foreach($name in @('positions','tests','gaps','batches','time_ns')){$base|Add-Member $name $a.last[$name].ToString()}
$b=Frame $base 1 $newLabel
$before=[Convert]::ToBase64String($a.bytes);$cpBefore=[Convert]::ToBase64String($a.checkpoint)
$proof=Test-PgnFullRequestStopResume $a.checkpoint $a.bytes $a.checkpoint $b.checkpoint $b.bytes $start $length $oldLabel $newLabel
Check ($proof.accepted -and $proof.first_groups -eq 3 -and $proof.resumed_new_groups -eq 1 -and $proof.request_descriptor_count -ceq '15523') 'three paid descriptors traverse slot0 reuse then one genuinely new resumed group'
Check ($proof.scope -ceq 'FULL_REQUEST_GEOMETRY_AND_OBSERVED_PREFIX_STOP_RESUME_ONLY' -and !$proof.source_range_complete -and !$proof.independent_coverage -and !$proof.performance_qualified -and !$proof.all_source_reference -and !$proof.analytical_last_physically_observed) 'count/lastclip analytical proof is not physical last-tail/fullrange/prime/performance proof'
Check ([Convert]::ToBase64String($a.bytes) -ceq $before -and [Convert]::ToBase64String($a.checkpoint) -ceq $cpBefore) 'pure validator never mutates original SOURCE/checkpoint'
Check ((Get-PgnFullRequestGeometry $start $length $base.positions).count -ceq '15520') 'resumed header Count reflects actual remaining request, not original Count'
$race=Frame $fresh 4 $oldLabel -ExtraFirst $extra -ExtraThird @($thirdPair);$raceBase=[pscustomobject]@{};foreach($name in @('positions','tests','gaps','batches','time_ns')){$raceBase|Add-Member $name $race.last[$name].ToString()};$raceNext=Frame $raceBase 2 $newLabel
Check (Test-PgnFullRequestStopResume $race.checkpoint $race.bytes $race.checkpoint $raceNext.checkpoint $raceNext.bytes $start $length $oldLabel $newLabel).accepted 'STOP may race with additional fully paid groups; never discard them or insist on exact3/1'
$two=Frame $fresh 2 $oldLabel;Held {Test-PgnFullRequestStopped $two.checkpoint $two.bytes $start $length $oldLabel $fresh 3} 'two descriptors do not exercise slot0 reuse'
$drain=Frame $base 0 $newLabel;Held {Test-PgnFullRequestStopped $drain.checkpoint $drain.bytes $start $length $newLabel $base 1} 'resumed baseline-only STOP cannot qualify one new group'
Held {Test-PgnFullRequestStopResume $a.checkpoint $a.bytes ($a.checkpoint+0) $b.checkpoint $b.bytes $start $length $oldLabel $newLabel} 'resume input must retain exactly captured64B'
$badCp=[byte[]]$b.checkpoint.Clone();$badCp[40]=$badCp[40] -bxor 1;Held {Test-PgnFullRequestStopResume $a.checkpoint $a.bytes $a.checkpoint $badCp $b.bytes $start $length $oldLabel $newLabel} 'actual resumed checkpoint/counters mismatch held'
Held {Read-PgnFullRequestSource (Bytes $b.text.Replace('descriptors=15520 ','descriptors=15523 ')) $start $length $base} 'stale original header Count on resumed epoch held'
Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('descriptor=2 ','descriptor=0 ')) $start $length $fresh} 'descriptor2 cannot silently reuse local publication identity0'
$end1=($s+$width-$rem).ToString();Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('owned_first='+$end1,'owned_first='+([Numerics.BigInteger]::Parse($end1)+1))) $start $length $fresh} 'hole between adjacent paid owned frontiers held'
Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('source_tests=2 ','source_tests=N/A ')) $start $length $fresh} 'unknown terminal evidence is not zero'
$markerRows=@($a.text.Split([char]10)|Where-Object{$_.StartsWith('[pgn-source-boundary-v2]')});Held {Read-PgnFullRequestSource (Bytes $a.text.Replace($markerRows[2]+"`n",'')) $start $length $fresh} 'third accepted group cannot lose its terminal marker'
$extraDrain=(Counter $a.last).Replace('tests='+$a.last.tests,'tests='+($a.last.tests+1));Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('[phase0-gpu] stopped ',($extraDrain+'[phase0-gpu] stopped '))) $start $length $fresh} 'drain cannot increment a counter without a newly paid group'
$late=Pair ($s+10).ToString() ($s+810).ToString();Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('[phase0-gpu] stopped ',($late+'[phase0-gpu] stopped '))) $start $length $fresh} 'extra unpaid GAP after last accepted counter held'
Held {Read-PgnFullRequestSource (Bytes $b.text.Replace('terminal_q=',"terminal_q=0")) $start $length $base} 'noncanonical terminal tuple cannot be mistaken for owned prefix'
Held {Read-PgnFullRequestSource (Bytes $a.text.Replace('boundary_protocol=pgn-source-boundary-v2','boundary_protocol=pgn-source-boundary-v1')) $start $length $fresh} 'new diagnostic has no legacy grammar waiver'
Held {Read-PgnFullRequestSource $a.bytes $start $length $null} 'missing baseline never defaults unknown counters to zero'
$lastBase=[pscustomobject]@{positions=(([Numerics.BigInteger]::Parse($geometry.count)-1)*$width-$rem).ToString();tests='9007199254740993';gaps='123';batches='15522';time_ns='1000'}
$tail=Frame $lastBase 1 $newLabel -Complete;$tailParsed=Read-PgnFullRequestSource $tail.bytes $start $length $lastBase
Check ($tailParsed.geometry.count -ceq '1' -and $tailParsed.terminal -ceq 'complete' -and $tailParsed.groups[0].end -ceq $geometry.end -and $tailParsed.last.positions -eq [Numerics.BigInteger]::Parse($length)) 'last clipped descriptor grammar is paid analytically in closed fixture only'
Held {Test-PgnFullRequestStopped $tail.checkpoint $tail.bytes $start $length $newLabel $lastBase 1} 'completed request is not partial-stop/resume qualification'
Held {Read-PgnFullRequestSource (Bytes $tail.text.Replace('owned_end='+$geometry.end,'owned_end='+([Numerics.BigInteger]::Parse($geometry.end)+1))) $start $length $lastBase} 'full-tail clip cannot extend requested ownership'
$keys=New-Object 'System.Collections.Generic.List[System.Numerics.BigInteger]';foreach($key in $first.source.pairs.Keys){$n=[Numerics.BigInteger]::Parse($key);if($n -ge [Numerics.BigInteger]::Parse($windowStart) -and $n -lt [Numerics.BigInteger]::Parse($windowEnd)){$keys.Add($n)}};$keys.Sort();$ref='';foreach($key in $keys){$ref+=$key.ToString()+"`t"+$first.source.pairs[$key.ToString()]+"`n"}
$window=Test-PgnFullRequestKnownWindow $first (Bytes $ref) $windowStart $windowEnd 21
Check ($window.known_window_match -and $window.pairs -eq 21) 'all21 known lower-owned pairs match despite different request partition, not other unreferenced points'
$omitted=$a.text.Replace($thirdPair,'').Replace('gaps=22','gaps=21');$selfConsistent=[pscustomobject]@{source=(Read-PgnFullRequestSource (Bytes $omitted) $start $length $fresh)}
Held {Test-PgnFullRequestKnownWindow $selfConsistent (Bytes $ref) $windowStart $windowEnd 21} 'known-window guard catches omitted slot0-reuse group point even with self-consistent counters'
Held {Test-PgnFullRequestKnownWindow $first (Bytes ($ref.Split([char]10)[0]+"`n")) $windowStart $windowEnd 21} 'incomplete known reference cannot qualify a bigger observed window'
$firstRef=$ref.Split([char]10)[0];$refFields=$firstRef.Split([char]9);$changedRef=$ref.Replace($firstRef,($refFields[0]+"`t"+([Numerics.BigInteger]::Parse($refFields[1])+1).ToString()))
Held {Test-PgnFullRequestKnownWindow $first (Bytes $changedRef) $windowStart $windowEnd 21} 'changed known pair/reference mismatch held'
$one=Frame $fresh 1 $oldLabel;$oneParsed=[pscustomobject]@{source=(Read-PgnFullRequestSource $one.bytes $start $length $fresh)};Held {Test-PgnFullRequestKnownWindow $oneParsed (Bytes $ref) $windowStart $windowEnd 21} 'known window must be wholly inside actual paid prefix'
$res=Test-PgnFullRequestResources '15523' '6841679872' '2091988540' '12367953920' '34530529280'
Check ($res.scope -ceq 'FRESH_RESOURCE_THRESHOLDS_ONLY_NOT_ALLOCATION_GUARANTEE') 'root thresholds paid without pretending snapshots reserve allocation'
Held {Test-PgnFullRequestResources '15523' '6841679872' '2091988540' 'N/A' '34530529280'} 'N/A VRAM stays unknown'
Held {Test-PgnFullRequestResources '15523' '6841679872' '2091988540' '12367953920' ''} 'absent RAM stays unknown'
Held {Test-PgnFullRequestResources '15523' '6841679872' '2091988540' '6841679871' '34530529280'} 'one-byte-under device threshold refuses launch'
Held {Test-PgnFullRequestResources '15523' '6841679872' '2091988540' '12367953920' '2091988539'} 'one-byte-under host threshold refuses launch'
Held {Test-PgnFullRequestResources '15523' '5767938048' '2091988540' '12367953920' '34530529280'} 'known source minimum alone does not pay root context/allocation margin'
$runner=Join-Path $PSScriptRoot '../tools/test-full-request-prefix-stop-resume.ps1';$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$null,[ref]$errors);Check (!$errors) 'new runner parses without any armed path'
$unarmed=&$runner|ConvertFrom-Json;Check ($unarmed.state -ceq 'NOT_ARMED' -and $unarmed.gpu_launches -eq 0) 'new runner requires explicit ExecuteDiagnostic before any import/host probe'
$text=[IO.File]::ReadAllText($runner);Check (!$text.Contains("'HasPositivePrefix'") -and $text.Contains("'LaunchEpoch'") -and $text.Contains('5DD1A42935B26AF11242162560395A87DDEBEFFB204BFC99E44D304AC543F968')) 'exact pinned whitelist reuses OWN machinery, not the small geometry/predicate'
Check ($text.Contains((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../tools/full-request-prefix-diagnostic.psm1') -Algorithm SHA256).Hash)) 'new runner requires the exact delivered generic validator'
$poll=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'HasPaidGroups'},$true));Check ($poll.Count -eq 1) 'extract actual new live STOP predicate only'
. ([scriptblock]::Create($poll[0].Extent.Text))
$temp=Join-Path ([IO.Path]::GetTempPath()) ('pgn-full-prefix-'+[guid]::NewGuid().ToString('N'));$null=[IO.Directory]::CreateDirectory($temp);$pollPassed=$false
try{
 $path=Join-Path $temp 'TESTONLY.stdout';$epoch=[pscustomobject]@{manifest=[pscustomobject]@{stdout=$path}}
 [IO.File]::WriteAllBytes($path,$two.bytes);Check (!(HasPaidGroups $epoch '0' 3)) 'poll two groups cannot trigger slot0-reuse STOP'
 [IO.File]::WriteAllBytes($path,$a.bytes);Check (HasPaidGroups $epoch '0' 3) 'poll at least3 groups triggers only STOP, not acceptance'
 [IO.File]::WriteAllBytes($path,$drain.bytes);Check (!(HasPaidGroups $epoch $base.batches 1)) 'poll resumed baseline-only drain cannot count as a new group'
 [IO.File]::WriteAllBytes($path,$b.bytes);Check (HasPaidGroups $epoch $base.batches 1) 'poll resumed batches compare captured baseline+1'
 [IO.File]::WriteAllBytes($path,$tail.bytes);Check (!(HasPaidGroups $epoch '0' 1)) 'poll full completion is not a partial STOP target'
 $counterOnly=Counter $a.last;[IO.File]::WriteAllBytes($path,(Bytes $counterOnly));Check (HasPaidGroups $epoch '0' 3) 'live counter is a provisional trigger, deliberately not proof'
 Held {Read-PgnFullRequestSource (Bytes $counterOnly) $start $length $fresh} 'a counter-trigger without actual header/markers/footer never promotes acceptance'
 $pollPassed=$true
}finally{
 if($pollPassed){$resolved=[IO.Path]::GetFullPath($temp);$expectedParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]@('/','\'));if([IO.Path]::GetDirectoryName($resolved) -ine $expectedParent -or [IO.Path]::GetFileName($resolved) -cnotmatch '\Apgn-full-prefix-[0-9a-f]{32}\z'){throw 'Private cleanup target guard'};Remove-Item -LiteralPath $resolved -Recurse -Force}else{[Console]::Error.WriteLine('Failed private poll fixture retained: '+$temp)}
}
$forbidden=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -in @('Stop-Process','ssh','Invoke-Command','nvcc')},$true));Check ($forbidden.Count -eq 0) 'no kill/SSH/build command in new diagnostic'
foreach($pin in @(@('test-terminal-boundary-stop-resume.ps1','5DD1A42935B26AF11242162560395A87DDEBEFFB204BFC99E44D304AC543F968'),@('source-stop-resume-diagnostic.psm1','73E7E360EC14B31C5250419EC610F60B00B465FBEC527283FFE322C0A85C911B'),@('diagnostic-child-process.psm1','8EC24A342507B04816228180FC4E37F0C485EEF1D5E64A418728579B91A59E86'),@('source-stop-checkpoint-evidence.psm1','6CE6A91BA962D491329DAF248D03B69F81CBDB789FE9BE95EE871BE88718ACD8'))){Check ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot ('../tools/'+$pin[0])) -Algorithm SHA256).Hash -ceq $pin[1]) ('frozen dependency preserved '+$pin[0])}
"FULL_REQUEST_PREFIX_DIAGNOSTIC $script:checks checks / 0 failures; TESTONLY synthetic exact journals/64B/resource observations, no process/GPU/SSH/build/user DB, no full-range or independent coverage"
