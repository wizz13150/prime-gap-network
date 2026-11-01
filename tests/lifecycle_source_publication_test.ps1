$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../tools/deploy-lifecycle-source.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Installer syntax error'}
# Extract only filesystem atoms: NEVER execute the top-level runtime deployer.
foreach($name in @('Digest','WriteNewJson','PublishSource','RestoreSource')){
 $nodes=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
 if($nodes.Count -ne 1){throw 'Exactly one production atom required'}
 . ([scriptblock]::Create($nodes[0].Extent.Text))
}
$root=Join-Path ([IO.Path]::GetTempPath()) ('pgn-source-publication-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
function Fixture([string]$Name,[string]$Value){$p=Join-Path $root $Name;[IO.File]::WriteAllText($p,$Value,[Text.UTF8Encoding]::new($false));$p}
function MustThrow($Action){$failed=$false;try{& $Action}catch{$failed=$true};if(!$failed){throw 'Expected fail-closed refusal'}}
$source=Fixture 'source.ps1' 'new source'
$target=Fixture 'target.ps1' 'old source'
$backup=Join-Path $root 'target.before';$failed=Join-Path $root 'target.failed'
$newHash=Digest $source;$oldHash=Digest $target
MustThrow {PublishSource $source $target $backup ('0'*64) $oldHash}
if((Digest $target) -cne $oldHash -or (Test-Path -LiteralPath $backup)){throw 'Bad staged pin mutated target'}
MustThrow {PublishSource $source $target $backup $newHash ('0'*64)}
if((Digest $target) -cne $oldHash){throw 'Bad preimage mutated target'}
PublishSource $source $target $backup $newHash $oldHash
if((Digest $target) -cne $newHash -or (Digest $backup) -cne $oldHash){throw 'Replace did not preserve exact preimage'}
RestoreSource $target $backup $failed
if((Digest $target) -cne $oldHash -or (Digest $backup) -cne $oldHash -or (Digest $failed) -cne $newHash){throw 'Rollback lost old/new bytes'}
$helper=Join-Path $root 'helper.psm1';$absentBackup=Join-Path $root 'helper.before';$failedHelper=Join-Path $root 'helper.failed'
PublishSource $source $helper $absentBackup $newHash $null
MustThrow {PublishSource $source $helper $absentBackup $newHash $null}
RestoreSource $helper $absentBackup $failedHelper
if((Test-Path -LiteralPath $helper) -or (Digest $failedHelper) -cne $newHash){throw 'New helper rollback did not retain bytes'}
$pending=Fixture 'target.ps1.ROOT553.pending' 'unresolved staged bytes'
MustThrow {PublishSource $source $target (Join-Path $root 'second.before') $newHash $oldHash}
if((Digest $target) -cne $oldHash -or [IO.File]::ReadAllText($pending) -cne 'unresolved staged bytes'){throw 'Unresolved pending was overwritten'}
$intent=Join-Path $root 'intent.json'
WriteNewJson $intent @{schema='closed-test';old=$oldHash;new=$newHash}
$intentHash=Digest $intent
MustThrow {WriteNewJson $intent @{schema='overwrite-refused'}}
if((Digest $intent) -cne $intentHash){throw 'Write-ahead identity overwritten'}
('PASS source publication/rollback/idempotency guards; only own temporary files, no runtime/controller/GPU/task/UI; fixtures retained '+$root)
