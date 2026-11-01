$ErrorActionPreference='Stop'
$module=Import-Module (Join-Path $PSScriptRoot '../tools/checkpoint-resume-binding.psm1') -PassThru -Force
$script:checks=0
function Check($Ok,[string]$Message){++$script:checks;if(!$Ok){throw $Message}}
function Reject($Operation){$bad=$false;try{& $Operation|Out-Null}catch{$bad=$true};Check $bad 'Expected closed resume refusal'}
function Clone($Value){$Value|ConvertTo-Json -Depth 20 -Compress|ConvertFrom-Json}
function Encode($Value){$json=& $module {param($v) ResumeCanonical $v} $Value;[Convert]::ToBase64String([Text.UTF8Encoding]::new($false,$true).GetBytes($json))}
$device='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';$old='a'*32;$new='b'*32;$predecessor='f'*32
$booking='11111111-2222-4333-8444-555555555555';$request='aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee';$observation='12345678-1234-4567-89ab-123456789abc'
$first=[System.Numerics.BigInteger]::Parse('199932000000000000000');$length=[uint64]1000000000000000;$positions=[uint64]123456789012345;$tests=[uint64]::Parse('9007199254740997');$gaps=[uint64]41;$batches=[uint64]19
$window=[System.Numerics.BigInteger]::Pow(2,64);$bytes=[byte[]]::new(64);[Array]::Copy([Text.Encoding]::ASCII.GetBytes('PGNCG001'),0,$bytes,0,8)
$values=@([uint64]($first%$window),[uint64][System.Numerics.BigInteger]::Divide($first,$window),$length,$positions,$tests,$gaps,$batches)
$index=8;foreach($word in $values){[Array]::Copy([BitConverter]::GetBytes([uint64]$word),0,$bytes,$index,8);$index+=8}
$sha=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
$resume=Clone @{run=$old;start=$first.ToString();end=($first+$length).ToString();length=$length.ToString();positions=$positions.ToString();nextStart=($first+$positions).ToString();remaining=([System.Numerics.BigInteger]$length-$positions).ToString();tests=$tests.ToString();gaps=$gaps.ToString();batches=$batches.ToString();counterTimeNs='123456789000';checkpointLabel='C:\PGN\runtime\current-job\checkpoint';checkpointSha256=$hash}
$receipt=Clone @{schema='pgn-checkpoint-resume-epoch-1';origin='RESUME_CHECKPOINT';request_id=$request;device=$device;booking_id=$booking;previous_run=$old;run=$new;observation_id=$observation;predecessor=$predecessor;counter_basis='PGNCG001_CUMULATIVE_BASELINE';raw_conservation_requirement='LINEAGE_RAW_CONSERVATION_REQUIRED';checkpoint=@{schema='pgn-source-resume-1';device=$device.ToLowerInvariant();resume=$resume;checkpoint_base64=[Convert]::ToBase64String($bytes)}}
$admission=Clone @{schema='pgn-checkpoint-resume-admission-1';receipt=$receipt;base=@{base_run=$old;base_birth='2026-10-14T12:00:00.1234567Z';base_start=$resume.start;base_end=$resume.end}}
$binding=Clone @{uuid=$device;start=$resume.start;end=$resume.end;length=$resume.length;queued_intent=$booking;predecessor=@{run=$predecessor};executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sdk='C:/PGN/native/20261012-gpu-owned-source-v1';engine_upgrade=@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'};pins=@(@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/cudart64_12.dll';sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'});test_scope=@{order='ascending';start='199900000000000000000';end='200000000000000000000'};telemetry=@{progress_seconds=1.0;checkpoint_seconds=30.0}}
$manifest=Clone @{schema='pgn-native-process-1';run=$old;birth=$admission.base.base_birth;uuid=$device;start=$resume.start;end=$resume.end;queued_intent=$booking}
$exit=Clone @{run=$old;exit_code=0}
$tail="[pgn-counters] time_ns=$($resume.counterTimeNs) positions=$positions gaps=$gaps tests=$tests batches=$batches processed_gaps=$gaps`r`n[phase0-gpu] stopped positions=$positions checkpoint=$($resume.checkpointLabel)`r`n"
$captured=Clone @{schema='pgn-stop-checkpoint-evidence-1';status='captured';run=$old;checkpoint_label=$resume.checkpointLabel;checkpoint_base64=$receipt.checkpoint.checkpoint_base64;source_tail_utf8=$tail}
$encoded=Encode $admission;$decoded=Read-PgnCheckpointResumeAdmission $encoded
Check ($decoded.receipt.checkpoint.resume.tests -ceq '9007199254740997') 'Exact >2^53 tests survive wire'
$before=$binding|ConvertTo-Json -Depth 20 -Compress;$next=Get-PgnCheckpointResumeBinding $binding $manifest $exit $false $captured $decoded
Check ($next.start -ceq $resume.start -and $next.end -ceq $resume.end -and $next.length -ceq $resume.length) 'Original interval must survive resume'
Check ($next.resume_start.run -ceq $new -and $next.resume_start.previous_run -ceq $old -and $next.predecessor.run -ceq $predecessor) 'Process lineage is not completed predecessor'
Check (($binding|ConvertTo-Json -Depth 20 -Compress) -ceq $before) 'Pure binding must not mutate input'
Check ($next.telemetry.progress_seconds -eq 1.0) 'Producer settings retained'
foreach($change in @(
 {param($v) $v.receipt.origin='INITIAL_START'},
 {param($v) $v.receipt.run=$old},
 {param($v) $v.receipt.previous_run='c'*32},
 {param($v) $v.receipt.observation_id='00000000-0000-0000-0000-000000000000'},
 {param($v) $v.receipt.request_id+='x'},
 {param($v) $v.receipt.device=$device.ToLowerInvariant()},
 {param($v) $v.receipt.counter_basis='RESET'},
 {param($v) $v.receipt.raw_conservation_requirement='PAID'},
 {param($v) $v.receipt.checkpoint.resume.positions='01'},
 {param($v) $v.receipt.checkpoint.resume.tests='9007199254740996'},
 {param($v) $v.receipt.checkpoint.resume.checkpointSha256='0'*64},
 {param($v) $v.receipt.checkpoint.resume.remaining='1'},
 {param($v) $v.receipt.checkpoint.resume.nextStart=$resume.end},
 {param($v) $v.base.base_birth+="`n"},
 {param($v) $v.base.base_birth='2026-02-31T12:00:00Z'},
 {param($v) $v.base.base_run=$new},
 {param($v) $v.receipt|Add-Member NoteProperty surplus 'not native'}
)){$bad=Clone $admission;& $change $bad;Reject {Read-PgnCheckpointResumeAdmission (Encode $bad)}}
foreach($badText in @(' '+([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encoded))),([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($encoded))).Replace('"origin":"RESUME_CHECKPOINT"','"origin":"INITIAL_START","origin":"RESUME_CHECKPOINT"'))){Reject {Read-PgnCheckpointResumeAdmission ([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($badText)))}}
Reject {Read-PgnCheckpointResumeAdmission ($encoded+"`n")}
foreach($ownership in @($true,$null,'false',0)){Reject {Get-PgnCheckpointResumeBinding $binding $manifest $exit $ownership $captured $decoded}}
foreach($change in @({param($v)$v.run=$new},{param($v)$v.birth='2026-10-14T12:00:01Z'},{param($v)$v.start=$resume.nextStart},{param($v)$v.queued_intent=$request})){$bad=Clone $manifest;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $binding $bad $exit $false $captured $decoded}}
foreach($value in @($null,'0',0.0,$false,1)){$bad=Clone $exit;$bad.exit_code=$value;Reject {Get-PgnCheckpointResumeBinding $binding $manifest $bad $false $captured $decoded}}
foreach($change in @({param($v)$v.status='unavailable'},{param($v)$v.run=$new},{param($v)$v.checkpoint_base64=[Convert]::ToBase64String([byte[]]::new(64))},{param($v)$v.source_tail_utf8=$tail.Replace('processed_gaps=41','processed_gaps=40')},{param($v)$v.source_tail_utf8=$tail+'[phase0-gpu] --- end ---'},{param($v)$v.source_tail_utf8=$tail+$tail})){$bad=Clone $captured;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $binding $manifest $exit $false $bad $decoded}}
foreach($change in @({param($v)$v.start=$resume.nextStart},{param($v)$v.predecessor.run=$old},{param($v)$v.engine_upgrade.policy='other'},{param($v)$v.pins[0].sha256='0'*64},{param($v)$v.test_scope.start=$resume.nextStart})){$bad=Clone $binding;& $change $bad;Reject {Get-PgnCheckpointResumeBinding $bad $manifest $exit $false $captured $decoded}}
$intent=Clone @{schema='pgn-checkpoint-resume-intent-1';state='STAGED_START_PENDING';request_id=$request;admission=$decoded}
Check ((Get-PgnCheckpointResumeWorkerEpoch $next $manifest $exit $false $captured $intent) -ceq $new) 'Real worker validator consumes reserved newrun, not autonomous GUID'
foreach($state in @('PREPARED','LAUNCH_COMMITTED_UNCERTAIN','OWNED_PROCESS_OBSERVED','DONE',$null)){$bad=Clone $intent;$bad.state=$state;Reject {Get-PgnCheckpointResumeWorkerEpoch $next $manifest $exit $false $captured $bad}}
foreach($field in @('request_id','observation_id','previous_run','run','checkpoint_sha256','counter_basis','raw_conservation_requirement')){$bad=Clone $next;$bad.resume_start.$field='wrong';Reject {Get-PgnCheckpointResumeWorkerEpoch $bad $manifest $exit $false $captured $intent}}
$bad=Clone $next;$bad.resume_start|Add-Member NoteProperty surplus 'not native';Reject {Get-PgnCheckpointResumeWorkerEpoch $bad $manifest $exit $false $captured $intent}
Reject {Get-PgnCheckpointResumeWorkerEpoch $next $manifest $exit $true $captured $intent}
Write-Output ("checkpoint resume binding checks=$script:checks failures=0; pure memory only, no launch or raw-lineage proof")
