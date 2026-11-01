$ErrorActionPreference='Stop'
# CLOSED PS5/PS7 production-helper calls only. All worker/source observations
# below are synthetic TESTONLY fixtures. No controller, SDK image, driver,
# network, process launch, checkpoint load or physical capacity test is run.
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force -ErrorAction Stop
$script:checks=0
function Check([bool]$Ok,[string]$Why){$script:checks++;if(!$Ok){throw $Why}}
function Clone($Value){$Value|ConvertTo-Json -Depth 16 -Compress|ConvertFrom-Json}
function Text($Value){$Value|ConvertTo-Json -Depth 16 -Compress}
function Reject([scriptblock]$Body,[string]$Expected){
 $message=$null;try{& $Body|Out-Null}catch{$message=$_.Exception.Message}
 Check ($null -ne $message) 'Unsafe queued capacity/source request accepted'
 if($Expected){Check ($message -like ('*'+$Expected+'*')) ('Wrong refusal boundary: '+$message)}
}
$qualificationFile=Join-Path $PSScriptRoot '../evidence/gpu-owned-source-qualified-20261012-v1.json'
$profileFile=Join-Path $PSScriptRoot '../evidence/gpu-owned-source-next-profile-20261012.json'
Check ((Get-FileHash -LiteralPath $qualificationFile -Algorithm SHA256).Hash -ceq '99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684') 'Named finite qualification bytes changed'
Check ((Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash -ceq '713EA5456BD945849C7C291682FE32E733F000A8C5132DBE22B93392455B1685') 'Named profile bytes changed'
$profile=Get-Content -LiteralPath $profileFile -Raw|ConvertFrom-Json
$qualification=Get-Content -LiteralPath $qualificationFile -Raw|ConvertFrom-Json
Check ($qualification.production_validated -ceq $false -and $qualification.independent_coverage -ceq $false) 'Do not relabel finite qualification as production or independent coverage'
$binding=Clone @{
 uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$profile.sdk;executable=$profile.executable;pins=$profile.pins
 start='199909000000000000000';end='199910000000000000000';length='1000000000000000'
 engine_upgrade=@{policy=$profile.numeric_policy;qualification_sha256=$profile.qualification_sha256}
 community=@{source='PUBLIC-TESTONLY';source_sha256='old';observed_utc='UNCHANGED-TESTONLY'}
 test_scope=@{enabled=$true;order='ascending';start='199900000000000000000';end='200000000000000000000'}
 telemetry=@{progress_seconds=1;checkpoint_seconds=30};queued_intent='00000000-0000-4000-8000-000000000001'
 initial_start=@{origin='INITIAL_START';request_id='00000000-0000-4000-8000-000000000002';mode='manual-reservations'}
 fixture='SYNTHETIC_TESTONLY_NOT_PHYSICAL_OBSERVATION'
}
$csv="Start,End,Status,Owner`n199900,200000,Open,`n"
$id='00000000-0000-4000-8000-000000000003'
function Source($Binding,[string]$Run){
 $manifest=[pscustomobject]@{schema='pgn-native-process-1';run=$Run;uuid=$Binding.uuid;start=$Binding.start;end=$Binding.end;queued_intent=$Binding.queued_intent;stdout='PUBLIC-TESTONLY.stdout'}
 $exit=[pscustomobject]@{run=$Run;exit_code=0;utc='2026-10-15T00:00:00Z'}
 $tail="[pgn-counters] time_ns=123 positions=$($Binding.length) gaps=7 tests=9007199254740993 batches=2 processed_gaps=7`n[phase0-gpu] gaps reported=7 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
 $receipt=Get-PgnSourceCompletion $Binding $manifest $tail $exit $false $false
 Check ($receipt.source_range_complete -ceq $true -and $receipt.independent_coverage -ceq $false -and $receipt.proof_status -ceq 'SOURCE_PRP_INDEPENDENT_PROOF_PENDING') 'SOURCE footer is not independent coverage'
 [pscustomobject]@{manifest=$manifest;exit=$exit;tail=$tail;receipt=$receipt}
}
# The actual qualified identity/pins are read from the named immutable local
# profile. This binds the branch under test, not an actual installed image.
$originalBindingJson=Text $binding
$full=Source $binding ('a'*32)
$fragment=Get-PgnQueuedBinding $binding $full.receipt $csv $id '199907000000000000000' '199907000000000000500'
Check ($fragment.length -ceq '500' -and $fragment.start -ceq '199907000000000000000' -and $fragment.end -ceq '199907000000000000500') 'Full->fragment exact range failed'
Check ((Text $binding) -ceq $originalBindingJson) 'Source binding or original observation age mutated'
Check ($fragment.predecessor.run -ceq $full.manifest.run -and $fragment.predecessor.start -ceq $binding.start -and $fragment.predecessor.end -ceq $binding.end) 'Real predecessor tuple lost'
Check ($fragment.predecessor.coverage -ceq 'SOURCE_COMPLETION_NOT_INDEPENDENT_CERTIFICATE') 'Predecessor wrongly promoted'
$fragmentSnapshotJson=Text $fragment
Check ($fragmentSnapshotJson -is [string] -and $fragmentSnapshotJson.Length -gt 0) 'Fragment JSON snapshot was empty before either source/next call'
$short=Source $fragment ('b'*32)
Check ($fragmentSnapshotJson -is [string] -and $fragmentSnapshotJson.Length -gt 0) 'Source helper erased caller fragment JSON snapshot'
$next=Get-PgnQueuedBinding $fragment $short.receipt $csv $id '199908000000000000000' '199909000000000000000'
Check ($next.length -ceq '1000000000000000' -and $next.start -ceq '199908000000000000000' -and $next.end -ceq '199909000000000000000') 'Fragment500->full independent qualified capacity failed'
Check ((Text $fragment) -ceq $fragmentSnapshotJson) 'Fragment observation changed after queued binding construction'
Check ($next.predecessor.run -ceq $short.manifest.run) ('Wrong predecessor run: '+$next.predecessor.run+' expected='+$short.manifest.run)
Check ($next.predecessor.start -ceq $fragment.start) ('Wrong predecessor start: '+$next.predecessor.start+' expected='+$fragment.start)
Check ($next.predecessor.end -ceq $fragment.end) ('Wrong predecessor end: '+$next.predecessor.end+' expected='+$fragment.end)
foreach($name in @('pins','executable','sdk','telemetry','test_scope','engine_upgrade','initial_start')){Check ((Text $next.$name) -ceq (Text $fragment.$name)) ('Unrelated binding field changed: '+$name)}
# Disjoint/manual order is not forced forward; it still cannot overlap the
# current source or leave freshly declared Open bounds.
$later=Source $next ('c'*32)
$back=Get-PgnQueuedBinding $next $later.receipt $csv $id '199906000000000000000' '199907000000000000000'
Check ($back.start -ceq '199906000000000000000' -and $back.length -ceq '1000000000000000') 'Disjoint backward manual reservation changed/refused'
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv $id $fragment.start $fragment.end} 'declared-open source/finite slice/64-bit window'
Reject {Get-PgnQueuedBinding $fragment $short.receipt ($csv.Replace('Open','Reserved')) $id $next.start $next.end} 'declared-open source/finite slice/64-bit window'
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv $id '199899000000000000000' '199900000000000000000'} 'declared-open source/finite slice/64-bit window'
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv 'invalid' $next.start $next.end} 'Queued intent identity invalid'
foreach($change in @(
 {param($b)$b.engine_upgrade.policy='OLD_UNSUPPORTED'},
 {param($b)$b.engine_upgrade.PSObject.Properties.Remove('policy')},
 {param($b)$b.sdk='C:/PGN/native/unqualified'},
 {param($b)$b.executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/../pgn-continuous-gpu.exe'},
 {param($b)$b.engine_upgrade.qualification_sha256='0'*64}
)){$bad=Clone $fragment;& $change $bad;Reject {Get-PgnQueuedBinding $bad $short.receipt $csv $id $next.start $next.end} 'Source engine profile not qualified'}
foreach($change in @(
 {param($b)$b.pins[0].sha256='0'*64},
 {param($b)$b.pins[1].sha256='0'*64},
 {param($b)$b.pins[0].path+='/'},
 {param($b)$b.pins[0].sha256=$b.pins[0].sha256.ToLowerInvariant()}
)){$bad=Clone $fragment;& $change $bad;Reject {Get-PgnQueuedBinding $bad $short.receipt $csv $id $next.start $next.end} 'Qualified image/runtime pin mismatch'}
foreach($change in @({param($b)$b.pins=@()},{param($b)$b.pins+=$b.pins[0]})){$bad=Clone $fragment;& $change $bad;Reject {Get-PgnQueuedBinding $bad $short.receipt $csv $id $next.start $next.end} 'Source engine closure requires both pins'}
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv $id '199908000000000000000' '199909000000000000001'} 'Launch exceeds qualified high10 finite capacity'
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv $id '184467440737095516159' '184468440737095516159'} 'Launch exceeds qualified high10 finite capacity'
Reject {Get-PgnQueuedBinding $fragment $short.receipt $csv $id '202914184810805067700' '202914184810805067800'} 'Launch exceeds qualified high10 finite capacity'
# Declaring the known policy does not permit a corrupt pin to fall back to a
# legacy non-enlarging path either.
$bad=Clone $binding;$bad.pins[0].sha256='0'*64
Reject {Get-PgnQueuedBinding $bad $full.receipt $csv $id '199907000000000000000' '199907000000000000500'} 'Qualified image/runtime pin mismatch'
# Existing other engines retain only their previous non-enlarging behavior;
# they do not inherit the A73 capacity simply from a local 10^15 constant.
$legacy=Clone $binding;$legacy.engine_upgrade.policy='LEGACY_TESTONLY';$legacy.executable='PUBLIC-TESTONLY-LEGACY'
$smaller=Get-PgnQueuedBinding $legacy $full.receipt $csv $id $fragment.start $fragment.end
Check ($smaller.length -ceq '500') 'Legacy non-enlarging golden behavior changed'
$legacyShort=Clone $fragment;$legacyShort.engine_upgrade.policy='LEGACY_TESTONLY';$legacyShort.executable='PUBLIC-TESTONLY-LEGACY'
Reject {Get-PgnQueuedBinding $legacyShort $short.receipt $csv $id $next.start $next.end} 'Source engine profile not qualified'
# The original SOURCE/STOP/checkpoint/failed/partial guards remain the real
# production helpers, not copies of controller logic in this test.
Reject {Get-PgnSourceCompletion $fragment $short.manifest $short.tail $short.exit $false $true} 'Stopped source cannot authorize succession'
Reject {Get-PgnSourceCompletion $fragment $short.manifest $short.tail $short.exit $true $false} 'Checkpointed source is not range completion'
$failed=Clone $short.exit;$failed.exit_code=1
Reject {Get-PgnSourceCompletion $fragment $short.manifest $short.tail $failed $false $false} 'Source exit is missing or failed'
Reject {Get-PgnSourceCompletion $fragment $short.manifest ($short.tail.Replace('--- end ---','partial')) $short.exit $false $false} 'Complete source footer required'
foreach($change in @({param($r)$r.source_range_complete=$false},{param($r)$r.independent_coverage=$true},{param($r)$r.source_positions='499'},{param($r)$r.start='199907000000000000001'})){$r=Clone $short.receipt;& $change $r;Reject {Get-PgnQueuedBinding $fragment $r $csv $id $next.start $next.end} 'Exact predecessor receipt required'}
Check ((Text $binding) -ceq $originalBindingJson -and (Text $fragment) -ceq $fragmentSnapshotJson) 'Poison calls mutated original bindings'
[pscustomobject]@{status='PASS';checks=$script:checks;ps_major=$PSVersionTable.PSVersion.Major;scope='CLOSED_SYNTHETIC_TESTONLY_PRODUCTION_HELPERS';sequence='full->fragment500->full';source_scope='SOURCE_PRP_INDEPENDENT_PROOF_PENDING';physical_launch=$false;installed_image_read=$false;independent_coverage=$false}|ConvertTo-Json -Compress
