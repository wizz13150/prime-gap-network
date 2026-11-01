$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath ($PSScriptRoot+'/../tools/native-control.ps1')).Path
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Production script parse failed'}
$definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'ReadJson'},$true)
if(!$definition){throw 'Production reader missing'}
. ([scriptblock]::Create($definition.Extent.Text))
$qaRoot=[IO.Path]::GetFullPath($PSScriptRoot+'/../private-qa')
$fixture=Join-Path $qaRoot ('metadata-reader-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixture
$file=Join-Path $fixture 'head.json';$next=Join-Path $fixture 'next.json'
function Replace-Fixture { [IO.File]::Replace($next,$file,(Join-Path $fixture ('preserved-'+[guid]::NewGuid().ToString('N')+'.json'))) }
[IO.File]::WriteAllText($file,'{"epoch":"old","exact":"199921000000000000000"}',[Text.UTF8Encoding]::new($false))
if((ReadJson $file).exact -cne '199921000000000000000'){throw 'Exact decimal changed'}
if($null -ne (ReadJson (Join-Path $fixture 'absent.json'))){throw 'Missing metadata invented'}
# Reproduce the sharing boundary with real Windows file handles, no GPU.
[IO.File]::WriteAllText($next,'{"epoch":"new","exact":"199922000000000000000"}',[Text.UTF8Encoding]::new($false))
$oldReader=[IO.File]::Open($file,'Open','Read',[IO.FileShare]::ReadWrite)
$refused=$false
try{Replace-Fixture}catch [IO.IOException]{$refused=$true}finally{$oldReader.Dispose()}
if(!$refused){throw 'Expected old sharing obstruction not reproduced'}
$share=[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
$newReader=[IO.File]::Open($file,'Open','Read',$share)
try{
 Replace-Fixture
 $reader=[IO.StreamReader]::new($newReader)
 $previous=$reader.ReadToEnd()|ConvertFrom-Json
 if($previous.epoch -cne 'old' -or (ReadJson $file).epoch -cne 'new'){throw 'Atomic old/new snapshot conservation failed'}
}finally{$newReader.Dispose()}
foreach($case in @('invalid-json','oversized','invalid-utf8')){
 if($case -eq 'invalid-json'){[IO.File]::WriteAllText($file,'{"epoch":')}
 elseif($case -eq 'oversized'){[IO.File]::WriteAllBytes($file,(New-Object byte[] 32769))}
 else{[IO.File]::WriteAllBytes($file,[byte[]](0xff,0xfe,0xff))}
 $rejected=$false;try{$null=ReadJson $file}catch{$rejected=$true}
 if(!$rejected){throw ('Unsafe metadata admitted: '+$case)}
 # Failed parsing/bounds must also release the physical read handle.
 [IO.File]::WriteAllText($next,'{"epoch":"recovered"}',[Text.UTF8Encoding]::new($false))
 Replace-Fixture
 if((ReadJson $file).epoch -cne 'recovered'){throw 'Reader handle leaked'}
}
[IO.File]::WriteAllText($file,'{"epoch":"bom"}',[Text.UTF8Encoding]::new($true))
if((ReadJson $file).epoch -cne 'bom'){throw 'Valid UTF8 BOM rejected'}
@{result='PASS';fixture=$fixture;scope='Actual production ReadJson AST; Windows atomic replacement; exact values; byte bound; malformed data; handle release; no GPU calls'}|ConvertTo-Json -Compress
