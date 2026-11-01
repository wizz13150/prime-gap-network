$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/configured-stop-binding.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/observed-stop-binding.psm1') -Force
$script:checks=0;$script:skips=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw "CHECK $script:checks $why"}}
function Refused([scriptblock]$body,[string]$why){$failed=$false;try{& $body|Out-Null}catch{$failed=$true};Check $failed $why}
$root=Join-Path ([IO.Path]::GetTempPath()) ('pgn-configured-stop-TESTONLY-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root -ErrorAction Stop
$utf8=[Text.UTF8Encoding]::new($false)
$script:hashCalls=0;$script:writes=0
$hash={param($p);$script:hashCalls++;(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
$write={param($p,$o);$script:writes++;[IO.File]::WriteAllText($p,($o|ConvertTo-Json -Depth 8 -Compress),$utf8)}
$image=Join-Path $root "PUBLIC-'TESTONLY.exe";$extra=Join-Path $root 'PUBLIC-extra.bin'
[IO.File]::WriteAllText($image,'PUBLIC NOT EXECUTABLE',$utf8);[IO.File]::WriteAllText($extra,'PUBLIC NOT ENGINE DATA',$utf8)
$bindingPath=Join-Path $root 'binding.json';$stopPath=Join-Path $root 'operator-stop.json';$manifestPath=Join-Path $root 'process.json'
$device='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';$request='11111111-2222-4333-8444-555555555555'
function Binding {
 return @{uuid=$device;executable=$image;pins=@(@{path=$image;sha256=(Get-FileHash -LiteralPath $image).Hash},@{path=$extra;sha256=(Get-FileHash -LiteralPath $extra).Hash});unknown_extension=@{retained='PUBLIC-TESTONLY'};start='184467440737095516260';end='184467440737096516260'}
}
function PutBinding($b){[IO.File]::WriteAllText($bindingPath,($b|ConvertTo-Json -Depth 8 -Compress),$utf8)}
function Args($observed){return @{RuntimeRoot=$root;RequestId=$request;Reason='AppClose';Protocol=$observed.protocol;ExpectedDevice=$observed.profile.device;ExpectedExecutable=$observed.profile.executable;ExpectedImageSha256=$observed.profile.image_sha256;ExpectedBindingSha256=$observed.profile.binding_sha256;HashFile=$hash;WriteJson=$write}}
try{
 PutBinding (Binding);$beforeBinding=(Get-FileHash -LiteralPath $bindingPath).Hash
 $observed=Get-PgnConfiguredProfile -RuntimeRoot $root -HashFile $hash
 Check ($observed.schema -ceq 'pgn-configured-profile-observation-1' -and $observed.protocol -ceq 'pgn-configured-stop-1') 'typed distinct profile acquisition'
 Check ($observed.status -ceq 'CONFIGURED_PROFILE_OBSERVED' -and !$observed.ContainsKey('owned') -and !$observed.ContainsKey('state')) 'no physical OFF sample invented'
 Check ($observed.profile.binding_sha256 -ceq $beforeBinding -and $script:hashCalls -eq 2) 'actual bounded raw binding digest and all pins checked'
 Check ($null -eq $observed.source_capacity -and $observed.binding.unknown_extension.retained -ceq 'PUBLIC-TESTONLY') 'unqualified own fake SDK remains capacity-unknown and whole binding survives'
 Check (!(Test-Path -LiteralPath $manifestPath) -and !(Test-Path -LiteralPath $stopPath) -and $script:writes -eq 0) 'null manifest acquisition neither creates epoch nor writes STOP'
 $a=Args $observed;$result=Invoke-PgnConfiguredStop @a
 Check ($result.status -ceq 'CONFIGURED_OFF_LATCHED' -and $result.physical_applied -is [bool] -and !$result.physical_applied) 'latch report not stop ACK'
 $marker=Get-Content -LiteralPath $stopPath -Raw|ConvertFrom-Json
 Check ($marker.stop -eq $true -and $marker.request_id -ceq $request -and $marker.reason -ceq 'AppClose') 'actual desired-OFF marker retained'
 Check ((Get-FileHash -LiteralPath $bindingPath).Hash -ceq $beforeBinding -and !(Test-Path -LiteralPath (Join-Path $root 'current-job'))) 'no binding/stopfile/job/process rewritten'
 $guard=Enter-PgnControlTransition $root 0
 try{Check ((Get-Content -LiteralPath $stopPath -Raw|ConvertFrom-Json).stop -eq $true) 'next admitted worker sees desired-OFF under same gate'}finally{$guard.Dispose()}
 # Stale/contradictory process data must NEVER be inspected or normalized by
 # this profile-only operation. No Owner, process lookup, signal or task API.
 [IO.File]::WriteAllText($manifestPath,'MALFORMED OLD PROCESS DATA MUST REMAIN OPAQUE',$utf8)
 $beforeManifest=(Get-FileHash -LiteralPath $manifestPath).Hash;$a.Reason='CardOff';$result=Invoke-PgnConfiguredStop @a
 Check ($result.reason -ceq 'CardOff' -and (Get-FileHash -LiteralPath $manifestPath).Hash -ceq $beforeManifest) 'old epoch cannot retarget latch or become a PID signal'
 $previousStop=(Get-FileHash -LiteralPath $stopPath).Hash;$previousWrites=$script:writes
 foreach($delta in @(@{RequestId='NOT-UUID'},@{RequestId=$request.ToUpperInvariant()},@{Reason='automatic'},@{Protocol='legacy-stop'},@{ExpectedDevice='GPU-OTHER'},@{ExpectedImageSha256=('B'*64)},@{ExpectedBindingSha256=('C'*64)},@{ExpectedExecutable='relative.exe'})){
  # This request contains only digits; an upper-case UUID mutation needs an
  # actually alphabetic UUID, not an unchanged control fixture.
  if($delta.ContainsKey('RequestId') -and $delta.RequestId -ceq $request){$delta.RequestId='AAAAAAAA-BBBB-4CCC-8DDD-EEEEEEEEEEEE'}
  $bad=Args $observed;foreach($key in $delta.Keys){$bad[$key]=$delta[$key]};Refused {Invoke-PgnConfiguredStop @bad} 'bad exact correlation/capability/profile refused'
 }
 Check ($script:writes -eq $previousWrites -and (Get-FileHash -LiteralPath $stopPath).Hash -ceq $previousStop) 'all rejected commands preserve existing latch bytes'
 $changed=Binding;$changed.start='184467440737095516261';PutBinding $changed
 Refused {Invoke-PgnConfiguredStop @a} 'full raw binding change since acquisition refused before marker'
 Check ($script:writes -eq $previousWrites) 'no partial write on target change'
 PutBinding (Binding);[IO.File]::AppendAllText($extra,'CHANGED',$utf8)
 Refused {Invoke-PgnConfiguredStop @a} 'non-executable pin hash change is also refused'
 Check ($script:writes -eq $previousWrites) 'no partial latch on auxiliary pin change'
 PutBinding (Binding);$fresh=Get-PgnConfiguredProfile $root $hash;$freshArgs=Args $fresh
 $guard=Enter-PgnControlTransition $root 0
 try{$freshArgs.TransitionTimeoutMs=0;Refused {Invoke-PgnConfiguredStop @freshArgs} 'launch transition cannot be bypassed'}finally{$guard.Dispose()}
 Check ($script:writes -eq $previousWrites) 'busy transition preserves latch'
 foreach($poison in @('null','[]','{}','{"uuid":"GPU-OTHER"}',('x'*32769))){[IO.File]::WriteAllText($bindingPath,$poison,$utf8);Refused {Get-PgnConfiguredProfile $root $hash} 'absent/wrong/oversized binding held'}
 [IO.File]::WriteAllBytes($bindingPath,[byte[]]@(0xff));Refused {Get-PgnConfiguredProfile $root $hash} 'invalid UTF8 refused'
 $bad=Binding;$bad.pins+=@($bad.pins[0]);PutBinding $bad;Refused {Get-PgnConfiguredProfile $root $hash} 'duplicate pin paths refused'
 $bad=Binding;$bad.pins=@($bad.pins[1]);PutBinding $bad;Refused {Get-PgnConfiguredProfile $root $hash} 'missing executable pin refused'
 $bad=Binding;$bad.pins[0].path=$root;PutBinding $bad;Refused {Get-PgnConfiguredProfile $root $hash} 'directory cannot be an image pin'
 Check ($script:writes -eq $previousWrites -and (Get-FileHash -LiteralPath $manifestPath).Hash -ceq $beforeManifest) 'all profile poison paths preserve process and marker'
 # PRIVATE pure guard adapter test only: known declarations are synthetic,
 # never hashed/opened as installed SDK files. Public acquisition above can
 # publish capacity only AFTER every real pin has passed ReadBinding.
 $sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
 $known=@{sdk=$sdk;executable=$sdk+'/bin/pgn-continuous-gpu.exe';engine_upgrade=@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'};pins=@(@{path=$sdk+'/bin/pgn-continuous-gpu.exe';sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},@{path=$sdk+'/bin/cudart64_12.dll';sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'})}
 $cap=& (Get-Module configured-stop-binding) {param($b);Get-PgnConfiguredSourceCapacity $b} $known
 Check ($cap.schema -ceq 'pgn-qualified-source-capacity-1' -and $cap.max_length -ceq '1000000000000000') 'private pure adapter inherits existing qualified guard budget'
 Check ($cap.scope_start -ceq '184467440737095516160' -and $cap.scope_end -ceq '202914184810805067776') 'qualified high10 scope exact, no caller test_scope inference'
 $known.sdk='C:/OTHER';$cap=& (Get-Module configured-stop-binding) {param($b);Get-PgnConfiguredSourceCapacity $b} $known
 Check ($null -eq $cap) 'wrong SDK never acquires qualified capacity from policy label alone'
 $source=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/native-control.ps1'))
 Check ($source.IndexOf("if(`$Action -eq 'configured-profile' -or `$Action -eq 'stop-configured')") -lt $source.IndexOf('function Owner(')) 'profile action bypasses owner inspection, not its guards'
 $worker=$source.Substring($source.IndexOf("if(`$Action -eq 'worker')"));$worker=$worker.Substring(0,$worker.IndexOf("if(`$Action -eq 'stop')"))
 Check ($worker.IndexOf('$workerTransition=EnterControlTransition') -lt $worker.IndexOf("if((ReadJson (Join-Path `$root 'operator-stop.json')).stop){exit}")) 'source contract: shared transition before worker STOP read'
 Check ($worker.IndexOf("if((ReadJson (Join-Path `$root 'operator-stop.json')).stop){exit}") -lt $worker.IndexOf('$p=Start-Process')) 'source contract: worker refuses desired-OFF before child launch'
 $tokens=$null;$parseErrors=$null;[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../tools/native-control.ps1'),[ref]$tokens,[ref]$parseErrors)
 Check ($parseErrors.Count -eq 0) 'actual controller syntax compiled only, never invoked'
 Write-Output "CONFIGURED_STOP_BINDING $script:checks checks / 0 failures; own temp profile only; no process/GPU/SSH/TaskScheduler; worker guard source-contract only"
}finally{
 $cleanup=[IO.Path]::GetFullPath($root);$expectedPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar+'pgn-configured-stop-TESTONLY-'
 if(!$cleanup.StartsWith($expectedPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Refuse cleanup outside this test unique temporary prefix'}
 Remove-Item -LiteralPath $cleanup -Recurse -Force
}
