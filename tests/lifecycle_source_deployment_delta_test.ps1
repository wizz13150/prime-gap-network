param([Parameter(Mandatory=$true)][string]$AuditPath)
$ErrorActionPreference='Stop'
# Read-only comparison of the audited live controller with the intended patch.
# Never dot-source/import/execute a controller or write to its runtime.
function ReadBounded([string]$Path,[int]$Limit){
 $info=Get-Item -LiteralPath $Path
 if($info.Length -gt $Limit){throw 'Unexpected source/audit size'}
 [IO.File]::ReadAllText($info.FullName)
}
function ParseFunctions([string]$Text){
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseInput($Text,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw 'Source syntax error'}
 @($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst]},$true))
}
function ExactlyOne($Functions,[string]$Name){
 $found=@($Functions|Where-Object Name -eq $Name)
 if($found.Count -ne 1){throw ('Expected exactly one '+$Name)}
 $found[0].Extent.Text
}
function Normalize([string]$Text){
 (($Text -split '\r?\n'|ForEach-Object {$_.TrimEnd()}|Where-Object {$_ -ne ''}) -join "`n")
}
function Same([string]$A,[string]$B,[string]$Message){
 if((Normalize $A) -cne (Normalize $B)){throw $Message}
}
$audit=(ReadBounded $AuditPath 131072)|ConvertFrom-Json
if($audit.schema -cne 'pgn-readonly-lifecycle-source-audit-1' -or $audit.computer -cne 'WIZZ'){throw 'Audit identity'}
$remoteControl=$audit.files|Where-Object path -eq 'C:/PGN/runtime/native-control.ps1'
$remoteModule=$audit.files|Where-Object path -eq 'C:/PGN/runtime/range-successor.psm1'
if($remoteControl.sha256 -cne '509D692D019CF19739254DD7956C5DDFE93CE8A5932485945785FA87A0B04805' -or
 $remoteModule.sha256 -cne '95CEECFDA5E3742DA0F9E9E05FA4FC73105C00A610459C30A580DB3D64863CFB'){throw 'Unexpected preimage'}
$localControl=ReadBounded (Join-Path $PSScriptRoot '../tools/native-control.ps1') 131072
$localModule=ReadBounded (Join-Path $PSScriptRoot '../tools/range-successor.psm1') 131072
$rc=ParseFunctions $remoteControl.text;$lc=ParseFunctions $localControl
$rm=ParseFunctions $remoteModule.text;$lm=ParseFunctions $localModule
$remoteRemainder=$remoteControl.text.Replace((ExactlyOne $rc 'Get-PgnSnapshotCompletion'),'')
$localRemainder=$localControl.Replace((ExactlyOne $lc 'Get-PgnSnapshotCompletion'),'').Replace((ExactlyOne $lc 'Get-PgnSnapshotSourceCompletion'),'')
$localRemainder=$localRemainder.Replace("# Source evidence and permission to continue are different questions. A later`n# operator STOP cannot undo an exact successful full-range result, but it MUST`n# still prevent automatic succession. Never alter the observed STOP fields.`n",'')
# Accept CRLF on Windows without changing the token sequence elsewhere.
$localRemainder=$localRemainder.Replace("`r`n","`n")
$localRemainder=$localRemainder.Replace("# Source evidence and permission to continue are different questions. A later`n# operator STOP cannot undo an exact successful full-range result, but it MUST`n# still prevent automatic succession. Never alter the observed STOP fields.`n",'')
$captureStart=$localRemainder.IndexOf('$body.checkpoint_evidence=@')
$captureEnd=$localRemainder.IndexOf('$body.operator_stop=', $captureStart)
if($captureStart -lt 0 -or $captureEnd -le $captureStart){throw 'Capture patch boundary missing'}
$localRemainder=$localRemainder.Remove($captureStart,$captureEnd-$captureStart)
$localRemainder=[regex]::Replace($localRemainder,'(?m)^[ \t]*\$body\.source_completion_observation=.*\n','')
Same $remoteRemainder $localRemainder 'Unreviewed controller delta outside capture/SOURCE-only verifier'
$oldProof=ExactlyOne $rm 'Get-PgnSourceCompletion'
$newProof=ExactlyOne $lm 'Get-PgnSourceCompletionEvidence'
$expectedProof=$oldProof.Replace('function Get-PgnSourceCompletion(','function Get-PgnSourceCompletionEvidence(').Replace(',[bool]$StopRequested)',')')
$expectedProof=$expectedProof.Replace(" if(`$HasCheckpoint -or `$StopRequested){throw 'Paused or checkpointed source is not range completion'}"," if(`$HasCheckpoint){throw 'Checkpointed source is not range completion'}")
Same $expectedProof $newProof 'Quantitative SOURCE proof changed beyond separating STOP permission'
$remoteRemainder=$remoteModule.text.Replace($oldProof,'')
$localRemainder=$localModule.Replace($newProof,'').Replace((ExactlyOne $lm 'Get-PgnSourceCompletion'),'')
$remoteRemainder=[regex]::Replace($remoteRemainder,'(?m)^Export-ModuleMember .*','')
$localRemainder=[regex]::Replace($localRemainder,'(?m)^Export-ModuleMember .*','')
Same $remoteRemainder $localRemainder 'Unreviewed successor/qualification/selection delta'
'PASS exact audited live source preserved outside checkpoint capture and STOP-independent SOURCE evidence; no runtime imported or mutated'
