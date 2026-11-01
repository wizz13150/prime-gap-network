$ErrorActionPreference='Stop'
$modulePath=[IO.Path]::GetFullPath($PSScriptRoot+'/../tools/source-stop-checkpoint-evidence.psm1')
Import-Module $modulePath -Force
$fixture=[IO.Path]::GetFullPath($PSScriptRoot+'/../private-qa/stop-evidence-'+[guid]::NewGuid().ToString('N'))
$job=Join-Path $fixture 'current-job';$null=[IO.Directory]::CreateDirectory($job)
$run='1234567890abcdef1234567890abcdef';$other='abcdef1234567890abcdef1234567890'
$utf8=[Text.UTF8Encoding]::new($false,$true)
$checkpoint=Join-Path $job 'checkpoint';$stdout=Join-Path $job ($run+'.stdout')
$manifestPath=Join-Path $fixture 'process.json';$exitPath=Join-Path $fixture 'last-exit.json'
$checks=0
function Check([bool]$Condition,[string]$Label){$script:checks++;if(!$Condition){throw ('FAILED '+$Label)}}
function Write-Json($Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 8 -Compress),$utf8)}
function Reset-Fixture {
 $script:manifest=[pscustomobject][ordered]@{
  schema='pgn-native-process-1';run=$run;pid=1234;birth='2026-10-14T00:00:00Z';uuid='fixture-public';
  stdout=$stdout;stop=(Join-Path $job 'stop.request');start='184467440737095516260';end='184467440737095526260'
 }
 $script:lastExit=[pscustomobject][ordered]@{run=$run;exit_code=[long]0;utc='2026-10-14T00:00:01Z'}
 Write-Json $manifestPath $manifest;Write-Json $exitPath $lastExit
 $stream=[IO.MemoryStream]::new();$writer=[IO.BinaryWriter]::new($stream)
 try{
  $writer.Write([Text.Encoding]::ASCII.GetBytes('PGNCG001'))
  foreach($n in @([uint64]100,[uint64]10,[uint64]10000,[uint64]123,[uint64]9007199254740993,[uint64]7,[uint64]2)){$writer.Write([uint64]$n)}
  $script:expectedBytes=$stream.ToArray()
 }finally{$writer.Dispose();$stream.Dispose()}
 $script:expectedTail="[pgn-counters] time_ns=9007199254740993 positions=123 gaps=7 tests=9007199254740993 batches=2 processed_gaps=7`r`n[phase0-gpu] stopped positions=123 checkpoint=$checkpoint`r`n"
 [IO.File]::WriteAllBytes($checkpoint,$expectedBytes);[IO.File]::WriteAllBytes($stdout,$utf8.GetBytes($expectedTail))
}
function Capture {Get-PgnStoppedCheckpointEvidence -Root $fixture -Manifest $manifest -LastExit $lastExit -Owned $false -ExpectedRun $run}
function Fingerprint {
 # The writer probe deliberately retains a WRITE handle without writing.
 # Unlike Get-FileHash's sharing mode, this reader can inspect that fixture.
 @($checkpoint,$stdout,$manifestPath,$exitPath)|ForEach-Object{
  $f=[IO.File]::Open($_,'Open','Read',[IO.FileShare]::ReadWrite);$sha=[Security.Cryptography.SHA256]::Create()
  try{[Convert]::ToBase64String($sha.ComputeHash($f))}finally{$sha.Dispose();$f.Dispose()}
 }
}
function Refused([string]$Label){
 $before=(Fingerprint)-join ',';$result=Capture
 Check ($result.status -ceq 'unavailable' -and !!$result.error) $Label
 foreach($field in @('checkpoint_base64','checkpoint_label','source_tail_utf8')){Check (!$result.Contains($field)) ($Label+' no '+$field)}
 Check (((Fingerprint)-join ',') -ceq $before) ($Label+' byte preservation')
}
Reset-Fixture
$before=(Fingerprint)-join ',';$result=Capture
Check ($result.status -ceq 'captured' -and $result.error -ceq '') 'normal stopped byte acquisition'
Check ([Convert]::ToBase64String($expectedBytes) -ceq $result.checkpoint_base64) 'exact64 public bytes, no double'
Check ($result.checkpoint_label -ceq $checkpoint -and $result.source_tail_utf8 -ceq $expectedTail) 'producer label and exact CRLF tail'
Check (((Fingerprint)-join ',') -ceq $before) 'success does not write bytes'
$roundtrip=($result|ConvertTo-Json -Depth 8 -Compress)|ConvertFrom-Json
Check ($roundtrip.source_tail_utf8 -ceq $expectedTail -and [Convert]::FromBase64String($roundtrip.checkpoint_base64).Length -eq 64) 'JSON transport exact payload'

