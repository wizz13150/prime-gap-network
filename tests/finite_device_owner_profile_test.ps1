# CLOSED PS5.1 guard test. NO C:/PGN filesystem/process/driver/control access.
# Production function bodies are dot-sourced unchanged. Only external leaves
# listed below are replaced; the actual shared range declaration is executed.
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$script:checks=0;$script:failures=0
function Check([bool]$Ok,[string]$Why){$script:checks++;if(!$Ok){$script:failures++;Write-Output ('FAIL '+$Why)}}
function Reject([scriptblock]$Body,[string]$Why){$held=$false;try{& $Body|Out-Null}catch{$held=$true};Check $held $Why}
$script:Tool=Join-Path $PSScriptRoot '../tools/finite-device-owner.ps1'
$script:Range=(Resolve-Path (Join-Path $PSScriptRoot '../tools/range-successor.psm1')).Path
$script:Module='C:/PGN/runtime/deployments/root563-native-v2-closure-20261015-v1/range-successor.psm1'
$script:ModuleSha='DAE02BA2A144024CE8C3659339F1592D34C600991FBDF87A4797561A1BB70AC6'
if((Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $script:Range).Hash -cne $script:ModuleSha){throw 'Real shared declaration preimage changed'}
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $script:Tool).Path,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Production helper parse error'}
if(@($ast.EndBlock.Statements|Where-Object {$_ -isnot [Management.Automation.Language.FunctionDefinitionAst]}).Count){throw 'Test only loads function definitions, no top-level production action permitted'}
. $script:Tool
$script:Fixture=[IO.Path]::Combine([IO.Path]::GetTempPath(),('pgn-finite-owner-profile-'+[guid]::NewGuid().ToString('N')))
if([IO.Directory]::Exists($script:Fixture)){throw 'Fresh private fixture required'}
$null=[IO.Directory]::CreateDirectory($script:Fixture)
$script:Birth='2026-10-15T00:00:00.1234567Z';$script:Run='a'*32;$script:Uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac'
$script:V2Sdk='C:/PGN/native/20261015-source-boundary-v2';$script:A73Sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
$script:V2Sha='74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B'
$script:A73Sha='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'
$script:CudaSha='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'
$script:V2Q='C:/PGN/native/20261015-source-boundary-v2/root555-source-boundary-v2-engine-qualification-20261015.json'
$script:A73Q='C:/PGN/evidence/gpu-owned-source-qualified-20261012-v1/result.json'
$script:V2QSha='2DB06EAD094579A18588BBEF6429102A5E59052BCF2D09E3A13E0E8E84964C52'
$script:A73QSha='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'
function Save-Fixture {
 [IO.File]::WriteAllText((Join-Path $script:Fixture 'process.json'),($script:Epoch|ConvertTo-Json -Depth 8 -Compress),[Text.UTF8Encoding]::new($false))
 [IO.File]::WriteAllText((Join-Path $script:Fixture 'binding.json'),($script:Binding|ConvertTo-Json -Depth 8 -Compress),[Text.UTF8Encoding]::new($false))
}
function Reset-Fixture([bool]$V2=$true){
 $sdk=if($V2){$script:V2Sdk}else{$script:A73Sdk};$sha=if($V2){$script:V2Sha}else{$script:A73Sha};$q=if($V2){$script:V2Q}else{$script:A73Q};$qsha=if($V2){$script:V2QSha}else{$script:A73QSha}
 $policy=if($V2){'GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2'}else{'GPU_OWNED_HIGH10_SOURCE_V1'}
 $script:Epoch=[pscustomobject]@{pid=4242;birth=$script:Birth;run=$script:Run;uuid=$script:Uuid}
 $script:Binding=[pscustomobject]@{sdk=$sdk;executable=$sdk+'/bin/pgn-continuous-gpu.exe';engine_upgrade=[pscustomobject]@{policy=$policy;qualification=$q;qualification_sha256=$qsha};pins=@([pscustomobject]@{path=$sdk+'/bin/pgn-continuous-gpu.exe';sha256=$sha},[pscustomobject]@{path=$sdk+'/bin/cudart64_12.dll';sha256=$script:CudaSha})}
 if($V2){$script:Binding|Add-Member NoteProperty source_boundary_protocol 'pgn-source-boundary-v2'}
 $script:Process=[pscustomobject]@{Id=4242;StartTime=[DateTime]::ParseExact($script:Birth,'o',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind);Path=[IO.Path]::GetFullPath($script:Binding.executable)}
 $script:Hashes=@{};$script:Hashes[$script:Module]=$script:ModuleSha;$script:Hashes[$script:Binding.executable]=$sha;$script:Hashes[$sdk+'/bin/cudart64_12.dll']=$script:CudaSha;$script:Hashes[$q]=$qsha
 $script:Snapshot=[pscustomobject]@{owned=$false;operator_stop=$false;last_exit=[pscustomobject]@{run=$script:Run;exit_code=0};completion_candidate=[pscustomobject]@{run=$script:Run;source_range_complete=$true}}
 $script:Apps=@();$script:GpuExit=0;$script:Io=@();$script:Imports=0;Save-Fixture
}
# EXACT substituted leaves:
# - process/binding JSON reads redirected to two PRIVATE real UTF8 JSON files;
# - native-control -Action snapshot -NoDriver returns an explicit fixture object;
# - Get-Process returns an explicit object/null, NEVER an OS process;
# - Get-FileHash returns an exact allowlisted pin/deliberate poison, NEVER C:/PGN;
# - Import-Module accepts ONLY root's fixed closure path then imports the REAL
#   byte-identical workspace range module, not a fake declaration function;
# - nvidia-smi.exe returns explicit fixture lines/exit, NEVER a driver command.
function Read-ConstantOwnerJson([string]$Path){
 $script:Io+=('json:'+ $Path)
 $leaf=switch -CaseSensitive ($Path){'C:/PGN/runtime/process.json' {'process.json'} 'C:/PGN/runtime/binding.json' {'binding.json'} default {throw 'Unsubstituted metadata path forbidden'}}
 return ([IO.File]::ReadAllText((Join-Path $script:Fixture $leaf))|ConvertFrom-Json)
}
function Get-ConstantOwnerEndedSnapshot{$script:Io+='snapshot';return $script:Snapshot}
function Get-Process {param($Id,$ErrorAction)$script:Io+='process';if($script:Process -and $Id -eq $script:Process.Id){return $script:Process};return $null}
function Get-FileHash {param([string]$LiteralPath)
 $path=$LiteralPath.Replace('\','/');$script:Io+=('hash:'+ $path)
 if(!$script:Hashes.ContainsKey($path)){throw ('Unsubstituted file hash forbidden: '+$path)}
 return [pscustomobject]@{Hash=$script:Hashes[$path]}
}
function Import-Module {param([string]$Name,[string]$Scope,[switch]$Force,[switch]$PassThru)
 if($Name -cne $script:Module -or $Scope -cne 'Local' -or !$Force -or !$PassThru -or $script:Io[-1] -cne ('hash:'+$script:Module)){throw 'Declaration import was not immediately preceded by exact fixed-module hash'}
 $script:Imports++;$script:Io+='import-real-shared-declaration'
 Microsoft.PowerShell.Core\Import-Module -Name $script:Range -Scope Local -Force -PassThru -DisableNameChecking
}
function nvidia-smi.exe {param([Parameter(ValueFromRemainingArguments=$true)]$Arguments)$script:Io+='gpu-query-double';$global:LASTEXITCODE=$script:GpuExit;return $script:Apps}
try{
 foreach($v2 in @($false,$true)){
  Reset-Fixture $v2;$owner=Get-ConstantOwner;$before=$owner|ConvertTo-Json -Depth 8 -Compress;Assert-ConstantOwner $owner
  Check ($owner.pid -eq 4242 -and $owner.birth -ceq $script:Birth -and $owner.run -ceq $script:Run -and $owner.uuid -ceq $script:Uuid) 'Captured active owner exact, not rewritten by assert'
  Check (($owner|ConvertTo-Json -Depth 8 -Compress) -ceq $before) 'Assert preserves owner receipt and age/birth identity'
  Check ($owner.device_code_location -ceq 'EMBEDDED_EXECUTABLE' -and $owner.dll -ceq $script:Binding.executable) 'Legacy device-code alias retained; CUDA runtime not mislabeled device code'
  if($v2){
   Check ($owner.runtime_dll -ceq ($script:V2Sdk+'/bin/cudart64_12.dll') -and $owner.runtime_dll_sha256 -ceq $script:CudaSha -and $script:Imports -ge 3) 'V2 uses actual shared declaration for Get plus repeated assertions'
   foreach($path in @($script:Binding.executable,$owner.runtime_dll,$script:V2Q,$script:Module)){Check (@($script:Io|Where-Object {$_ -ceq ('hash:'+$path)}).Count -ge 3) 'Module/image/CUDA/qualification actually rehashed at repeated guard boundaries'}
  }else{Check ($script:Imports -eq 0 -and !$owner.ContainsKey('runtime_dll')) 'A73 owner branch preserved without module or ABI change'}
 }
 $declarationPoisons=@(
  { $script:Binding.sdk='C:/PGN/native/unknown' },{ $script:Binding.sdk=$null },{ $script:Binding.sdk=@($script:V2Sdk) },
  { $script:Binding.executable=$script:A73Sdk+'/bin/pgn-continuous-gpu.exe' },{ $script:Binding.executable=$null },
  { $script:Binding.engine_upgrade.policy='GPU_OWNED_HIGH10_SOURCE_V1' },{ $script:Binding.engine_upgrade.policy=1 },
  { $script:Binding.engine_upgrade.qualification_sha256=$script:A73QSha },{ $script:Binding.engine_upgrade.qualification_sha256=$null },
  { $script:Binding.engine_upgrade.qualification=$script:A73Q },
  { $script:Binding.PSObject.Properties.Remove('source_boundary_protocol') },{ $script:Binding.source_boundary_protocol=$null },{ $script:Binding.source_boundary_protocol='pgn-source-legacy-a73' },{ $script:Binding.source_boundary_protocol=$true },
  { $script:Binding.pins[0].sha256=$script:A73Sha },{ $script:Binding.pins[1].sha256='0'*64 },{ $script:Binding.pins[1].path=$script:A73Sdk+'/bin/cudart64_12.dll' },
  { $script:Binding.pins=@($script:Binding.pins[0],$script:Binding.pins[0]) },{ $script:Binding.pins=@($script:Binding.pins[0]) },{ $script:Binding.pins[0].sha256=@($script:V2Sha) }
 )
 foreach($poison in $declarationPoisons){Reset-Fixture;& $poison;Save-Fixture;Reject {Get-ConstantOwner} 'V2 declaration mixed/absent/type/pin corruption held'}
 foreach($path in @($script:Module,($script:V2Sdk+'/bin/pgn-continuous-gpu.exe'),($script:V2Sdk+'/bin/cudart64_12.dll'),$script:V2Q)){
  Reset-Fixture;Check ($script:Hashes.ContainsKey($path)) 'Digest poison targets an actual complete guard path, not an unused fixture key';$script:Hashes[$path]='0'*64;Reject {Get-ConstantOwner} 'Observed module/image/CUDA/receipt digest poison held'
  if($path -ceq $script:Module){Check ($script:Imports -eq 0) 'Corrupt module rejected BEFORE any Import-Module code execution'}
  Reset-Fixture;$owner=Get-ConstantOwner;$before=$owner|ConvertTo-Json -Depth 8 -Compress;$script:Hashes[$path]='0'*64;Reject {Assert-ConstantOwner $owner} 'Digest drift AFTER admission rejected at later boundary';Check (($owner|ConvertTo-Json -Depth 8 -Compress) -ceq $before) 'Drift rejection does not rewrite captured owner to new value'
 }
 foreach($poison in @({$script:Epoch.pid=4243},{$script:Epoch.birth='2026-10-15T00:00:01.1234567Z'},{$script:Epoch.run='b'*32},{$script:Epoch.uuid='GPU-11111111-2222-3333-4444-555555555555'},{$script:Process.StartTime=$script:Process.StartTime.AddSeconds(1)},{$script:Process.Path=$script:A73Sdk+'/bin/pgn-continuous-gpu.exe'},{$script:Binding.engine_upgrade.policy='GPU_OWNED_HIGH10_SOURCE_V1'})){
  Reset-Fixture;$owner=Get-ConstantOwner;& $poison;Save-Fixture;Reject {Assert-ConstantOwner $owner} 'Captured process/manifest/profile identity drift rejected'
 }
 Reset-Fixture;$script:Process=$null;Reject {Get-ConstantOwner} 'Default active-only caller still refuses absent productive owner'
 foreach($v2 in @($false,$true)){
  Reset-Fixture $v2;$script:Process=$null;$owner=Get-ConstantOwner -AllowEnded;Check ($owner.ended -eq $true -and $owner.run -ceq $script:Run) 'Explicit AllowEnded accepts exact natural full completion only';Assert-ConstantOwner $owner
  $script:Apps=@($script:Uuid+', 9999');Reject {Assert-ConstantOwner $owner} 'Ended owner cannot hide an actual 3090 compute owner'
  $script:Apps=@();$script:GpuExit=1;Reject {Assert-ConstantOwner $owner} 'Unknown driver query is not no-owner zero'
 }
 foreach($field in @('owned','operator_stop')){
  foreach($bad in @($null,'false',0,$true)){
   Reset-Fixture;$script:Process=$null;$script:Snapshot.$field=$bad;Reject {Get-ConstantOwner -AllowEnded} 'Ended owned/STOP must be typed false, not coercible unknown'
  }
  Reset-Fixture;$script:Process=$null;$script:Snapshot.PSObject.Properties.Remove($field);Reject {Get-ConstantOwner -AllowEnded} 'Ended absent owned/STOP is held'
 }
 foreach($bad in @($null,'true',1,$false)){
  Reset-Fixture;$script:Process=$null;$script:Snapshot.completion_candidate.source_range_complete=$bad;Reject {Get-ConstantOwner -AllowEnded} 'Ended completion must be typed true'
 }
 foreach($poison in @({$script:Snapshot.completion_candidate=$null},{$script:Snapshot.last_exit=$null},{$script:Snapshot.last_exit=@($script:Snapshot.last_exit)},{$script:Snapshot.completion_candidate=@($script:Snapshot.completion_candidate)},{$script:Snapshot.last_exit.run=$null},{$script:Snapshot.last_exit.run='b'*32},{$script:Snapshot.completion_candidate.run='b'*32},{$script:Snapshot.completion_candidate.run=@($script:Run)},{$script:Epoch.run=$null},{$script:Epoch.run=$script:Run+"`n"},{$script:Snapshot.last_exit.exit_code=$null},{$script:Snapshot.last_exit.exit_code='0'},{$script:Snapshot.last_exit.exit_code=[double]0},{$script:Snapshot.last_exit.exit_code=$false},{$script:Snapshot.last_exit.exit_code=1})){
  Reset-Fixture;$script:Process=$null;& $poison;Save-Fixture;Reject {Get-ConstantOwner -AllowEnded} 'Ended missing/foreign/typed run or exit0 shape held'
 }
 Reset-Fixture;$script:Process=$null;$script:Snapshot.last_exit.exit_code=[long]0;Check ((Get-ConstantOwner -AllowEnded).ended -eq $true) 'Typed Int64 exit0 is accepted without floating conversion'
 Check ((Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $script:Range).Hash -ceq $script:ModuleSha) 'Shared declaration bytes remain immutable'
 Write-Output ('PRIVATE_TESTONLY_FIXTURE '+$script:Fixture)
}catch{$script:failures++;Write-Output ('EXCEPTION '+$_.Exception.Message)}
Write-Output ('FINITE_DEVICE_OWNER_PROFILE '+$script:checks+' checks / '+$script:failures+' failures; PS5 guard/real shared declaration with explicit I/O doubles ONLY, NOT physical ownership/two-card independence')
if($script:failures){exit 1};exit 0
