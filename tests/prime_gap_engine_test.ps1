param([Parameter(Mandatory=$true)][string]$Fixture,[Parameter(Mandatory=$true)][string]$PinFixture,[string]$CompletedSource)
$ErrorActionPreference='Stop'
Write-Output 'Engine contract interpreter entered'
Import-Module (Join-Path $PSScriptRoot '../tools/prime-gap-engine.psm1') -Force
$data=Get-Content -LiteralPath $Fixture -Raw|ConvertFrom-Json
Write-Output 'Engine contract fixtures decoded'
$script:checks=0
function Check([bool]$Value,[string]$Message){++$script:checks;if(!$Value){throw $Message}}
foreach($entry in $data.jobs){foreach($k in @(8,16,32,64)){
 $job=Get-PgnPrimeGapJob -Start $entry.start -End $entry.end -ProfileK $k
 Check ($job.bits-eq$entry.bits-and$job.high_word-ceq$entry.high-and$job.low_word-ceq$entry.low-and[string]$job.minimum_gap-ceq$entry.min_gap) 'Cross-language engine tuple differs'
 $arguments=Get-PgnPrimeGapArguments -Job $job -Plan
 Check ($arguments[0]-ceq'--plan'-and$arguments[$arguments.IndexOf('--gap-min')+1]-ceq$entry.min_gap) 'Typed native arguments differ'
}}
foreach($range in $data.rejected_ranges){
 $rejected=$false;try{$null=Get-PgnPrimeGapJob -Start $range[0] -End $range[1]}catch{$rejected=$true}
 Check $rejected 'Unpaid native range was admitted'
}
$binding=Get-Content -LiteralPath $PinFixture -Raw|ConvertFrom-Json
$declaration=Get-PgnPrimeGapEngineDeclaration $binding
Check ($declaration.job.bits-eq95-and$declaration.job.minimum_gap-eq1332) '95-bit declaration differs'
$profile=Get-PgnPrimeGapRunSourceProfile -Binding $binding -HashFile {param($path);return @($binding.pins|Where-Object path -ceq $path)[0].sha256}
Check ($profile.schema-ceq'pgn-run-source-profile-2'-and$profile.job.minimum_gap-ceq'1332') 'Existing PGN run profile differs'
Import-Module (Join-Path $PSScriptRoot '../tools/run-source-profile.psm1') -Force
$existing=Get-PgnVerifiedRunSourceProfile -Binding $binding -InitialPositions '0' -HashFile {param($path);return @($binding.pins|Where-Object path -ceq $path)[0].sha256}
Check ($existing.schema-ceq'pgn-run-source-profile-2'-and$existing.job.minimum_gap-ceq'1332') 'Existing controller profile route did not select the native engine'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -ErrorAction Stop
foreach($entry in $data.jobs){Assert-PgnQualifiedSourceRange $binding ([Numerics.BigInteger]::Parse($entry.start)) ([Numerics.BigInteger]::Parse($entry.end));Check $true 'Existing range gate supports engine width'}
if($CompletedSource){
 $source=Get-Content -LiteralPath $CompletedSource -Raw
 $manifest=[pscustomobject]@{run=('a'*32);uuid=$binding.uuid;start=$binding.start;end=$binding.end;source_profile=$existing}
 $exit=[pscustomobject]@{run=('a'*32);exit_code=0}
 $evidence=Get-PgnSourceCompletionEvidence $binding $manifest $source $exit $false
 Check ($evidence.source_range_complete-and$evidence.source_positions-ceq$binding.length) 'Actual native95 completion did not conserve exact source positions'
 $rejected=$false;try{$null=Get-PgnSourceCompletionEvidence $binding $manifest ($source.Replace('emitted_gaps=8','emitted_gaps=7')) $exit $false}catch{$rejected=$true}
 Check $rejected 'Wrong emitted gap count was admitted as completed source'
}
$plan=$data.actual_plans[0].plan
$job=Get-PgnPrimeGapJob -Start $data.actual_plans[0].start -End $data.actual_plans[0].end -ProfileK $data.actual_plans[0].K
Assert-PgnPrimeGapPlan -Job $job -Plan $plan
Check $true 'Actual GPU plan accepted'
$plan.gap_min+=2
$rejected=$false;try{Assert-PgnPrimeGapPlan -Job $job -Plan $plan}catch{$rejected=$true}
Check $rejected 'Wrong threshold plan accepted'
$parseTokens=$null;$parseErrors=$null
$controllerAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../tools/native-control.ps1'),[ref]$parseTokens,[ref]$parseErrors)
Check ($parseErrors.Count-eq0) ('Existing native controller parse failed: '+($parseErrors|Out-String))
$selfCalls=@($controllerAst.FindAll({param($node);$node-is[Management.Automation.Language.CommandAst]-and$node.Extent.Text.StartsWith('& $PSCommandPath ')},$true))
Check ($selfCalls.Count-eq4) 'All native controller self-invocations identified'
foreach($call in $selfCalls){$parameters=@($call.CommandElements|Where-Object{$_-is[Management.Automation.Language.CommandParameterAst]}|ForEach-Object{$_.ParameterName});Check ($parameters-contains'RuntimeRoot'-and$parameters-contains'LeaseRoot') 'Self-invocation cannot fall back to another runtime/GPU lease'}
$rejected=$false;try{$null=Get-PgnPrimeGapRunSourceProfile -Binding $binding -HashFile {param($path);return ('0'*64)}}catch{$rejected=$true}
Check $rejected 'Bad actual image pin was admitted'
$binding.engine.profile_k=8
$rejected=$false;try{$null=Get-PgnPrimeGapEngineDeclaration $binding}catch{$rejected=$true}
Check $rejected 'Wrong native filename/K accepted'
Write-Output ('Prime-gap existing-controller PowerShell contract PASS checks='+$script:checks+'; no files hashed, processes, GPU or reservations changed')