# Read only the production snapshot call AST, NEVER dot-source its top level.
$control=[IO.Path]::GetFullPath($PSScriptRoot+'/../tools/native-control.ps1');$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($control,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'real control syntax'
$allCalls=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Get-PgnStoppedCheckpointEvidence'},$true))
# Resume admission and the worker now also acquire the real checkpoint. This
# test executes ONLY the snapshot assignment, never either control branch.
$calls=@($allCalls|Where-Object {
 $parent=$_.Parent
 while($parent -and !($parent -is [Management.Automation.Language.AssignmentStatementAst])){$parent=$parent.Parent}
 $parent -and $parent.Left.Extent.Text -ceq '$body.checkpoint_evidence'
})
Check ($calls.Count -eq 1) 'one real snapshot acquisition call'
$root=$fixture;$body=@{last_exit=$lastExit;owned=$false};$ExpectedRun=$run
$wired=. ([scriptblock]::Create($calls[0].Extent.Text))
Check ($wired.status -ceq 'captured' -and $wired.checkpoint_base64 -ceq $result.checkpoint_base64) 'actual snapshot call uses helper'
$basePath=[IO.Path]::GetFullPath($PSScriptRoot+'/../evidence/co-root548-checkpoint-native-control-before.ps1')
Check ((Get-Item -LiteralPath $basePath).Length -le 65536) 'bounded immutable baseline'
$baseAst=[Management.Automation.Language.Parser]::ParseFile($basePath,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'baseline syntax'
# Scope conservation to the actual snapshot initializer. Unrelated admitted
# initial/resume/stop entry points legitimately evolved after ROOT548.
$initializers=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$body'},$true))
$baseInitializers=@($baseAst.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq '$body'},$true))
Check ($initializers.Count -eq 1 -and $baseInitializers.Count -eq 1) 'unique real snapshot initializers'
Check ($initializers[0].Right.Extent.Text.Replace("`r`n","`n") -ceq $baseInitializers[0].Right.Extent.Text.Replace("`r`n","`n")) 'all original snapshot initializer fields preserved'

