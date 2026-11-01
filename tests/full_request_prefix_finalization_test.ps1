# Closed PS5 regression: run the maintained runner's actual finally/JSON AST,
# not its top-level admission, resource probes, process launches or GPU work.
$ErrorActionPreference='Stop'
$script:checks=0
function Check($Ok,[string]$Why){++$script:checks;if(!$Ok){throw $Why}}
function ParseFile([string]$Path){
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$errors)
 Check ($errors.Count -eq 0) ('Parse failure: '+$Path)
 return $ast
}
function HashBytes([byte[]]$Bytes){
 $sha=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-','')}finally{$sha.Dispose()}
}
$runner=Join-Path $PSScriptRoot '../tools/test-full-request-prefix-stop-resume.ps1'
$small=Join-Path $PSScriptRoot '../tools/test-terminal-boundary-stop-resume.ps1'
Check ($PSVersionTable.PSVersion.Major -eq 5) 'This regression qualifies Windows PowerShell 5 only'
Check ((Get-FileHash -LiteralPath $small -Algorithm SHA256).Hash -ceq '5DD1A42935B26AF11242162560395A87DDEBEFFB204BFC99E44D304AC543F968') 'Frozen small runner changed'
$runnerBytes=[IO.File]::ReadAllBytes($runner);$text=[Text.UTF8Encoding]::new($false,$true).GetString($runnerBytes)
$replacement='$result.resources=$resources.ToArray()';$old='$result.resources=@($resources)'
Check ([regex]::Matches($text,[regex]::Escape($replacement)).Count -eq 1) 'Exactly one array materialization boundary required'
# Pin the one-line delta to the known failed local preimage. No remote file read.
Check ((HashBytes ([Text.UTF8Encoding]::new($false).GetBytes($text.Replace($replacement,$old)))) -ceq 'DD3E3D884C76AD0A92142C364A018F128FAFBF6FE7852E9E4A161B49E0FF45C5') 'Production edit is not the single intended preimage delta'
$ast=ParseFile $runner
$tries=@($ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.TryStatementAst] -and $null -ne $_.Finally})
Check ($tries.Count -eq 1) 'Unique top-level runner finally required'
$body=$tries[0].Finally.Extent.Text
$finalize=[scriptblock]::Create($body.Substring(1,$body.Length-2))
$serializers=@($ast.EndBlock.Statements|Where-Object{$_.Extent.Text -ceq '$result|ConvertTo-Json -Depth 12 -Compress'})
Check ($serializers.Count -eq 1) 'Actual final stdout serializer required'
$serialize=[scriptblock]::Create($serializers[0].Extent.Text)
$smallAst=ParseFile $small
foreach($name in @('NewBytes','NewJson')){
 $functions=@($smallAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)|Where-Object{$_.Name -ceq $name})
 Check ($functions.Count -eq 1) ('Unique frozen JSON helper required: '+$name)
 . ([scriptblock]::Create($functions[0].Extent.Text))
}
# Reproduce the reported PS5 Generic.List binder failure independently of GPU.
$resources=New-Object 'System.Collections.Generic.List[object]'
$resources.Add([pscustomobject]@{device_free_bytes='123';host_free_bytes='456'})
$oldFailed=$false
try{$discard=@($resources)}catch{$oldFailed=$_.Exception -is [ArgumentException]}
Check $oldFailed 'Old Generic.List array subexpression must reproduce the PS5 ArgumentException'
function RunFinalization([int]$Count,[string]$FixtureRoot){
 $Stage=Join-Path $FixtureRoot ('resources-'+$Count)
 $null=[IO.Directory]::CreateDirectory($Stage)
 $resources=New-Object 'System.Collections.Generic.List[object]'
 for($i=0;$i -lt $Count;$i++){$resources.Add([pscustomobject]@{ordinal=$i;device_free_bytes='12345678901234567890';host_free_bytes='2091988540';observed_utc='2026-10-15T05:22:30.0000000Z'})}
 $epochs=New-Object 'System.Collections.Generic.List[object]'
 $result=[ordered]@{schema='pgn-full-request-prefix-runner-1';state='Held';accepted=$false;gpu_launches=0;error='TESTONLY no physical qualification'}
 $stageCreated=$true;$originalVisible=$env:CUDA_VISIBLE_DEVICES
 $requestLock=[IO.MemoryStream]::new();$workerLock=[IO.MemoryStream]::new()
 # This is the complete actual finally block. Empty epochs ensure no process,
 # source-path, output hash or hardware callback can be reached.
 . $finalize
 Check ($result.resources -is [object[]]) ('Actual object[] required for list '+$Count)
 Check ($result.resources.Count -eq $Count) ('List count conserved: '+$Count)
 Check ($resources.Count -eq $Count) 'Resource collection not mutated'
 Check (!$requestLock.CanWrite -and !$workerLock.CanWrite) 'Actual finally disposed both own fixture locks'
 Check (!$result.accepted -and $result.state -ceq 'Held' -and $result.gpu_launches -eq 0) 'Finalization must not promote qualification'
 Check ($result.processes.Count -eq 0 -and $result.outputs.Count -eq 0) 'No fabricated process or SOURCE output'
 $json=. $serialize
 $disk=[IO.File]::ReadAllText((Join-Path $Stage 'result.json'),[Text.UTF8Encoding]::new($false,$true))
 foreach($wire in @($json,$disk)){
  Check ($wire -match '"resources":\[') 'JSON resources must remain an array including singleton/empty cases'
  $parsed=$wire|ConvertFrom-Json
  Check ($parsed.resources -is [array] -and $parsed.resources.Count -eq $Count) ('JSON resource cardinality: '+$Count)
  Check (!$parsed.accepted -and $parsed.state -ceq 'Held' -and $parsed.gpu_launches -eq 0) 'Serialized result preserves nonphysical verdict'
  for($i=0;$i -lt $Count;$i++){
   Check ($parsed.resources[$i].ordinal -eq $i -and $parsed.resources[$i].device_free_bytes -ceq '12345678901234567890' -and $parsed.resources[$i].host_free_bytes -ceq '2091988540' -and $parsed.resources[$i].observed_utc -ceq '2026-10-15T05:22:30.0000000Z') 'Exact resource strings/order preserved without numeric/date coercion'
  }
 }
 Check ($json -ceq $disk) 'Actual final stdout and actual NewJson disk serialization agree for the fixture'
}
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('pgn-finalization-test-'+[Guid]::NewGuid().ToString('N'))
$savedVisible=$env:CUDA_VISIBLE_DEVICES
try{
 $null=[IO.Directory]::CreateDirectory($fixture)
 foreach($count in @(0,1,2)){RunFinalization $count $fixture}
 Check ((Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash -ceq (HashBytes $runnerBytes)) 'Runner immutable during closed test'
}finally{
 $env:CUDA_VISIBLE_DEVICES=$savedVisible
 # Only this test-created, fully resolved direct TEMP child may be removed.
 $full=[IO.Path]::GetFullPath($fixture);$parent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
 if([IO.Path]::GetDirectoryName($full) -cne $parent -or [IO.Path]::GetFileName($full) -notmatch '\Apgn-finalization-test-[0-9a-f]{32}\z'){throw 'Unexpected fixture cleanup target'}
 if(Test-Path -LiteralPath $full){Remove-Item -LiteralPath $full -Recurse -Force}
}
'full_request_prefix_finalization_test: '+$script:checks+' checks, 0 failures (PS5 closed finalization only)'
