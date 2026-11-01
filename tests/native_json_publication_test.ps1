param([Parameter(Mandatory=$true)][string]$WorkRoot)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$source=Join-Path $PSScriptRoot '../tools/native-control.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Controller source parse failed'}
foreach($name in @('WriteJson','Drain-PgnFailedProducer')){
 $fn=@($ast.FindAll({param($n)$n-is[Management.Automation.Language.FunctionDefinitionAst]},$true)|Where-Object{$_.Name-ceq$name})
 if($fn.Count-ne1){throw 'Exact production function unavailable'}
 Invoke-Expression $fn[0].Extent.Text
}
$root=Join-Path $WorkRoot ('publication-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$checks=0
function Check($condition,$message){if(!$condition){throw $message};$script:checks++}
$p=Join-Path $root 'metadata.json'
WriteJson $p @{version=1};Check ((Get-Content -Raw $p|ConvertFrom-Json).version-eq1) 'Initial atomic creation failed'
$reader=[IO.File]::Open($p,'Open','Read',([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete))
try{
 WriteJson $p @{version=2}
 Check ((Get-Content -Raw $p|ConvertFrom-Json).version-eq2) 'Replacement under delete-sharing reader failed'
 $old=New-Object IO.StreamReader($reader,[Text.Encoding]::UTF8,$true,1024,$true)
 try{Check (($old.ReadToEnd()|ConvertFrom-Json).version-eq1) 'Old reader observed partial/new inode'}finally{$old.Dispose()}
}finally{$reader.Dispose()}
$ready=Join-Path $root 'ready';$begin=Join-Path $root 'begin'
$holder=Join-Path $root 'holder.ps1'
[IO.File]::WriteAllText($holder,@'
param($Target,$Ready,$Begin)
$f=[IO.File]::Open($Target,'Open','Read','ReadWrite')
try{
 [IO.File]::WriteAllText($Ready,'ready')
 $wait=[Diagnostics.Stopwatch]::StartNew()
 while(!(Test-Path -LiteralPath $Begin)){if($wait.ElapsedMilliseconds-gt5000){exit 5};Start-Sleep -Milliseconds 10}
 Start-Sleep -Milliseconds 250
}finally{$f.Dispose()}
'@,[Text.UTF8Encoding]::new($false))
$child=Start-Process -FilePath "$PSHOME/powershell.exe" -ArgumentList @('-NoProfile','-NonInteractive','-File',$holder,$p,$ready,$begin) -WindowStyle Hidden -PassThru
$null=$child.Handle;$wait=[Diagnostics.Stopwatch]::StartNew()
while(!(Test-Path -LiteralPath $ready)){if($wait.ElapsedMilliseconds-gt5000){throw 'Reader did not become ready'};Start-Sleep -Milliseconds 10}
[IO.File]::WriteAllText($begin,'begin');$timer=[Diagnostics.Stopwatch]::StartNew()
WriteJson $p @{version=3}
Check ($timer.ElapsedMilliseconds-ge150) 'Transient sharing conflict was not exercised'
Check ((Get-Content -Raw $p|ConvertFrom-Json).version-eq3) 'Transient sharing failure was not repaired'
$child.WaitForExit();Check ($child.ExitCode-eq0) 'Reader helper failed'
$reader=[IO.File]::Open($p,'Open','Read','ReadWrite');$refused=$false;$timer=[Diagnostics.Stopwatch]::StartNew()
try{try{WriteJson $p @{version=4}}catch{$refused=$true}}finally{$reader.Dispose()}
Check $refused 'Persistent sharing conflict did not refuse'
Check ($timer.ElapsedMilliseconds-ge1500-and$timer.ElapsedMilliseconds-lt5000) 'Sharing retry escaped its monotonic bound'
Check ((Get-Content -Raw $p|ConvertFrom-Json).version-eq3) 'Failed publication altered prior metadata'
Check ((Get-Content -Raw ($p+'.pending')|ConvertFrom-Json).version-eq4) 'Failed publication lost the pending bytes'
$stop=Join-Path $root 'producer.stop';$leasePath=Join-Path $root 'start.lock';$out=Join-Path $root 'producer.stdout'
$producerScript=Join-Path $root 'producer.ps1'
[IO.File]::WriteAllText($producerScript,@'
param($Stop,$Lease)
$wait=[Diagnostics.Stopwatch]::StartNew()
while(!(Test-Path -LiteralPath $Stop)){if($wait.ElapsedMilliseconds-gt5000){exit 6};Start-Sleep -Milliseconds 10}
try{$l=[IO.File]::Open($Lease,'OpenOrCreate','ReadWrite','None');$l.Dispose();exit 7}catch [IO.IOException]{'lease-still-held'}
'final-output-flushed'
exit 17
'@,[Text.UTF8Encoding]::new($false))
$lease=[IO.File]::Open($leasePath,'OpenOrCreate','ReadWrite','None')
try{
 $producer=Start-Process -FilePath "$PSHOME/powershell.exe" -ArgumentList @('-NoProfile','-NonInteractive','-File',$producerScript,$stop,$leasePath) -WindowStyle Hidden -RedirectStandardOutput $out -PassThru
 $null=$producer.Handle;$drained=Drain-PgnFailedProducer $producer $stop
 Check ($producer.HasExited-and$drained.exit_code-eq17) 'Producer drain lost actual nonzero exit'
 $text=[IO.File]::ReadAllText($out)
 Check ($text.Contains('lease-still-held')-and$text.Contains('final-output-flushed')) 'Producer outlived lease or output drain'
}finally{$lease.Dispose()}
@{state='ATOMIC_PUBLICATION_AND_LEASE_RETAINING_FAILURE_DRAIN_PASS';checks=$checks;root=$root;source_sha256=(Get-FileHash -LiteralPath $source).Hash;gpu_launches=0}|ConvertTo-Json -Compress
