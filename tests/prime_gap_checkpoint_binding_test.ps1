param([Parameter(Mandatory=$true)][string]$PinFixture,[Parameter(Mandatory=$true)][string]$RuntimeRoot)
$ErrorActionPreference='Stop';$script:checks=0
function Check([bool]$Value,[string]$Message){++$script:checks;if(!$Value){throw $Message}}
Import-Module (Join-Path $PSScriptRoot '../tools/prime-gap-engine.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/configured-stop-binding.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/source-stop-checkpoint-evidence.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/checkpoint-resume-binding.psm1') -Force
$binding=Get-Content -LiteralPath $PinFixture -Raw|ConvertFrom-Json
$capacity=& (Get-Module configured-stop-binding) {param($value);Get-PgnConfiguredSourceCapacity $value} $binding
Check ($capacity.schema-ceq'pgn-prime-gap-source-capacity-1'-and$capacity.job.minimum_gap-ceq'1332') 'Existing configured capacity did not bind engine'
$profile=Get-PgnPrimeGapRunSourceProfile -Binding $binding -HashFile {param($path);return @($binding.pins|Where-Object path -ceq $path)[0].sha256}
$job=Get-PgnPrimeGapProfileJob -Profile $profile -Start $binding.start -End $binding.end
$slot=[Numerics.BigInteger]$job.profile_k*1006632960;$position=$slot-([Numerics.BigInteger]::Parse($job.start)%30)
$bytes=[Collections.Generic.List[byte]]::new();$bytes.AddRange([Text.Encoding]::ASCII.GetBytes('PGNS'));$bytes.AddRange([BitConverter]::GetBytes([uint32]$job.minimum_gap))
foreach($word in @($job.low_word,$job.high_word,$job.length,$position.ToString(),'1234','0','1')){$bytes.AddRange([BitConverter]::GetBytes([uint64]::Parse($word)))}
$checkpoint=$bytes.ToArray();Assert-PgnPrimeGapCheckpoint -Bytes $checkpoint -Job $job
$changed=[byte[]]$checkpoint.Clone();$changed[4]=$changed[4]-bxor1;$rejected=$false;try{Assert-PgnPrimeGapCheckpoint -Bytes $changed -Job $job}catch{$rejected=$true};Check $rejected 'Changed checkpoint threshold was accepted'
Check (!(Test-Path -LiteralPath $RuntimeRoot)) 'Test must own a fresh root'
$null=New-Item -ItemType Directory -Path (Join-Path $RuntimeRoot 'current-job') -Force
$old='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';$next='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
$booking='aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee';$request='11111111-2222-4333-8444-555555555555';$observation='12345678-1234-4567-89ab-123456789abc'
$label=Join-Path $RuntimeRoot 'current-job/checkpoint';$stdout=Join-Path $RuntimeRoot ('current-job/'+$old+'.stdout');$stop=Join-Path $RuntimeRoot 'current-job/stop.request'
$birth='2026-10-30T00:00:00.0000000Z'
$manifest=[pscustomobject]@{schema='pgn-native-process-1';run=$old;uuid=$binding.uuid;start=$binding.start;end=$binding.end;birth=$birth;pid=123;queued_intent=$booking;stdout=$stdout;stop=$stop;source_profile=$profile}
$exit=[pscustomobject]@{run=$old;exit_code=0}
[IO.File]::WriteAllText((Join-Path $RuntimeRoot 'process.json'),($manifest|ConvertTo-Json -Depth 8 -Compress),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $RuntimeRoot 'last-exit.json'),($exit|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllBytes($label,$checkpoint)
$tail='[pgn-counters] time_ns=1000 positions='+$position.ToString()+' gaps=0 tests=1234 batches=1 processed_gaps=0 emitted_gaps=0'+"`n"+'[phase0-gpu] stopped positions='+$position.ToString()+' checkpoint='+$label+"`n"
[IO.File]::WriteAllText($stdout,$tail,[Text.UTF8Encoding]::new($false))
$captured=Get-PgnStoppedCheckpointEvidence -Root $RuntimeRoot -Manifest $manifest -LastExit $exit -Owned $false -ExpectedRun $old
Check ($captured.status-ceq'captured') ('Held actual-file capture failed: '+$captured.error)
$sha=[Security.Cryptography.SHA256]::Create();try{$digest=([BitConverter]::ToString($sha.ComputeHash($checkpoint))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
$resume=[pscustomobject]@{run=$old;start=$job.start;end=$job.end;length=$job.length;positions=$position.ToString();nextStart=([Numerics.BigInteger]::Parse($job.start)+$position).ToString();remaining=([Numerics.BigInteger]::Parse($job.length)-$position).ToString();tests='1234';gaps='0';batches='1';counterTimeNs='1000';checkpointLabel=$label;checkpointSha256=$digest}
$receipt=[pscustomobject]@{schema='pgn-checkpoint-resume-epoch-1';origin='RESUME_CHECKPOINT';request_id=$request;device=$binding.uuid;booking_id=$booking;previous_run=$old;run=$next;observation_id=$observation;predecessor=$null;counter_basis='PGNS_GAP32_64B_CUMULATIVE_BASELINE';raw_conservation_requirement='LINEAGE_RAW_CONSERVATION_REQUIRED';checkpoint=[pscustomobject]@{schema='pgn-source-resume-1';device=$binding.uuid.ToLowerInvariant();resume=$resume;checkpoint_base64=[Convert]::ToBase64String($checkpoint)}}
$admission=[pscustomobject]@{schema='pgn-checkpoint-resume-admission-1';receipt=$receipt;base=[pscustomobject]@{base_run=$old;base_birth=$birth;base_start=$job.start;base_end=$job.end}}
$binding|Add-Member -NotePropertyName queued_intent -NotePropertyValue $booking
$binding|Add-Member -NotePropertyName test_scope -NotePropertyValue ([pscustomobject]@{order='ascending';start=$job.start;end=$job.end})
$bound=Get-PgnCheckpointResumeBinding $binding $manifest $exit $false $captured $admission
Check ($bound.resume_start.counter_basis-ceq'PGNS_GAP32_64B_CUMULATIVE_BASELINE'-and$bound.resume_start.run-ceq$next) 'Existing PGN resume did not preserve exact engine checkpoint'
$binding.engine.minimum_gap=1334
$rejected=$false;try{$null=Get-PgnCheckpointResumeBinding $binding $manifest $exit $false $captured $admission}catch{$rejected=$true};Check $rejected 'Resume changed pinned engine threshold'
Write-Output ('Existing PGN native checkpoint capture/binding PASS checks='+$script:checks+'; real held fixture files, no process authentication/GPU/driver/control')
