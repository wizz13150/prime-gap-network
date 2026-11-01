param([string]$FixtureDirectory=(Join-Path $PSScriptRoot '../evidence/root554-native-resume-caller-contract-v1'))
$ErrorActionPreference='Stop'
if((Split-Path $FixtureDirectory -Leaf) -cnotmatch '\Aroot554-native-resume-caller-contract-v[1-9][0-9]*\z'){throw 'Explicit closed native caller fixture required'}
Import-Module (Join-Path $PSScriptRoot '../tools/checkpoint-resume-binding.psm1') -Force -ErrorAction Stop
$script:checks=0
function Check($Ok,[string]$Message){++$script:checks;if(!$Ok){throw $Message}}
function Reject($Operation,[string]$Message){$bad=$false;try{& $Operation|Out-Null}catch{$bad=$true};Check $bad $Message}
function Clone($Value){$Value|ConvertTo-Json -Depth 24 -Compress|ConvertFrom-Json}
$utf8=[Text.UTF8Encoding]::new($false,$true)
$artifacts=@('native-bookings.sqlite','command.json','receipt.json','snapshot.json','old-source.stdout','fixture.json','admission.base64')
$limits=@(1048576,16384,24576,98304,65536,16384,10924)
$hashes=@{}
for($index=0;$index -lt $artifacts.Count;++$index){
 $file=Join-Path $FixtureDirectory $artifacts[$index];$item=Get-Item -LiteralPath $file
 if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -le 0 -or $item.Length -gt $limits[$index]){throw 'Bounded regular TESTONLY artifact required'}
 $hashes[$artifacts[$index]]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
}
function ReadFixture([string]$Name){[IO.File]::ReadAllText((Join-Path $FixtureDirectory $Name),$utf8)}
try{
 $fixture=(ReadFixture 'fixture.json')|ConvertFrom-Json
 Check ($fixture.schema -ceq 'pgn-closed-native-resume-caller-contract-1' -and $fixture.scope -ceq 'TEST_ONLY_NO_RUNTIME_OR_PHYSICAL_PROOF') 'Native caller TESTONLY descriptor required'
 Check ($fixture.profile_scope -ceq 'FIXTURE_BINDING_HAS_NO_SDK_PINS_EXTERNAL_TEST_PROFILE_QUALIFICATION_REQUIRED') 'Memory-only profile adaptation must be declared'
 Check (($fixture.artifacts -join '|') -ceq ($artifacts[0..5] -join '|')) 'Exact fixed native export artifacts'
 $command=(ReadFixture 'command.json')|ConvertFrom-Json
 $receipt=(ReadFixture 'receipt.json')|ConvertFrom-Json
 $snapshot=(ReadFixture 'snapshot.json')|ConvertFrom-Json
 $wire=ReadFixture 'admission.base64'
 $admission=Read-PgnCheckpointResumeAdmission $wire
 $tuple=$admission.receipt.checkpoint.resume
 Check (($admission.receipt|ConvertTo-Json -Depth 24 -Compress) -ceq ($receipt|ConvertTo-Json -Depth 24 -Compress)) 'PS decoder retains the exact native/Node receipt'
 Check ($admission.receipt.request_id -ceq $command.request_id -and $admission.receipt.observation_id -ceq $command.observation_id -and $admission.receipt.booking_id -ceq $command.booking_id -and $admission.receipt.device -ceq $command.device) 'Native command and receipt correlation'
 Check ($admission.receipt.run -ceq $command.new_run -and $admission.receipt.previous_run -ceq $command.old_run -and $command.new_run -cne $command.old_run) 'Reserved new run, not an autonomous worker GUID'
 Check ($admission.base.base_birth -ceq $command.base_birth -and $admission.base.base_run -ceq $command.old_run -and $admission.base.base_start -ceq $command.base_start -and $admission.base.base_end -ceq $command.base_end) 'Native observed base survives the Node/PS boundary'
 Check ($tuple.start -ceq '184467440737095516260' -and $tuple.end -ceq '184467440737095526260' -and $tuple.length -ceq '10000' -and $tuple.positions -ceq '123' -and $tuple.tests -ceq '9007199254740993' -and $tuple.counterTimeNs -ceq '9007199254740995') 'Exact native >2^53 bounds/counters and partial baseline'
 Check ($snapshot.schema -ceq 'pgn-runtime-snapshot-1' -and $snapshot.state -ceq 'OFF' -and $snapshot.owned -is [bool] -and !$snapshot.owned -and $snapshot.operator_stop -is [bool] -and $snapshot.operator_stop -and $snapshot.has_checkpoint -is [bool] -and $snapshot.has_checkpoint) 'Actual exported stopped snapshot, not fabricated Auto permission'
 Check ($snapshot.checkpoint_resume_protocol -ceq $admission.schema -and $snapshot.manifest.pid -gt 0) 'Explicit resume protocol and process fixture'
 $source=[IO.File]::ReadAllBytes((Join-Path $FixtureDirectory 'old-source.stdout'))
 $sourceText=$utf8.GetString($source);$captured=$snapshot.checkpoint_evidence
 Check ($sourceText.StartsWith('[PUBLIC-FIXTURE]') -and $sourceText.EndsWith($captured.source_tail_utf8,[StringComparison]::Ordinal)) 'Captured tail is from synthetic immutable old-source bytes'
 Check ($snapshot.next_offset -eq $source.Length -and $snapshot.pending_bytes -eq 0) 'Exact exported old-source frontier'
 $originalSnapshot=$snapshot|ConvertTo-Json -Depth 24 -Compress
 $originalAdmission=$admission|ConvertTo-Json -Depth 24 -Compress
 $originalReceipt=$receipt|ConvertTo-Json -Depth 24 -Compress
 # The real C++ fixture intentionally has no engine/SDK pins or predecessor.
 # Its raw binding MUST fail; assigning a JSON object is not checkpoint loading.
 Reject {Get-PgnCheckpointResumeBinding $snapshot.binding $snapshot.manifest $snapshot.last_exit $snapshot.owned $captured $admission} 'Unqualified raw C++ binding is not a physical launch capability'
 $binding=Clone $snapshot.binding
 # Only TESTONLY memory is adapted. These are the exact profiles accepted by
 # Assert-PgnQualifiedSourceBinding, not new pins or edits to an installed SDK.
 $profile=Clone @{
  executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe'
  sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
  engine_upgrade=@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'}
  pins=@(@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/cudart64_12.dll';sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'})
 }
 foreach($field in $profile.PSObject.Properties){$binding|Add-Member -NotePropertyName $field.Name -NotePropertyValue $field.Value -Force}
 Reject {Get-PgnCheckpointResumeBinding $binding $snapshot.manifest $snapshot.last_exit $snapshot.owned $captured $admission} 'Qualified profile cannot invent a missing completed-predecessor relation'
 Check ($null -ne $receipt.predecessor -and $receipt.predecessor -cne $receipt.previous_run) 'Fixture retains separate native completed-predecessor relation'
 $binding|Add-Member -NotePropertyName predecessor -NotePropertyValue ([pscustomobject]@{run=$receipt.predecessor}) -Force
 $bindingBefore=$binding|ConvertTo-Json -Depth 24 -Compress
 $next=Get-PgnCheckpointResumeBinding $binding $snapshot.manifest $snapshot.last_exit $snapshot.owned $captured $admission
 Check ($next.start -ceq $tuple.start -and $next.end -ceq $tuple.end -and $next.length -ceq $tuple.length -and $next.queued_intent -ceq $receipt.booking_id) 'Binding preserves original full interval and booking, not remaining subrange'
 Check ($next.predecessor.run -ceq $receipt.predecessor -and $next.resume_start.previous_run -ceq $receipt.previous_run -and $next.resume_start.run -ceq $receipt.run) 'Completed predecessor and new process lineage remain distinct'
 Check ($next.resume_start.checkpoint_sha256 -ceq $tuple.checkpointSha256 -and $next.resume_start.counter_basis -ceq 'PGNCG001_CUMULATIVE_BASELINE' -and $next.resume_start.raw_conservation_requirement -ceq 'LINEAGE_RAW_CONSERVATION_REQUIRED') 'Checkpoint marker retains exact bytes/baseline and unpaid raw-conservation obligation'
 Check (($binding|ConvertTo-Json -Depth 24 -Compress) -ceq $bindingBefore) 'Production binding validator is nonmutating'
 $intent=Clone @{schema='pgn-checkpoint-resume-intent-1';state='STAGED_START_PENDING';request_id=$receipt.request_id;admission=$admission}
 Check ((Get-PgnCheckpointResumeWorkerEpoch $next $snapshot.manifest $snapshot.last_exit $snapshot.owned $captured $intent) -ceq $receipt.run) 'Production worker-epoch validator returns the natively reserved run'
 # Byte/counter validation is real; no test invokes a worker or claims that
 # these bytes have been loaded, resumed, source-conserved or proved on a GPU.
 $checkpoint=[Convert]::FromBase64String($receipt.checkpoint.checkpoint_base64)
 Check ($checkpoint.Length -eq 64 -and [Text.Encoding]::ASCII.GetString($checkpoint,0,8) -ceq 'PGNCG001') 'Actual native 64-byte format decoded'
 foreach($pair in @(@(24,'length'),@(32,'positions'),@(40,'tests'),@(48,'gaps'),@(56,'batches'))){Check ([BitConverter]::ToUInt64($checkpoint,[int]$pair[0]).ToString([Globalization.CultureInfo]::InvariantCulture) -ceq $tuple.($pair[1])) 'Exact cumulative checkpoint word'}
 foreach($ownership in @($true,$null,'false',0)){Reject {Get-PgnCheckpointResumeBinding $binding $snapshot.manifest $snapshot.last_exit $ownership $captured $admission} 'Unknown/active ownership cannot be admitted'}
 foreach($change in @({param($v)$v.run=$receipt.run},{param($v)$v.birth+="`n"},{param($v)$v.start=$tuple.nextStart},{param($v)$v.queued_intent=$receipt.request_id})){$bad=Clone $snapshot.manifest;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $binding $bad $snapshot.last_exit $false $captured $admission} 'Changed actual manifest is Held'}
 foreach($value in @($null,'0',$false,1)){$bad=Clone $snapshot.last_exit;$bad.exit_code=$value;Reject {Get-PgnCheckpointResumeBinding $binding $snapshot.manifest $bad $false $captured $admission} 'Unknown/nonzero/noninteger exit is Held'}
 foreach($change in @({param($v)$v.status='unavailable'},{param($v)$v.run=$receipt.run},{param($v)$v.checkpoint_base64=[Convert]::ToBase64String([byte[]]::new(64))},{param($v)$v.source_tail_utf8=$v.source_tail_utf8.Replace('tests=9007199254740993','tests=9007199254740992')},{param($v)$v.source_tail_utf8+=$v.source_tail_utf8})){$bad=Clone $captured;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $binding $snapshot.manifest $snapshot.last_exit $false $bad $admission} 'Changed captured bytes/footer/baseline is Held'}
 foreach($change in @({param($v)$v.pins[0].sha256='0'*64},{param($v)$v.sdk='C:/not-qualified'},{param($v)$v.predecessor.run=$receipt.previous_run},{param($v)$v.start=$tuple.nextStart},{param($v)$v.test_scope.start=$tuple.nextStart})){$bad=Clone $binding;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $bad $snapshot.manifest $snapshot.last_exit $false $captured $admission} 'Profile/authority/original interval cannot be relaxed'}
 foreach($state in @('PREPARED','LAUNCH_COMMITTED_UNCERTAIN','OWNED_PROCESS_OBSERVED','DONE',$null)){$bad=Clone $intent;$bad.state=$state;Reject {Get-PgnCheckpointResumeWorkerEpoch $next $snapshot.manifest $snapshot.last_exit $false $captured $bad} 'Only an exact staged intent yields the reserved worker epoch'}
 foreach($field in @('request_id','observation_id','run','previous_run','checkpoint_sha256','counter_basis','raw_conservation_requirement')){$bad=Clone $next;$bad.resume_start.$field='changed';Reject {Get-PgnCheckpointResumeWorkerEpoch $bad $snapshot.manifest $snapshot.last_exit $false $captured $intent} 'Changed resume marker is Held'}
 Reject {Read-PgnCheckpointResumeAdmission ($wire+"`n")} 'Wire whitespace is not native canonical base64'
 Check (($snapshot|ConvertTo-Json -Depth 24 -Compress) -ceq $originalSnapshot) 'Actual snapshot object remains unchanged including STOP and original age'
 Check (($admission|ConvertTo-Json -Depth 24 -Compress) -ceq $originalAdmission -and ($receipt|ConvertTo-Json -Depth 24 -Compress) -ceq $originalReceipt) 'Native receipt and decoded cumulative baseline remain unchanged'
}finally{
 foreach($name in $artifacts){Check ((Get-FileHash -LiteralPath (Join-Path $FixtureDirectory $name) -Algorithm SHA256).Hash -ceq $hashes[$name]) ('Immutable exported artifact: '+$name)}
}
Write-Output ("native resume caller contract checks=$script:checks failures=0; actual C++/SQLite -> Node -> production PS decoder/binding/worker-epoch validators; TESTONLY memory profile+predecessor, no checkpoint loading, launch, GPU, source-lineage or physical claim")
