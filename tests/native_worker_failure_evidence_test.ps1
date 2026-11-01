param([string]$ControllerSource=(Join-Path $PSScriptRoot '../tools/native-control.ps1'))
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($ControllerSource,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Actual controller source must parse'}
$functions=@($ast.FindAll({param($node);$node -is [Management.Automation.Language.FunctionDefinitionAst]-and$node.Name-ceq'Get-PgnWorkerFailureEvidence'},$true))
if($functions.Count-ne1){throw 'Exactly one actual diagnostic function required'}
# Extract only the pure function. Never execute a controller action, lease,
# scheduled task, driver query or GPU process in this test.
. ([scriptblock]::Create($functions[0].Extent.Text))
$checks=0
function Check([bool]$Pass,[string]$Reason){$script:checks++;if(!$Pass){throw $Reason}}
$caught=$null
try{$missing=$null;$missing.NoSuchMethod()}catch{$caught=$_}
$record=Get-PgnWorkerFailureEvidence $caught 'PREPARE_OWNED_OUTPUTS'
Check ($record.error.Length-gt0) 'real exception message retained'
Check ($record.exception_type.Length-gt0) 'real exception type retained'
Check ($record.error_id.Length-gt0) 'real error identifier retained'
Check ($record.position.Contains('NoSuchMethod')) 'actual failing expression retained'
Check ($record.script_stack.Length-gt0) 'actual script stack retained'
Check ($record.stage-ceq'PREPARE_OWNED_OUTPUTS') 'stage correlated with failure'
Check ($record.scope-ceq'CONTROLLER_DIAGNOSTIC_NOT_PROCESS_OWNERSHIP_OR_SCIENTIFIC_CREDIT') 'evidence cannot admit or grant scientific credit'
$large=$null
try{throw ('x'*10000)}catch{$large=$_}
$bounded=Get-PgnWorkerFailureEvidence $large ('s'*500)
Check ($bounded.error.Length-eq2000) 'large message bounded'
Check ($bounded.stage.Length-eq80) 'stage bounded'
Check ($bounded.position.Length-le1600-and$bounded.script_stack.Length-le2000) 'location and stack bounded'
$empty=Get-PgnWorkerFailureEvidence $null ''
Check ($empty.error-ceq'Unknown worker failure') 'missing exception does not cause a secondary null method failure'
$json=$record|ConvertTo-Json -Compress
$copy=$json|ConvertFrom-Json
Check ($copy.error-ceq$record.error-and$copy.position-ceq$record.position) 'actual PS5 JSON roundtrip'
$utf8=[Text.UTF8Encoding]::new($false,$true)
Check ($utf8.GetByteCount(($bounded|ConvertTo-Json -Compress))-lt12000) 'failure remains below metadata size limit'
[ordered]@{state='REAL_ERROR_RECORD_LOCATION_BOUNDS_AND_PS5_SERIALIZATION_PASS';checks=$checks;GPUlaunches=0;controller_actions=0}|ConvertTo-Json -Compress
