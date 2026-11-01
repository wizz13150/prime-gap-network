param(
 [string]$Controller='',
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [ValidateSet('all','v2')][string]$Cases='all'
)
$ErrorActionPreference='Stop'
if(!$Controller){$Controller=Join-Path $PSScriptRoot '../tools/native-control.ps1'}
# Executes only the existing worker statements, never controller top-level,
# a driver, Start-Process, an installed path, or a real child process.
if(![IO.Path]::IsPathRooted($EvidenceDirectory) -or (Test-Path -LiteralPath $EvidenceDirectory)){throw 'Fresh absolute fixture directory required'}
$null=New-Item -ItemType Directory -Path $EvidenceDirectory
$testState=@{checks=0}
function Check($ok,[string]$why){$testState.checks++;if(!$ok){throw "CHECK $($testState.checks) $why"}}
$errors=$null;$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Controller,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'controller parses'
$workers=@($ast.FindAll({param($n);$n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses[0].Item1.Extent.Text -ceq '$Action -eq ''worker'''},$true))
Check ($workers.Count -eq 1) 'one actual worker'
$statements=@($workers[0].Clauses[0].Item2.Statements)
Check ($statements.Count -eq 7 -and
 $statements[0].Extent.Text -ceq '$lock=[IO.File]::Open((Join-Path $root ''start.lock''),''OpenOrCreate'',''ReadWrite'',''None'')' -and
 $statements[1].Extent.Text -ceq '$workerTransition=$null' -and
 $statements[2].Extent.Text -ceq '$producer=$null' -and
 $statements[3].Extent.Text -ceq '$memoryScope=$null' -and
 $statements[4].Extent.Text -ceq '$workerStage=''ENTER_LAUNCH_TRANSITION''' -and
 $statements[5] -is [Management.Automation.Language.TryStatementAst] -and
 $statements[-1] -is [Management.Automation.Language.ExitStatementAst]) 'worker shape: held lock, transition/producer/stage initialization, actual try/finally, exit'
