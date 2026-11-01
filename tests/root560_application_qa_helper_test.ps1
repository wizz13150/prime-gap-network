param([string]$HelperPath=(Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/qualify-root560-native-v2-application.ps1'),[switch]$ReporterOnly)
$ErrorActionPreference='Stop';$script:checks=0;$script:failures=0
function Check($Condition,[string]$Name){++$script:checks;if(!$Condition){++$script:failures;[Console]::Error.WriteLine('FAIL '+$Name)}}
$parseErrors=$null;$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($HelperPath,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Helper parse failure'}
Check ($ast.Extent.Text.Contains('gpu_launches=$null;gpu_launches_measured=$false')) 'unobserved hardware launches remain unknown, never a declared zero'
Check ($ast.Extent.Text.Contains('explicit_start_requests=0')) 'harness-only request count is separate from unmeasured hardware outcome'
# Extract declarations only. Never dot-source the launch/close program.
$functions=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)
foreach($f in $functions){Invoke-Expression $f.Extent.Text}
$values=[ordered]@{accepted=$false;error='ACTUAL_SSH_DEADLINE';startup_accepted=$false;identity=@{pid=3452};actual_exit=$null;independent_absent=$false;quit=@()}
if(Get-Command Get-Root560QualificationSummary -ErrorAction SilentlyContinue){$summary=Get-Root560QualificationSummary $values}else{$summary=$values|Select-Object accepted,error,startup_accepted,identity,actual_exit,independent_absent,quit}
Check ($summary.accepted -ceq $false -and $summary.error -ceq 'ACTUAL_SSH_DEADLINE' -and $summary.identity.pid -eq 3452) 'summary preserves OrderedDictionary values (not null properties)'
if($ReporterOnly){'RESULT checks='+$script:checks+' failures='+$script:failures;if($script:failures){exit 1};exit 0}

# Own file fixtures and in-memory lifecycle doubles only: no child process,
# GUI, actual staged image, existing PID, IPC, SSH or control is touched.
$temp=Join-Path ([IO.Path]::GetTempPath()) ('pgn-root560-helper-test-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($temp)
$image=Join-Path $temp 'not-an-executable.fixture';[IO.File]::WriteAllBytes($image,[byte[]](1,2,3,4))
$sha=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
# Parse/exercise ONLY the parameter block, never the helper's launch body.
# Each changed image needs explicit pins and a fresh, path-safe attempt name.
$bindOnly=[scriptblock]::Create($ast.ParamBlock.Extent.Text+"`nreturn 'PARAMETERS_ONLY_NO_LAUNCH'")
Check ((& $bindOnly -PgnRoot $temp -AttemptName 'root560-closed-fixture' -ExpectedSourceBytes 4 -ExpectedSourceSha256 $sha) -ceq 'PARAMETERS_ONLY_NO_LAUNCH') 'explicit new revision parameters accepted without helper body'
foreach($badName in @('../outside','root560-../outside','root560-a/b','not-root560','root560-'+('x'*81))){
 $refused=$false;try{& $bindOnly -PgnRoot $temp -AttemptName $badName -ExpectedSourceBytes 4 -ExpectedSourceSha256 $sha|Out-Null}catch{$refused=$true}
 Check $refused 'unsafe attempt name held before helper body'
}
$refused=$false;try{& $bindOnly -PgnRoot $temp -AttemptName 'root560-closed' -ExpectedSourceBytes 0 -ExpectedSourceSha256 $sha|Out-Null}catch{$refused=$true}
Check $refused 'nonpositive image size held'
$refused=$false;try{& $bindOnly -PgnRoot $temp -AttemptName 'root560-closed' -ExpectedSourceBytes 4 -ExpectedSourceSha256 'not-a-pin'|Out-Null}catch{$refused=$true}
Check $refused 'non-SHA image pin held'
$stage=$image;$pin=$sha;$ExpectedSourceBytes=4
$valid=$true;try{PinImage}catch{$valid=$false};Check $valid 'exact supplied image size and SHA accepted'
$ExpectedSourceBytes=5;$refused=$false;try{PinImage}catch{$refused=$true};Check $refused 'changed supplied image size held'
$ExpectedSourceBytes=4;$pin='0'*64;$refused=$false;try{PinImage}catch{$refused=$true};Check $refused 'changed supplied image SHA held'
$pin=$sha
$script:lateWaitCalls=0;$script:lateCompleteAt=3;$script:lateWaitValues=@()
$late=[pscustomobject]@{HasExited=$false}
$late|Add-Member ScriptMethod WaitForExit {param($milliseconds);++$script:lateWaitCalls;$script:lateWaitValues+=,$milliseconds;return $script:lateWaitCalls -ge $script:lateCompleteAt}
Check (Wait-Root560OwnedLateExit -Process $late) 'late exit observed using the same supplied handle'
Check ($script:lateWaitCalls -eq 3 -and @($script:lateWaitValues|Where-Object {$_ -ne 30000}).Count -eq 0) 'late observer uses bounded30s waits only'
$script:lateWaitCalls=0;$script:lateCompleteAt=10
Check (!(Wait-Root560OwnedLateExit -Process $late) -and $script:lateWaitCalls -eq 4) 'late observer ends after four slices, no unbounded retry'
$refused=$false;try{Wait-Root560OwnedLateExit -Process $late -MaximumSlices 0|Out-Null}catch{$refused=$true};Check $refused 'zero slices rejected'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3452,"window_visible":false}');$script:errBytes=[byte[]]@()
$script:complete=$true;$script:code=0;$script:observed=$true;$script:startFails=$false;$script:calls=0;$script:lastProcess=$null;$script:lastArgs=$null
function Start-Process {
 param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,$RedirectStandardOutput,$RedirectStandardError,[switch]$PassThru)
 ++$script:calls
 Check ($FilePath -ceq $image -and $WorkingDirectory -ceq $temp -and $WindowStyle -ceq 'Hidden' -and $PassThru) 'Start-Process uses explicit image/working directory/Hidden/PassThru'
 Check (!(Test-Path -LiteralPath $RedirectStandardOutput) -and !(Test-Path -LiteralPath $RedirectStandardError)) 'nonce output files are fresh'
 $script:lastArgs=@($ArgumentList)
 if($script:startFails){throw 'SYNTHETIC_START_FAILURE'}
 [IO.File]::WriteAllBytes($RedirectStandardOutput,$script:outBytes);[IO.File]::WriteAllBytes($RedirectStandardError,$script:errBytes)
 $p=[pscustomobject]@{Id=812;retained=$false;HasExited=$false;disposed=$false;waits=[Collections.Generic.List[int]]::new()}
 $p|Add-Member ScriptProperty Handle {$this.retained=$true;[IntPtr]123}
 $p|Add-Member ScriptMethod WaitForExit {param($ms) $this.waits.Add($ms);$this.HasExited=$script:complete;return $script:complete}
 $p|Add-Member ScriptMethod Dispose {$this.disposed=$true}
 $script:lastProcess=$p;return $p
}
function Wait-PgnDiagnosticOwnIdentity {
 param($Process,$ExpectedImage,$TimeoutMilliseconds)
 Check ([object]::ReferenceEquals($Process,$script:lastProcess) -and $Process.retained -and $ExpectedImage -ceq $image -and $TimeoutMilliseconds -eq 1000) 'identity observes retained actual child object'
 if($script:observed){return [pscustomobject]@{status='Observed';pid=812;birth='2001-01-01T00:00:00.0000000Z';image=$image}}
 return [pscustomobject]@{status='Held';error='SYNTHETIC_UNOBSERVED'}
}
function Get-PgnDiagnosticChildExit {
 param($Process,$Run)
 Check ([object]::ReferenceEquals($Process,$script:lastProcess) -and $Process.HasExited -and $Run -cmatch '\A[0-9a-f]{32}\z' -and $Run -cne '6229a1173e244b369d7d436ab323981b') 'exit receipt uses actual child and own client GUID'
 return [pscustomobject]@{run=$Run;pid=812;exit_code=[int]$script:code;utc='2001-01-01T00:00:01.0000000Z'}
}
Check ((Get-Command Start-Process).CommandType -eq 'Function') 'Start-Process is a strict in-memory test double'
function CallClient([string]$Action='inspect'){Invoke-Root560OwnedIpcClient -Image $image -ImageSha256 $sha -TargetPid 3452 -Action $Action -OutputDirectory $temp}
$LASTEXITCODE=77
$a=CallClient
Check ($a.accepted -and $a.actual_exit.exit_code -eq 0 -and $a.response.gui_pid -eq 3452) 'inspect receives real exit/JSON, ignores stale LASTEXITCODE'
Check ($script:lastArgs.Count -eq 2 -and $script:lastArgs[0] -ceq '--inspect-owned' -and $script:lastArgs[1] -ceq '3452') 'inspect argv exact and separate'
Check ($script:lastProcess.waits.Count -eq 1 -and $script:lastProcess.waits[0] -eq 5000 -and $script:lastProcess.disposed) 'bounded wait and normal retained-handle release'
$script:outBytes=[byte[]]@();$close=CallClient 'close'
Check ($close.accepted -and $close.response -eq $null -and $close.scope -ceq 'LOCAL_IPC_CLIENT_EXIT_ONLY_NOT_APPLICATION_CLOSE_READY') 'empty close stdout is request ACK only, never CloseReady'
Check ($script:lastArgs[0] -ceq '--close-owned' -and $a.client_run -cne $close.client_run -and $a.stdout_path -cne $close.stdout_path) 'close exact argv and unique nonce evidence'
$empty=CallClient;Check (!$empty.accepted -and $empty.error -like '*inspect JSON*') 'empty inspect is not valid'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{');$bad=CallClient;Check (!$bad.accepted) 'partial JSON held'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3453}');$bad=CallClient;Check (!$bad.accepted) 'other target PID rejected'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3452}');$script:code=7;$bad=CallClient;Check (!$bad.accepted -and $bad.actual_exit.exit_code -eq 7) 'actual nonzero exit preserved'
$script:code=0;$script:outBytes=New-Object byte[] 32769;$bad=CallClient;Check (!$bad.accepted -and $bad.stdout -eq '') 'stdout overflow never clipped into success'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3452}');$script:errBytes=New-Object byte[] 32769;$bad=CallClient;Check (!$bad.accepted) 'stderr overflow held'
$script:errBytes=[byte[]]@();$script:outBytes=[byte[]](0xc3,0x28);$bad=CallClient;Check (!$bad.accepted) 'invalid UTF8 held'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes(([string][char]0xfeff)+'{"gui_pid":3452}');$bad=CallClient;Check (!$bad.accepted -and $bad.error -like '*BOM*') 'actual UTF8 BOM held with ordinal comparison'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3452}'+[char]0);$bad=CallClient;Check (!$bad.accepted -and $bad.error -like '*NUL*') 'NUL held'
$script:outBytes=[Text.Encoding]::UTF8.GetBytes('{"gui_pid":3452}');$script:complete=$false;$before=$script:calls;$bad=CallClient
Check (!$bad.accepted -and $bad.actual_exit -eq $null -and $bad.error -like '*deadline*' -and $script:calls -eq ($before+1) -and $script:lastProcess.disposed) 'timeout unknown with one submission/no retry/no kill'
$script:complete=$true;$script:observed=$false;$bad=CallClient;Check (!$bad.accepted -and $bad.identity.status -ceq 'Held' -and $bad.actual_exit.exit_code -eq 0) 'unobserved identity cannot qualify client even with zero exit'
$script:observed=$true;$script:startFails=$true;$before=$script:calls;$bad=CallClient
Check (!$bad.accepted -and $bad.actual_exit -eq $null -and $script:calls -eq ($before+1)) 'startup failure explicit and no retry'
$script:startFails=$false;$before=$script:calls
try{Invoke-Root560OwnedIpcClient -Image $image -ImageSha256 ('0'*64) -TargetPid 3452 -Action inspect -OutputDirectory $temp|Out-Null}catch{}
Check ($script:calls -eq $before) 'changed image refuses before child submission'
$text=[IO.File]::ReadAllText($HelperPath)
Check ($text -notmatch '&\s*\$stage\s+--(?:inspect|close)-owned' -and $text -notmatch '-Run\s+''6229a1173e244b369d7d436ab323981b''') 'main has no async native invocation or GPU source run reuse'
Check ($text -match 'qa_run=\$qaRun' -and $text -match '-Run \$qaRun') 'GUI diagnostic GUID recorded and used for GUI exit'
Check ($text -match 'if\(\$ownsOutput\)\{SaveJson') 'refused reinit cannot rewrite original failed result'
'RESULT checks='+$script:checks+' failures='+$script:failures+' scope=MOCK_PROCESS_LIFECYCLE_OWN_FILES_NO_PROCESS_ACTION fixtures='+$temp
if($script:failures){exit 1}