foreach($owned in @($true,$null,0,'False')){
 $result=Get-PgnStoppedCheckpointEvidence -Root $fixture -Manifest $manifest -LastExit $lastExit -Owned $owned -ExpectedRun $run
 Check ($result.status -ceq 'unavailable' -and !$result.Contains('checkpoint_base64')) 'ownership must be observed boolean false'
}
$result=Get-PgnStoppedCheckpointEvidence -Root $fixture -Manifest $manifest -LastExit $lastExit -Owned $false -ExpectedRun $other
Check ($result.status -ceq 'unavailable') 'wrong caller run'
foreach($relative in @('C:ambiguous','\ambiguous','relative','\\server\share')){
 $result=Get-PgnStoppedCheckpointEvidence -Root $relative -Manifest $manifest -LastExit $lastExit -Owned $false -ExpectedRun $run
 Check ($result.status -ceq 'unavailable' -and $result.error -ceq 'capture_local_absolute_root_required') 'explicit local fully-qualified root required'
}
foreach($case in @('run','schema','start-number','end-leading-zero','stdout','stop','exit-run','exit-null','exit-string','exit-double','exit-nonzero')){
 Reset-Fixture
 switch($case){
  'run'{$manifest.run=$other};'schema'{$manifest.schema='wrong'};'start-number'{$manifest.start=[long]1}
  'end-leading-zero'{$manifest.end='001'};'stdout'{$manifest.stdout=Join-Path $fixture 'other.stdout'}
  'stop'{$manifest.stop=Join-Path $fixture 'stop.request'};'exit-run'{$lastExit.run=$other}
  'exit-null'{$lastExit.exit_code=$null};'exit-string'{$lastExit.exit_code='0'}
  'exit-double'{$lastExit.exit_code=[double]0};'exit-nonzero'{$lastExit.exit_code=[long]1}
 }
 Refused ('identity/type '+$case)
}
foreach($size in @(0,1,7,8,63,65,4096)){
 Reset-Fixture;$bytes=New-Object byte[] $size
 if($size -ge 8){[Array]::Copy($expectedBytes,$bytes,8)}
 [IO.File]::WriteAllBytes($checkpoint,$bytes);Refused ('checkpoint size '+$size)
}
Reset-Fixture;$expectedBytes[0]=0;[IO.File]::WriteAllBytes($checkpoint,$expectedBytes);Refused 'magic mismatch'
foreach($case in @('empty','fragment','nul','bad-utf8','oversize-no-row')){
 Reset-Fixture
 switch($case){
  'empty'{$bytes=[byte[]]@()};'fragment'{$bytes=$utf8.GetBytes('not a complete final row')}
  'nul'{$bytes=[byte[]](65,0,10)};'bad-utf8'{$bytes=[byte[]](0xc3,0x28,10)}
  'oversize-no-row'{$bytes=$utf8.GetBytes(('x'*70000)+"`n")}
 }
 [IO.File]::WriteAllBytes($stdout,$bytes);Refused ('source '+$case)
}
Reset-Fixture
$bytes=$utf8.GetBytes(('e'*70000)+"`n"+$expectedTail);[IO.File]::WriteAllBytes($stdout,$bytes)
$result=Capture
Check ($result.status -ceq 'captured' -and $result.source_tail_utf8 -ceq $expectedTail) 'large file bounded final complete rows only'
Check ($utf8.GetByteCount($result.source_tail_utf8) -le 65536) 'UTF8 byte bound'
Reset-Fixture
$prefix=('z'*(65536-$utf8.GetByteCount($expectedTail)-1))+"`n"
[IO.File]::WriteAllBytes($stdout,$utf8.GetBytes($prefix+$expectedTail));$result=Capture
Check ($result.status -ceq 'captured' -and $utf8.GetByteCount($result.source_tail_utf8) -eq 65536) 'exact64KiB accepted, byte budget not character budget'
Check ($utf8.GetByteCount(($result|ConvertTo-Json -Depth 8 -Compress)) -le 98304) 'whole encoded evidence fits96KiB'
foreach($escape in @([char]1,[char]34)){
 Reset-Fixture
 $prefix=([string]$escape)*(65536-$utf8.GetByteCount($expectedTail)-1)+"`n"
 [IO.File]::WriteAllBytes($stdout,$utf8.GetBytes($prefix+$expectedTail))
 $before=(Fingerprint)-join ',';$result=Capture
 Check ($result.status -ceq 'unavailable' -and $result.error -ceq 'capture_json_size_limit') 'escaped whole object budget explicit refusal'
 foreach($field in @('checkpoint_base64','checkpoint_label','source_tail_utf8')){Check (!$result.Contains($field)) 'encoded overbudget publishes no partial evidence'}
 Check (((Fingerprint)-join ',') -ceq $before) 'encoded overbudget preserves complete source bytes'
}
Reset-Fixture
$expectedTail=('[public] '+[char]0xe9+[char]0x6f22+[char]0x5b57+" +%25`n")+$expectedTail;[IO.File]::WriteAllBytes($stdout,$utf8.GetBytes($expectedTail))
$result=Capture;Check ($result.status -ceq 'captured' -and $result.source_tail_utf8 -ceq $expectedTail) 'Unicode unchanged (no normalization)'