# Omit only the final process-wide exit; no worker expression is rewritten.
# A real own fixture script preserves nonempty PSScriptRoot semantics (unlike
# ScriptBlock.Create). Its import is intercepted below, not loaded from TEMP.
$script:workerPath=Join-Path $EvidenceDirectory 'derived-worker.ps1'
[IO.File]::WriteAllText($script:workerPath,(($statements[0..5]|ForEach-Object {$_.Extent.Text}) -join "`n"),[Text.UTF8Encoding]::new($false))
$functions=@{}
foreach($name in @('ReadJson','WriteJson','Get-PgnWorkerFailureEvidence','Drain-PgnFailedProducer','Enter-PgnQualifiedMemoryPolicy','Exit-PgnQualifiedMemoryPolicy')){
 $found=@($ast.FindAll({param($n);$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
 Check ($found.Count -eq 1) "one actual $name definition"
 $functions[$name]=$found[0].Extent.Text
}
$script:readDefinition=[scriptblock]::Create($functions.ReadJson)
$script:writeDefinition=[scriptblock]::Create($functions.WriteJson.Replace('function WriteJson','function InvokeSourceWriteJson'))
$script:evidenceDefinition=[scriptblock]::Create($functions.'Get-PgnWorkerFailureEvidence')
$script:drainDefinition=[scriptblock]::Create($functions.'Drain-PgnFailedProducer')
$script:memoryEnterDefinition=[scriptblock]::Create($functions.'Enter-PgnQualifiedMemoryPolicy')
$script:memoryExitDefinition=[scriptblock]::Create($functions.'Exit-PgnQualifiedMemoryPolicy')
$script:modulePath=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../tools/run-source-profile.psm1'))
$script:image='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'
$script:dll='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'
$script:sdk='C:/PGN/native/20261012-gpu-owned-source-v1'

function InvokeCase([string]$name,[bool]$badHash,[string]$resumeCase='',[bool]$v2=$false){
 # Plain locals are intentionally inherited by the derived script and mocks;
 # its own script: scope belongs to the private worker file, not this test.
 $modulePath=$script:modulePath;$image=$script:image;$dll=$script:dll;$sdk=$script:sdk
 $protocol='pgn-source-legacy-a73';$policy='GPU_OWNED_HIGH10_SOURCE_V1';$qualification='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'
 if($v2){$sdk='C:/PGN/native/20261015-source-boundary-v2';$image='74A0FDD01CCF242FA3CA8714E4765C23022987C6785889CD8B1ECFA3AA27472B';$protocol='pgn-source-boundary-v2';$policy='GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2';$qualification='2DB06EAD094579A18588BBEF6429102A5E59052BCF2D09E3A13E0E8E84964C52'}
 $moduleDirectory=Split-Path $modulePath -Parent
 $root=Join-Path $EvidenceDirectory $name
 $null=New-Item -ItemType Directory -Path $root
 $bindingPath=Join-Path $root 'binding.json';$manifestPath=Join-Path $root 'process.json'
 $transitionPath=Join-Path $root 'control-transition.lock'
 $trace=New-Object 'Collections.Generic.List[string]'
 $calls=@{launch=0;publish=0;wait=0;hash=0;driver=0}
 $bindingFixture=@{uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$sdk;executable=($sdk+'/bin/pgn-continuous-gpu.exe');start='199932000019059617039';end='199932000019059617463';length='424';queued_intent=('b'*32);
  engine_upgrade=@{policy=$policy;qualification_sha256=$qualification};
  pins=@(@{path=($sdk+'/bin/pgn-continuous-gpu.exe');sha256=$image},@{path=($sdk+'/bin/cudart64_12.dll');sha256=$dll});unknown_extension='original'}
 if($v2){$bindingFixture.source_boundary_protocol=$protocol}
 $oldManifestBytes=$null;$oldExitBytes=$null;$checkpointBytes=$null
 if($resumeCase){
  # Real resume validators and held Win32 acquisition read only these own
  # synthetic files. Caller ownership/child/driver remain explicit doubles.
  Microsoft.PowerShell.Core\Import-Module (Join-Path $moduleDirectory 'checkpoint-resume-binding.psm1') -Force -Global
  Microsoft.PowerShell.Core\Import-Module (Join-Path $moduleDirectory 'source-stop-checkpoint-evidence.psm1') -Force -Global
  $window=[Numerics.BigInteger]::Pow(2,64)
  $first=[Numerics.BigInteger]::Parse('199932000000000000000');$length=[uint64]1000000000000000;$positions=[uint64]193273528320
  if($resumeCase -ceq 'geometry-refusal'){
   $first=10*$window+($window-1)-3*[Numerics.BigInteger]1006632960-25;$length=[uint64]100;$positions=[uint64]30
  }
  if($resumeCase -ceq 'floor-recovery'){
   # Contract discriminator only: coherent synthetic checkpoint bytes where
   # default0 geometry refuses but the exact saved20 geometry is admissible.
   # This does NOT claim that a GPU created this historical checkpoint.
   $first=10*$window;$length=[uint64]21;$positions=[uint64]20
   $bindingFixture.initial_positions='0' # Untrusted extension must not win.
  }
  $job=Join-Path $root 'current-job';$null=New-Item -ItemType Directory -Path $job
  $label=Join-Path $job 'checkpoint';$source=Join-Path $job (('a'*32)+'.stdout')
  $checkpointBytes=New-Object byte[] 64;[Array]::Copy([Text.Encoding]::ASCII.GetBytes('PGNCG001'),0,$checkpointBytes,0,8)
  $offset=8
  foreach($word in @([uint64]($first%$window),[uint64]10,$length,$positions,[uint64]9007199254740997,[uint64]41,[uint64]3)){
   [Array]::Copy([BitConverter]::GetBytes([uint64]$word),0,$checkpointBytes,$offset,8);$offset+=8
  }
  $sha=[Security.Cryptography.SHA256]::Create();try{$digest=([BitConverter]::ToString($sha.ComputeHash($checkpointBytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  $booking='11111111-2222-4333-8444-555555555555';$request='aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee'
  $bindingFixture.start=$first.ToString();$bindingFixture.end=($first+$length).ToString();$bindingFixture.length=$length.ToString();$bindingFixture.queued_intent=$booking
  $bindingFixture.test_scope=@{order='ascending';start=(10*$window).ToString();end=(11*$window).ToString()}
  $resume=@{run=('a'*32);start=$bindingFixture.start;end=$bindingFixture.end;length=$bindingFixture.length;positions=$positions.ToString();nextStart=($first+$positions).ToString();remaining=([Numerics.BigInteger]$length-$positions).ToString();tests='9007199254740997';gaps='41';batches='3';counterTimeNs='123456789000';checkpointLabel=$label;checkpointSha256=$digest}
  $admission=@{schema='pgn-checkpoint-resume-admission-1';receipt=@{schema='pgn-checkpoint-resume-epoch-1';origin='RESUME_CHECKPOINT';request_id=$request;device=$bindingFixture.uuid;booking_id=$booking;previous_run=('a'*32);run=('b'*32);observation_id='12345678-1234-4567-89ab-123456789abc';predecessor=$null;counter_basis='PGNCG001_CUMULATIVE_BASELINE';raw_conservation_requirement='LINEAGE_RAW_CONSERVATION_REQUIRED';checkpoint=@{schema='pgn-source-resume-1';device=$bindingFixture.uuid.ToLowerInvariant();resume=$resume;checkpoint_base64=[Convert]::ToBase64String($checkpointBytes)}};base=@{base_run=('a'*32);base_birth='2026-10-14T12:00:00.1234567Z';base_start=$bindingFixture.start;base_end=$bindingFixture.end}}|ConvertTo-Json -Depth 20 -Compress|ConvertFrom-Json
  $oldManifest=@{schema='pgn-native-process-1';run=('a'*32);birth=$admission.base.base_birth;uuid=$bindingFixture.uuid;start=$bindingFixture.start;end=$bindingFixture.end;queued_intent=$booking;stdout=$source;stop=(Join-Path $job 'stop.request')}|ConvertTo-Json -Compress|ConvertFrom-Json
  $oldExit=@{run=('a'*32);exit_code=0}|ConvertTo-Json -Compress|ConvertFrom-Json
  $oldManifestBytes=[Text.UTF8Encoding]::new($false).GetBytes(($oldManifest|ConvertTo-Json -Compress));[IO.File]::WriteAllBytes($manifestPath,$oldManifestBytes)
  $oldExitBytes=[Text.UTF8Encoding]::new($false).GetBytes(($oldExit|ConvertTo-Json -Compress));[IO.File]::WriteAllBytes((Join-Path $root 'last-exit.json'),$oldExitBytes)
  [IO.File]::WriteAllBytes($label,$checkpointBytes)
  [IO.File]::WriteAllText($source,"[pgn-counters] time_ns=123456789000 positions=$positions gaps=41 tests=9007199254740997 batches=3 processed_gaps=41`n[phase0-gpu] stopped positions=$positions checkpoint=$label`n",[Text.UTF8Encoding]::new($false))
  $captured=Get-PgnStoppedCheckpointEvidence -Root $root -Manifest $oldManifest -LastExit $oldExit -Owned $false -ExpectedRun ('a'*32)
  Check ($captured.status -ceq 'captured') 'real held acquisition of own exact64/footer/metadata'
  $bindingFixture=$bindingFixture|ConvertTo-Json -Depth 10 -Compress|ConvertFrom-Json
  if($resumeCase -ceq 'geometry-refusal'){
   $blocked=$false
   try{Get-PgnCheckpointResumeBinding $bindingFixture $oldManifest $oldExit $false $captured $admission|Out-Null}catch{if($_.Exception.Message -notmatch 'Source window/descriptor bound'){throw};$blocked=$true}
   Check $blocked 'actual resume admission now rejects saved-position geometry'
   # Reproduce a STAGED intent admitted by the previous gate (baseline v2),
   # so the worker must also reject it after a controller/module update.
   # This is synthetic old metadata, not a new admission or physical claim.
   $r=$admission.receipt
   $bindingFixture|Add-Member -NotePropertyName resume_start -NotePropertyValue ([pscustomobject]@{schema='pgn-checkpoint-resume-marker-1';request_id=$r.request_id;observation_id=$r.observation_id;previous_run=$r.previous_run;run=$r.run;checkpoint_sha256=$resume.checkpointSha256;counter_basis=$r.counter_basis;raw_conservation_requirement=$r.raw_conservation_requirement})
  }else{$bindingFixture=Get-PgnCheckpointResumeBinding $bindingFixture $oldManifest $oldExit $false $captured $admission}
  $intent=@{schema='pgn-checkpoint-resume-intent-1';state='STAGED_START_PENDING';request_id=$request;admission=$admission}
  if($resumeCase -cne 'geometry-refusal'){
   $validated=Get-PgnCheckpointResumeWorkerState $bindingFixture $oldManifest $oldExit $false $captured $intent
   Check ($validated.run -ceq ('b'*32) -and $validated.initial_positions -ceq $positions.ToString() -and ($validated.PSObject.Properties.Name|Sort-Object) -join ',' -ceq 'initial_positions,run') 'worker state copies only validated reservedrun and exact checkpoint positions'
   Check ((Get-PgnCheckpointResumeWorkerEpoch $bindingFixture $oldManifest $oldExit $false $captured $intent) -ceq ('b'*32)) 'old public epoch API remains the exact run string'
  }
  [IO.File]::WriteAllText((Join-Path $root 'checkpoint-resume-intent.json'),($intent|ConvertTo-Json -Depth 20 -Compress),[Text.UTF8Encoding]::new($false))
  if($resumeCase -ceq 'corrupt-checkpoint'){$badBytes=$checkpointBytes.Clone();$badBytes[32]=$badBytes[32] -bxor 1;[IO.File]::WriteAllBytes($label,$badBytes);$checkpointBytes=$badBytes}
 }
 [IO.File]::WriteAllText($bindingPath,($bindingFixture|ConvertTo-Json -Depth 8 -Compress),[Text.UTF8Encoding]::new($false))
 . $script:readDefinition
 . $script:writeDefinition
 . $script:evidenceDefinition
 . $script:drainDefinition
 . $script:memoryEnterDefinition
 . $script:memoryExitDefinition
 function GateHeld {
  $f=$null
  try{$f=[IO.File]::Open($transitionPath,'OpenOrCreate','ReadWrite','None');return $false}catch [IO.IOException]{return $true}finally{if($f){$f.Dispose()}}
 }
 function EnterControlTransition { $trace.Add('enter');return [IO.File]::Open($transitionPath,'OpenOrCreate','ReadWrite','None') }
 function Owner($manifest){Check (($resumeCase -and $manifest.run -ceq ('a'*32)) -or (!$resumeCase -and $null -eq $manifest)) 'private fixture has no physically owned process';return $false}
 function Import-Module {
  param([string]$Name,[switch]$Force,[string]$ErrorAction)
  $leaf=[IO.Path]::GetFileName($Name)
  Check ($leaf -cin @('run-source-profile.psm1','checkpoint-resume-binding.psm1','source-stop-checkpoint-evidence.psm1')) 'only exact existing capture/resume/profile helper import'
  Microsoft.PowerShell.Core\Import-Module (Join-Path $moduleDirectory $leaf) -Force -Global -ErrorAction Stop
 }
 function Get-FileHash {
  param([string]$LiteralPath,[string]$Algorithm)
  Check (GateHeld) 'hash under actual private transition lock'
  Check ($Algorithm -ceq 'SHA256') 'actual SHA256 request'
  $calls.hash++;$trace.Add('hash')
  if($LiteralPath -ceq ($sdk+'/bin/pgn-continuous-gpu.exe')){return [pscustomobject]@{Hash=$(if($badHash){'f'*64}else{$image})}}
  if($LiteralPath -ceq ($sdk+'/bin/cudart64_12.dll')){return [pscustomobject]@{Hash=$dll}}
  throw 'Unexpected hash path; no real file hash delegated'
 }
 function nvidia-smi.exe {
  Check (GateHeld) 'fake driver inventory under transition'
  $calls.driver++;$global:LASTEXITCODE=0
  # PS5 binds comma tokens to arrays for a function; the real native-command
  # call serializes these same tokens as comma-separated arguments.
  $nativeArgs=@($args|ForEach-Object {if($_ -is [array]){$_ -join ','}else{[string]$_}})
  if($nativeArgs -contains '--query-gpu=uuid,pci.bus_id'){return 'GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac, 00000000:01:00.0'}
  if($nativeArgs -contains '--query-compute-apps=gpu_uuid,pid'){return}
  throw 'Unexpected fake driver query'
 }
 function WriteJson($p,$o){
  if($p -ceq $manifestPath){
   Check (GateHeld) 'first manifest serialized before transition release'
   Check ($calls.launch -eq 1 -and $calls.publish -eq 0) 'one publication after one fake launch'
   $calls.publish++;$trace.Add('publish')
  }
  InvokeSourceWriteJson $p $o
 }
 function Start-Process {
  param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
  Check (GateHeld) 'fake launch under actual transition'
  Check ($calls.hash -eq 2 -and $calls.driver -eq 2) 'both pin checks and driver observations precede fake launch'
  Check ($FilePath -ceq $bindingFixture.executable -and $WindowStyle -ceq 'Hidden' -and $PassThru) 'same exact launch executable'
  Check (($ArgumentList -join '|').Contains(('--start|'+$bindingFixture.start+'|--length|'+$bindingFixture.length+'|'))) 'same exact original launch range, not resumed suffix'
  Check (($resumeCase -and (ReadJson $manifestPath).run -ceq ('a'*32)) -or (!$resumeCase -and !(Test-Path -LiteralPath $manifestPath))) 'old process manifest is not overwritten before launch result'
  $calls.launch++;$trace.Add('launch')
  # Simulate a later published binding, not a replacement of the worker's DTO.
  # A wrong post-launch profile lookup would now see an unknown image.
  $later=ReadJson $bindingPath;$later.executable='C:/NOT-LAUNCHED/later.exe';$later.unknown_extension='later'
  InvokeSourceWriteJson $bindingPath $later
  $p=[pscustomobject]@{Id=4242;StartTime=[DateTime]::Parse('2026-10-15T00:00:00.0000000Z').ToUniversalTime();Handle=[IntPtr]1;ExitCode=0}
  $p|Add-Member ScriptMethod WaitForExit {
   Check (!(GateHeld)) 'transition released before fake child wait'
   $calls.wait++;$trace.Add('wait')
   $m=ReadJson $manifestPath
   Check ($m.source_profile.schema -ceq 'pgn-run-source-profile-1' -and $m.source_profile.protocol -ceq $protocol) 'actual first JSON carries captured exact profile protocol'
   Check ($m.source_profile.image_sha256 -ceq $image.ToLowerInvariant() -and $m.source_profile.executable -ceq $bindingFixture.executable) 'captured launch tuple not later binding'
   Check (($m.source_profile.PSObject.Properties.Name|Sort-Object) -join ',' -ceq 'executable,image_sha256,protocol,schema') 'actual JSON exact profile shape'
   Check ($m.run -cmatch '^[0-9a-f]{32}$' -and $m.start -ceq $bindingFixture.start -and $m.end -ceq $bindingFixture.end -and $m.queued_intent -ceq $bindingFixture.queued_intent) 'profile attached to this actual worker epoch and range'
   Check ((ReadJson $bindingPath).executable -ceq 'C:/NOT-LAUNCHED/later.exe') 'later binding really differs'
   if($resumeCase){Check ($m.run -ceq ('b'*32) -and $m.resume_previous_run -ceq ('a'*32) -and $m.checkpoint_input_sha256 -ceq $digest) 'reserved newrun/checkpoint lineage survives actual manifest publication'}
  }
  return $p
 }
 $oldPath=$env:PATH;$oldVisible=$env:CUDA_VISIBLE_DEVICES;$oldOrder=$env:CUDA_DEVICE_ORDER;$oldTrace=$env:PGN_STAGE_TRACE
 $failed=$false;$errorText=''
 try{& $script:workerPath}catch{$failed=$true;$errorText=$_.Exception.Message}finally{
  $env:PATH=$oldPath;$env:CUDA_VISIBLE_DEVICES=$oldVisible;$env:CUDA_DEVICE_ORDER=$oldOrder;$env:PGN_STAGE_TRACE=$oldTrace
 }
 Check (!(GateHeld)) 'transition released after actual worker finally'
 $probe=[IO.File]::Open((Join-Path $root 'start.lock'),'Open','ReadWrite','None');$probe.Dispose()
 if($badHash -or $resumeCase -cin @('geometry-refusal','corrupt-checkpoint')){
  Check $failed 'admission failure surfaced from actual worker'
  Check ($calls.launch -eq 0 -and $calls.publish -eq 0 -and $calls.wait -eq 0 -and $calls.driver -eq 0) 'pin refusal before driver/launch/publication'
  if($resumeCase){
   Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($manifestPath)) -ceq [Convert]::ToBase64String($oldManifestBytes) -and [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $root 'last-exit.json'))) -ceq [Convert]::ToBase64String($oldExitBytes)) 'resume refusal preserves old process/exit metadata bytes'
   Check ((ReadJson (Join-Path $root 'checkpoint-resume-intent.json')).state -ceq 'STAGED_START_PENDING') 'refusal preserves unconsumed resume intent'
  }else{Check (!(Test-Path -LiteralPath $manifestPath) -and !(Test-Path -LiteralPath (Join-Path $root 'last-exit.json'))) 'refusal invents no process epoch/exit'}
  $expectedError=if($badHash){'pin mismatch'}elseif($resumeCase -ceq 'geometry-refusal'){'Source window/descriptor bound'}else{'Fresh resume checkpoint differs'}
  Check ((ReadJson (Join-Path $root 'last-error.json')).error -match $expectedError) ('actual bounded worker failure record; outer failure: '+$errorText)
 }else{
  Check (!$failed) ('actual worker successful closure: '+$errorText)
  Check ($calls.launch -eq 1 -and $calls.publish -eq 1 -and $calls.wait -eq 1) 'one actual worker fake launch/publication/wait'
  $m=ReadJson $manifestPath;$exitRecord=ReadJson (Join-Path $root 'last-exit.json')
  Check ($exitRecord.run -ceq $m.run -and $exitRecord.exit_code -eq 0) 'actual exit record preserves same fake epoch'
 }
 if($resumeCase){Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes($label)) -ceq [Convert]::ToBase64String($checkpointBytes)) 'worker geometry admission never rewrites checkpoint bytes'}
 [IO.File]::WriteAllText((Join-Path $root 'test-observation.json'),(@{fake_only=$true;calls=$calls;trace=@($trace.ToArray());failed=$failed;error=$errorText}|ConvertTo-Json -Depth 5 -Compress),[Text.UTF8Encoding]::new($false))
}
if($Cases -ceq 'all'){
 InvokeCase 'positive' $false
 InvokeCase 'hash-refusal' $true
 InvokeCase 'resume-geometry-refusal' $false 'geometry-refusal'
 InvokeCase 'resume-positive' $false 'positive'
 InvokeCase 'resume-floor-recovery' $false 'floor-recovery'
 InvokeCase 'resume-corrupt-checkpoint' $false 'corrupt-checkpoint'
}
InvokeCase 'v2-initial' $false '' $true
InvokeCase 'v2-resume' $false 'positive' $true
Write-Output "PASS checks=$($testState.checks) failures=0; actual worker AST statements/JSON/locks, fake driver/hash/child only; no installed paths opened, no worker child/GPU/SSH"
