param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop';$utf8=[Text.UTF8Encoding]::new($false,$true)
$root=[IO.Path]::GetFullPath($EvidenceRoot)
if(![IO.Path]::IsPathRooted($EvidenceRoot) -or (Test-Path -LiteralPath $root)){throw 'Explicit fresh evidence directory required'}
$null=[IO.Directory]::CreateDirectory($root)
$tools=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../tools'))
$module=Join-Path $tools 'native-controller-closure.psm1'
$observed=Join-Path $tools 'observed-stop-binding.psm1'
$ps5='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe'
$script:checks=0;$script:failures=0;$script:childNumber=0
function Check([bool]$test,[string]$label){$script:checks++;if(!$test){$script:failures++;Write-Output ('FAIL '+$label)}}
function WriteText([string]$path,[string]$text){[IO.File]::WriteAllText($path,$text,$utf8)}
function Json($o){return ($o|ConvertTo-Json -Depth 8 -Compress)}
function Quote([string]$v){return '"'+$v+'"'}
function WaitFile([string]$path){
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while(!(Test-Path -LiteralPath $path)){if($clock.ElapsedMilliseconds -ge 8000){throw ('Fixture child marker timeout: '+$path)};[Threading.Thread]::Sleep(10)}
}
function StartChild([string]$scriptPath,[string[]]$arguments){
 $script:childNumber++;$prefix=Join-Path $root ('child-'+$script:childNumber)
 $argsText='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File '+(Quote $scriptPath)
 foreach($arg in $arguments){$argsText+=' '+(Quote $arg)}
 $p=Start-Process -FilePath $ps5 -ArgumentList $argsText -WindowStyle Hidden -RedirectStandardOutput ($prefix+'.stdout') -RedirectStandardError ($prefix+'.stderr') -PassThru
 # Retain the handle before any wait; never use a later PID lookup/kill.
 $handle=$p.Handle;$p.Refresh()
 return [pscustomobject]@{process=$p;prefix=$prefix;pid=$p.Id;birth=$p.StartTime.ToUniversalTime().ToString('o');image=$p.Path;handle=([string]$handle)}
}
function FinishChild($child){
 if(!$child.process.WaitForExit(12000)){throw 'Fixture child did not exit; no external process was killed'}
 $child.process.WaitForExit();$exit=$child.process.ExitCode
 $out=Get-Item -LiteralPath ($child.prefix+'.stdout');$err=Get-Item -LiteralPath ($child.prefix+'.stderr')
 if($out.Length -gt 16384 -or $err.Length -gt 32768){throw 'Fixture diagnostics exceeded bound'}
 $record=[ordered]@{pid=$child.pid;birth=$child.birth;image=$child.image;handle=$child.handle;exit_code=$exit;stdout_bytes=$out.Length;stderr_bytes=$err.Length}
 WriteText ($child.prefix+'.receipt.json') (Json $record)
 $child.process.Dispose();return $exit
}
function Rejected([scriptblock]$body,[string]$label){try{& $body|Out-Null;Check $false $label}catch{Check $true $label}}
Import-Module $observed -Force
Import-Module $module -Force
$flat=Join-Path $root 'flat';$null=[IO.Directory]::CreateDirectory($flat)
$state=Join-Path $root 'state';$null=[IO.Directory]::CreateDirectory($state)
WriteText (Join-Path $root 'intent.json') (Json ([ordered]@{schema='pgn-controller-closure-test-intent-1';scope='OWN_PS5_FIXTURES_ONLY_NO_CONTROLLER_OR_GPU';root=$root;image=$ps5;image_sha256=(Get-FileHash -LiteralPath $ps5 -Algorithm SHA256).Hash;writer='core_wallet_native_integration';state='RUNNING'}))
function FlatController([string]$version){
 $text=@'
$ErrorActionPreference='Stop'
$ast='__VERSION__'
[IO.File]::WriteAllText('__LOADED__',$ast)
Import-Module '__OBSERVED__' -Force
$gate=Enter-PgnControlTransition '__STATE__'
try {Import-Module (Join-Path $PSScriptRoot 'dependency.psm1') -Force;[ordered]@{ast=$ast;dependency=(Get-FixtureVersion)}|ConvertTo-Json -Compress}finally{$gate.Dispose()}
'@
 return $text.Replace('__VERSION__',$version).Replace('__LOADED__',(Join-Path $flat 'loaded')).Replace('__OBSERVED__',$observed).Replace('__STATE__',$state)
}
function FlatRace([string]$parsed,[string]$installed){
 $marker=Join-Path $flat 'loaded';if(Test-Path -LiteralPath $marker){[IO.File]::Delete($marker)}
 WriteText (Join-Path $flat 'native-control.ps1') (FlatController $parsed)
 WriteText (Join-Path $flat 'dependency.psm1') ('function Get-FixtureVersion {'''+$parsed+'''};Export-ModuleMember Get-FixtureVersion')
 $guard=Enter-PgnControlTransition $state
 try{
  $child=StartChild (Join-Path $flat 'native-control.ps1') @();WaitFile $marker
  WriteText (Join-Path $flat 'native-control.ps1') (FlatController $installed)
  WriteText (Join-Path $flat 'dependency.psm1') ('function Get-FixtureVersion {'''+$installed+'''};Export-ModuleMember Get-FixtureVersion')
 }finally{$guard.Dispose()}
 $exit=FinishChild $child;Check ($exit -eq 0) ('flat '+$parsed+' exit')
 $raw=[IO.File]::ReadAllText($child.prefix+'.stdout')|ConvertFrom-Json
 Check ($raw.ast -ceq $parsed -and $raw.dependency -ceq $installed) ('actual mixed AST/dependency '+$parsed+'/'+$installed)
 return $raw
}
$mixForward=FlatRace 'A' 'B';$mixRollback=FlatRace 'B' 'A'
$entry=Join-Path $root 'entry';$deploy=Join-Path $root 'generations';$null=[IO.Directory]::CreateDirectory($entry);$null=[IO.Directory]::CreateDirectory($deploy)
[IO.File]::Copy($module,(Join-Path $entry 'native-controller-closure.psm1'))
$entryScript=@'
param([string]$Action='start',[string]$ExpectedRun='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'native-controller-closure.psm1') -Force
Invoke-PgnControllerEntry '__ENTRY__' '__DEPLOY__' '__STATE__' $PSBoundParameters
'@
WriteText (Join-Path $entry 'native-control.ps1') ($entryScript.Replace('__ENTRY__',$entry).Replace('__DEPLOY__',$deploy).Replace('__STATE__',$state))
$names=@('native-control.ps1','range-successor.psm1','run-source-profile.psm1','initial-start-binding.psm1','checkpoint-resume-binding.psm1','configured-stop-binding.psm1','observed-stop-binding.psm1','source-stop-checkpoint-evidence.psm1','native-controller-closure.psm1')
function FixturePackage([string]$version,[string]$generation){
 $code=Join-Path $deploy ('fixture-'+$version.ToLowerInvariant());$null=[IO.Directory]::CreateDirectory($code)
 foreach($name in $names){WriteText (Join-Path $code $name) '# fixture-only declared dependency'}
 [IO.File]::Copy($observed,(Join-Path $code 'observed-stop-binding.psm1'),$true)
 [IO.File]::Copy($module,(Join-Path $code 'native-controller-closure.psm1'),$true)
 WriteText (Join-Path $code 'range-successor.psm1') ('function Get-FixtureVersion {'''+$version+'''};Export-ModuleMember Get-FixtureVersion')
 $controller=@'
param([string]$Action='start',[string]$ExpectedRun='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'native-controller-closure.psm1') -Force
$lease=Open-PgnControllerClosure $PSScriptRoot '__ENTRY__' '__STATE__' '__GEN__'
try {
 [IO.File]::WriteAllText('__LOADED__','__VERSION__')
 $guard=Enter-PgnClosureTransition $lease $Action
 try {
  Import-Module (Join-Path $PSScriptRoot 'range-successor.psm1') -Force
  if($Action -eq 'stop-observed' -and $ExpectedRun -cne 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'){throw 'Fixture observed identity mismatch'}
  [IO.File]::WriteAllText('__MUTATION__',$Action+':'+$ExpectedRun)
  [ordered]@{ast='__VERSION__';dependency=(Get-FixtureVersion);action=$Action}|ConvertTo-Json -Compress
 }finally{$guard.Dispose()}
}finally{Close-PgnControllerClosure $lease}
'@
 $controller=$controller.Replace('__ENTRY__',$entry).Replace('__STATE__',$state).Replace('__GEN__',$generation).Replace('__VERSION__',$version).Replace('__LOADED__',(Join-Path $state ($version+'.loaded'))).Replace('__MUTATION__',(Join-Path $state ($version+'.mutation')))
 WriteText (Join-Path $code 'native-control.ps1') $controller
 $files=@();foreach($name in $names){$path=Join-Path $code $name;$files+=,[ordered]@{name=$name;bytes=([string](Get-Item -LiteralPath $path).Length);sha256=((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())}}
 $manifest=[ordered]@{schema='pgn-native-controller-closure-1';generation=$generation;code_root=$code;entry_root=$entry;state_root=$state;files=$files}
 WriteText (Join-Path $code 'closure.json') (Json $manifest)
 return [pscustomobject]@{root=$code;generation=$generation;pointer=[ordered]@{schema='pgn-native-controller-pointer-1';generation=$generation;package=([IO.Path]::GetFileName($code));manifest_sha256=((Get-FileHash -LiteralPath (Join-Path $code 'closure.json') -Algorithm SHA256).Hash.ToLowerInvariant())}}
}
function Publish($package){
 $target=Join-Path $entry 'active.json';$pending=$target+'.pending'
 WriteText $pending (Json $package.pointer)
 if(Test-Path -LiteralPath $target){[IO.File]::Replace($pending,$target,(Join-Path $entry ('pointer-prior-'+[guid]::NewGuid().ToString('N')+'.json')))}else{[IO.File]::Move($pending,$target)}
}
$a=FixturePackage 'A' ('a'*32);$b=FixturePackage 'B' ('b'*32)
foreach($package in @($a,$b)){
 Publish $package;$child=StartChild (Join-Path $entry 'native-control.ps1') @('-Action','start');$exit=FinishChild $child
 Check ($exit -eq 0) 'immutable invocation succeeds'
 $raw=[IO.File]::ReadAllText($child.prefix+'.stdout')|ConvertFrom-Json
 Check ($raw.ast -ceq $raw.dependency) 'immutable AST and dependency coherent'
}
# The controller has already resolved B and holds its files. Revoke B while it
# waits on the REAL control-transition file, not a mocked gate function.
Publish $b;[IO.File]::Delete((Join-Path $state 'B.loaded'))
$before=[IO.File]::ReadAllText((Join-Path $state 'B.mutation'))
$guard=Enter-PgnControlTransition $state
try{
 $child=StartChild (Join-Path $entry 'native-control.ps1') @('-Action','start');WaitFile (Join-Path $state 'B.loaded')
 Publish $a
}finally{$guard.Dispose()}
$exit=FinishChild $child
Check ($exit -ne 0) 'parsed B revoked under actual gate'
Check ([IO.File]::ReadAllText((Join-Path $state 'B.mutation')) -ceq $before) 'revoked generation writes no fixture application state'
Check ([IO.File]::ReadAllText($child.prefix+'.stderr').Contains('generation revoked')) 'refusal is generation guard, not unrelated import failure'
# A request already pinned to old B may still stop its exact fixture owner.
$child=StartChild (Join-Path $b.root 'native-control.ps1') @('-Action','stop-observed');Check ((FinishChild $child) -eq 0) 'revoked exact stop remains available'
$before=[IO.File]::ReadAllText((Join-Path $state 'B.mutation'))
$child=StartChild (Join-Path $b.root 'native-control.ps1') @('-Action','stop-observed','-ExpectedRun',('c'*32))
Check ((FinishChild $child) -ne 0) 'revocation does not bypass fixture observed identity guard'
Check ([IO.File]::ReadAllText((Join-Path $state 'B.mutation')) -ceq $before) 'wrong stop identity leaves state intact'
Publish $a
$lease=Open-PgnControllerClosure $a.root $entry $state $a.generation
try{
 Rejected { $file=[IO.File]::Open((Join-Path $a.root 'range-successor.psm1'),'Open','Write',[IO.FileShare]::ReadWrite);$file.Dispose() } 'held generation denies rewrite'
 Rejected { [IO.File]::Move((Join-Path $a.root 'range-successor.psm1'),(Join-Path $a.root 'range-moved.psm1')) } 'held generation denies rename/delete'
 foreach($action in @('initial','resume','advance','worker','recover')){
  Publish $b;Rejected {$g=Enter-PgnClosureTransition $lease $action;$g.Dispose()} ('revoked '+$action+' denied');Publish $a
 }
}finally{Close-PgnControllerClosure $lease}
$probePath=Join-Path $a.root 'range-successor.psm1';$probeBytes=[IO.File]::ReadAllBytes($probePath)
WriteText $probePath '# corrupt dependency'
Rejected { $l=Open-PgnControllerClosure $a.root $entry $state $a.generation;Close-PgnControllerClosure $l } 'corrupt dependency rejected'
[IO.File]::WriteAllBytes($probePath,$probeBytes)
[IO.File]::Move($probePath,$probePath+'.missing')
Rejected { $l=Open-PgnControllerClosure $a.root $entry $state $a.generation;Close-PgnControllerClosure $l } 'missing dependency rejected'
[IO.File]::Move($probePath+'.missing',$probePath)
# All handles released even after failed pin verification.
$w=[IO.File]::Open($probePath,'Open','Write',[IO.FileShare]::None);$w.Dispose();Check $true 'failure releases private handles'
$validPointer=Json $a.pointer
foreach($poison in @(
 $validPointer.Replace('pgn-native-controller-pointer-1','unknown-schema'),
 $validPointer.Replace('"package":"fixture-a"','"package":"../fixture-a"'),
 $validPointer.Replace('"generation":','"generation":"'+('c'*32)+'","generation":'),
 ([string][char]0xfeff+$validPointer),
 (' '*4097)
)){
 WriteText (Join-Path $entry 'active.json') $poison
 Rejected { Invoke-PgnControllerEntry $entry $deploy $state @{Action='start'} } 'pointer poison refused before controller'
}
Publish $a
$a.pointer.manifest_sha256='0'*64;Publish $a
Rejected {Invoke-PgnControllerEntry $entry $deploy $state @{Action='start'}} 'manifest pointer hash mismatch refused'
$a.pointer.manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $a.root 'closure.json') -Algorithm SHA256).Hash.ToLowerInvariant();Publish $a
$lease=Open-PgnControllerClosure $a.root $entry $state $a.generation;Close-PgnControllerClosure $lease
Rejected {Assert-PgnControllerGeneration $lease} 'closed lease cannot admit work'
$result=[ordered]@{schema='pgn-controller-closure-closed-test-1';checks=$script:checks;failures=$script:failures;children=$script:childNumber;forward_mix=$mixForward;rollback_mix=$mixRollback;scope='OWN_FIXTURES_REAL_PS5_REAL_CONTROL_TRANSITION_NO_ENGINE_OR_RUNTIME';limits='Fixture stop identity is not physical CAS; old A762 AST has no generation fence; no installed controller or engine invoked'}
WriteText (Join-Path $root 'result.json') (Json $result)
$result|ConvertTo-Json -Compress -Depth 8
if($script:failures){exit 1}