Reset-Fixture;$fresh=$manifest.PSObject.Copy();$fresh.birth='different';Write-Json $manifestPath $fresh
Refused 'stale manifest pinned against actual'
Reset-Fixture;$fresh=$lastExit.PSObject.Copy();$fresh.utc='different';Write-Json $exitPath $fresh
Refused 'stale exit pinned against actual'
foreach($case in @('oversized','malformed','invalid-utf8','non-object')){
 Reset-Fixture
 switch($case){
  'oversized'{$bytes=$utf8.GetBytes('x'*32769)};'malformed'{$bytes=$utf8.GetBytes('{')}
  'invalid-utf8'{$bytes=[byte[]](0xff,0xfe,0xff)};'non-object'{$bytes=$utf8.GetBytes('0')}
 }
 [IO.File]::WriteAllBytes($manifestPath,$bytes);Refused ('actual metadata '+$case)
}
foreach($path in @($manifestPath,$exitPath,$checkpoint,$stdout)){
 Reset-Fixture
 $writer=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
 try{Refused ('concurrent writer '+[IO.Path]::GetFileName($path))}finally{$writer.Dispose()}
 Check ((Capture).status -ceq 'captured') 'failure releases own handles'
}
# Exercise the real sharing handle primitive: no writer/delete can acquire
# the active final file. This is NOT a global lifecycle lock or physical stop.
Reset-Fixture;$handle=[PgnCheckpointCapture.NativeFile]::Hold($checkpoint,$false)
try{
 $denied=$false;try{$w=[IO.File]::Open($checkpoint,'Open','Write','ReadWrite');$w.Dispose()}catch{$denied=$true}
 Check $denied 'active capture denies second writer'
 $denied=$false;try{[IO.File]::Delete($checkpoint)}catch{$denied=$true}
 Check $denied 'active capture denies delete'
}finally{$handle.Dispose()}
Check ((Capture).status -ceq 'captured') 'sharing probe preserved data'

Reset-Fixture
$junction=Join-Path $fixture 'root-junction';$null=New-Item -ItemType Junction -Path $junction -Target $fixture
$linkManifest=$manifest.PSObject.Copy()
$linkManifest.stdout=Join-Path $junction ('current-job/'+$run+'.stdout');$linkManifest.stop=Join-Path $junction 'current-job/stop.request'
$result=Get-PgnStoppedCheckpointEvidence -Root $junction -Manifest $linkManifest -LastExit $lastExit -Owned $false -ExpectedRun $run
Check ($result.status -ceq 'unavailable') 'reparse root rejected'
# Directly prove the same final-handle check rather than following a link.
$denied=$false;try{$h=[PgnCheckpointCapture.NativeFile]::Hold($junction,$true);$h.Dispose()}catch{$denied=$true}
Check $denied 'actual junction handle rejected'
# Keep fixture evidence, but remove this exact link to avoid recursive cycles.
[IO.Directory]::Delete($junction)

foreach($path in @($checkpoint,$stdout,$manifestPath,$exitPath)){
 Reset-Fixture;$preserved=$path+'.missing-preserved';[IO.File]::Move($path,$preserved)
 try{
  $result=Capture;Check ($result.status -ceq 'unavailable' -and !$result.Contains('checkpoint_base64')) 'missing is not zero'
  Check (!(Test-Path -LiteralPath $path)) 'capture does not create missing files'
 }finally{[IO.File]::Move($preserved,$path)}
}
@{result='PASS';checks=$checks;failures=0;fixture=$fixture;ps=$PSVersionTable.PSVersion.ToString();scope='own files, real Win32 handles and exact production snapshot AST call; no top-level control/process/GPU/SSH/UI/DB'}|ConvertTo-Json -Compress
