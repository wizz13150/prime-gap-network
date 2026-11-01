$ErrorActionPreference='Stop'
$modulePath=[IO.Path]::GetFullPath($PSScriptRoot+'/../tools/observed-stop-binding.psm1')
Import-Module $modulePath -Force
$fixture=[IO.Path]::GetFullPath($PSScriptRoot+'/../private-qa/observed-stop-'+[guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory((Join-Path $fixture 'current-job'))
$image=Join-Path $fixture 'fixture-image.bin';[IO.File]::WriteAllBytes($image,[byte[]](1,2,3,4,5))
$sha=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
$run='0123456789abcdef0123456789abcdef';$birth='2026-10-14T12:34:56.1234567Z'
$checks=0;$events=[Collections.Generic.List[string]]::new();$ownerMode='live'
function Check([bool]$condition,[string]$label){$script:checks++;if(!$condition){throw ('FAILED '+$label)}}
function Reset-Fixture {
 $script:binding=[pscustomobject]@{uuid='fixture-public-device';executable=$image;pins=@([pscustomobject]@{path=$image;sha256=$sha})}
 $script:manifest=[pscustomobject]@{schema='pgn-native-process-1';run=$run;pid=[int]1234;birth=$birth;uuid='fixture-public-device';stop=(Join-Path $fixture 'current-job/stop.request');stdout=(Join-Path $fixture ('current-job/'+$run+'.stdout'))}
 $script:owner=[pscustomobject]@{Id=[int]1234;StartTime=[DateTime]::ParseExact($birth,'o',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind);Path=$image}
 $script:ownerMode='live';$events.Clear()
}
$reader={param($path);$events.Add('read:'+([IO.Path]::GetFileName($path)));if($path.EndsWith('binding.json')){return $binding};if($path.EndsWith('process.json')){return $manifest};throw 'Unexpected fixture read'}
$lookup={param($b,$m);$events.Add('owner');if($ownerMode -eq 'absent'){return $null};if($ownerMode -eq 'throw'){throw 'Fixture owner validation refused'};return $owner}
$writer={param($path,$value);$events.Add('write:'+([IO.Path]::GetFileName($path)));$script:written=$value;[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 5 -Compress),[Text.UTF8Encoding]::new($false))}
$hasher={param($path);$events.Add('hash');(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Invoke-Fixture {
 Invoke-PgnObservedStop -RuntimeRoot $fixture -ExpectedRun $run -ExpectedPid '1234' -ExpectedBirth $birth -ExpectedImageSha256 $sha -ReadJson $reader -OwnedProcess $lookup -WriteJson $writer -HashFile $hasher
}
function Refuses([scriptblock]$change,[string]$label){
 Reset-Fixture;& $change
 $failed=$false;try{$null=Invoke-Fixture}catch{$failed=$true}
 Check $failed $label;Check (@($events|Where-Object {$_.StartsWith('write:')}).Count -eq 0) ($label+' zero mutation')
 # Exceptions release the transition, rather than stranding every successor.
 $gate=Enter-PgnControlTransition $fixture 0;$gate.Dispose();Check $true ($label+' lock released')
}
Reset-Fixture
$result=Invoke-Fixture
Check ($result.protocol -ceq 'pgn-stop-observed-1') 'qualified protocol'
Check ($result.status -ceq 'STOP_REQUESTED' -and !$result.physical_applied) 'request is not physical stop'
Check ($result.observed.run -ceq $run -and $result.observed.pid -ceq '1234' -and $result.observed.birth -ceq $birth -and $result.observed.image_sha256 -ceq $sha) 'exact correlated tuple'
Check ([IO.File]::ReadAllText($manifest.stop) -ceq 'stop') 'actual fixture stop request bytes'
Check ($events.IndexOf('owner') -lt $events.IndexOf('hash') -and $events.IndexOf('hash') -lt $events.IndexOf('write:operator-stop.json')) 'identity then fingerprint before mutation'
Check ($written.stop -and $written.observed.run -ceq $run) 'latch has original provenance'
Reset-Fixture;$ownerMode='absent';$result=Invoke-Fixture
Check ($result.status -ceq 'ALREADY_EXITED_STOP_HELD' -and !$result.physical_applied -and $written.stop) 'closed epoch holds succession without inventing physical stop'
Refuses {$script:manifest.run='1123456789abcdef0123456789abcdef'} 'A request cannot stop B'
Refuses {$script:manifest.pid=[int]1235} 'reused manifest pid'
Refuses {$script:manifest.pid=[double]1234} 'noncanonical manifest pid type'
Refuses {$script:manifest.birth='2026-10-14T12:34:56.1234568Z'} 'changed manifest birth'
Refuses {$script:manifest.uuid='another-device'} 'other device'
Refuses {$script:binding.pins[0].sha256=('0'*64)} 'changed executable pin'
Refuses {$script:binding.pins+=@($script:binding.pins[0])} 'ambiguous executable pins'
Refuses {$script:manifest.stop=(Join-Path $fixture 'another-stop')} 'other stop path'
Refuses {$script:manifest.stdout=(Join-Path $fixture 'another-source')} 'other source path'
Refuses {$script:owner.Id=[int]1235} 'reused process pid'
Refuses {$script:owner.StartTime=$script:owner.StartTime.AddTicks(1)} 'process birth mismatch'
Refuses {$script:owner.Path=(Join-Path $fixture 'other-image.bin')} 'process image mismatch'
Refuses {$script:ownerMode='throw'} 'owner refusal'
Reset-Fixture
$badHash={param($path);$events.Add('hash');return ('0'*64)}
$failed=$false
try{$null=Invoke-PgnObservedStop -RuntimeRoot $fixture -ExpectedRun $run -ExpectedPid '1234' -ExpectedBirth $birth -ExpectedImageSha256 $sha -ReadJson $reader -OwnedProcess $lookup -WriteJson $writer -HashFile $badHash}catch{$failed=$true}
Check ($failed -and @($events|Where-Object {$_.StartsWith('write:')}).Count -eq 0) 'actual image hash refusal before all writes'
Reset-Fixture
$held=[IO.File]::Open($manifest.stop,'OpenOrCreate','Read','Read')
try{
 $failed=$false;try{$null=Invoke-Fixture}catch{$failed=$true}
 Check $failed 'stop request write failure is not success'
 Check ($written.stop -and $written.observed.run -ceq $run) 'partial failure retains exact operator stop latch'
}finally{$held.Dispose()}
$gate=Enter-PgnControlTransition $fixture 0;$gate.Dispose();Check $true 'write failure releases transition'
Reset-Fixture
foreach($pidText in @('0','01','1234.0','1e3','4294967296','-1',' 1234','1234 ')){
 $failed=$false;try{$null=Assert-PgnStopObservation $binding $manifest $run $pidText $birth $sha}catch{$failed=$true};Check $failed ('pid '+$pidText)
}
foreach($badBirth in @('2026-10-14T12:34:56Z','2026-10-14T12:34:56.1234567+00:00','2026-02-31T12:34:56.1234567Z')){
 $failed=$false;try{$null=Assert-PgnStopObservation $binding $manifest $run '1234' $badBirth $sha}catch{$failed=$true};Check $failed ('birth '+$badBirth)
}
# Actual kernel file-handle exclusion, not a boolean model of a lock. The
# worker/metadata writers participate in this same named runtime-file gate.
$first=Enter-PgnControlTransition $fixture 0
try{$failed=$false;try{$second=Enter-PgnControlTransition $fixture 0;$second.Dispose()}catch [TimeoutException]{$failed=$true};Check $failed 'second participant cannot cross transition'}finally{$first.Dispose()}
$first=Enter-PgnControlTransition $fixture 0;$first.Dispose();Check $true 'gate reusable'
Reset-Fixture
$guardedOwner={param($b,$m);$denied=$false;try{$gate=Enter-PgnControlTransition $fixture 0;$gate.Dispose()}catch [TimeoutException]{$denied=$true};Check $denied 'launch transition excluded during observed owner lookup';return $owner}
$result=Invoke-PgnObservedStop -RuntimeRoot $fixture -ExpectedRun $run -ExpectedPid '1234' -ExpectedBirth $birth -ExpectedImageSha256 $sha -ReadJson $reader -OwnedProcess $guardedOwner -WriteJson $writer -HashFile $hasher
Check ($result.status -ceq 'STOP_REQUESTED') 'guarded request completed'
# Controller source integration only: do not invoke native-control, SSH,
# scheduled workers, GPU, driver or ANY user process from this test.
$controller=[IO.Path]::GetFullPath($PSScriptRoot+'/../tools/native-control.ps1')
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($controller,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'actual controller syntax'
foreach($action in @('advance','recover','initial','resume','start','worker','stop')){
 $blocks=@($ast.FindAll({param($n);$n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses[0].Item1.Extent.Text -ceq ('$Action -eq '+[char]39+$action+[char]39)},$true))
 Check ($blocks.Count -eq 1) ('single '+$action+' block')
 $text=$blocks[0].Clauses[0].Item2.Extent.Text
 Check ($text.Contains('=EnterControlTransition') -and $text.Contains('.Dispose()')) ($action+' participates in release-safe transition')
 if($action -eq 'worker'){
  Check ($text.IndexOf('$workerTransition=EnterControlTransition') -lt $text.IndexOf('Start-Process')) 'worker gate before child launch'
  Check ($text.IndexOf('$workerTransition.Dispose();$workerTransition=$null') -lt $text.IndexOf('$p.WaitForExit()')) 'stop gate not held for GPU lifetime'
 }
}
$text=[IO.File]::ReadAllText($controller)
Check ($text.Contains("'stop-observed'") -and $text.Contains('Invoke-PgnObservedStop -RuntimeRoot')) 'controller routes typed stop without legacy fallback'
Check ($text.Contains("observed_stop_protocol='pgn-stop-observed-1'")) 'protocol advertised'
Write-Output ('Observed stop binding PASS checks='+$checks+'; own fixture only; no physical process/GPU/SSH qualification')
