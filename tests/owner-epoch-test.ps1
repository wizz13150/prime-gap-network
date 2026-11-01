$ErrorActionPreference='Stop'
$controller=Join-Path $PSScriptRoot '../tools/native-control.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $controller),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Controller syntax errors'}
$function=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Owner'},$true)
Invoke-Expression $function.Extent.Text
$root='C:/PGN/runtime';$binding=@{uuid='GPU-test';executable='C:/PGN/native/pgn-continuous-gpu.exe'}
$m=@{run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';uuid='GPU-test';pid=42;birth='2026-10-13T12:00:00.0000000Z';stdout='C:/PGN/runtime/current-job/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.stdout';stop='C:/PGN/runtime/current-job/stop.request'}
$script:proc=$null;$script:others=@();$script:closed=$null
function Get-Process {param($Id,$ErrorAction)$script:proc}
function Get-CimInstance {param($ClassName,$Filter)$script:others}
function ReadJson {param($Path)$script:closed}
$script:checks=0
function Check([bool]$ok,[string]$label){if(!$ok){throw $label};++$script:checks}
function Refuses([string]$label){$refused=$false;try{$null=Owner $m}catch{$refused=$_.Exception.Message -ceq 'Process identity mismatch; no mutation'};Check $refused $label}
Check ($null -eq (Owner $null)) 'No manifest'
Check ($null -eq (Owner $m)) 'Absent PID'
$script:proc=@{Id=42;StartTime=[DateTime]::Parse($m.birth).ToLocalTime();Path=$binding.executable}
Check ((Owner $m).Id -eq 42) 'Exact current owner'
$script:proc=@{Id=42;StartTime=[DateTime]::Parse('2026-10-13T13:00:00Z').ToLocalTime();Path='C:/Windows/System32/notepad.exe'}
Refuses 'Reused PID without exit receipt'
$script:closed=@{run=$m.run;exit_code=0;utc='2026-10-13T12:30:00Z'}
Check ($null -eq (Owner $m)) 'Closed epoch with later unrelated PID'
$script:closed.run='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';Refuses 'Wrong exit run';$script:closed.run=$m.run
$script:closed.exit_code=$null;Refuses 'Unknown exit';$script:closed.exit_code=1;Refuses 'Failed exit';$script:closed.exit_code=0
$script:closed.utc='2026-10-13T11:59:00Z';Refuses 'Exit predates birth';$script:closed.utc='2026-10-13T12:30:00Z'
$script:others=@(@{ProcessId=99;ExecutablePath=$binding.executable});Refuses 'Another engine exists';$script:others=@()
$script:proc.Path=$binding.executable;Refuses 'Same engine different epoch';$script:proc.Path='C:/Windows/System32/notepad.exe'
$script:proc.StartTime=[DateTime]::Parse($m.birth).ToLocalTime();Refuses 'Same birth different image'
$script:proc.StartTime=[DateTime]::Parse('2026-10-13T11:00:00Z').ToLocalTime();Refuses 'Earlier conflicting epoch'
@{status='PASS';checks=$script:checks;target='Exact controller Owner function';gpu_calls=0;process_mutations=0}|ConvertTo-Json -Compress
