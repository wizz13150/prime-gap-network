$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
$candidate='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'
$baseline='C20657BF37C353235B36DF691F500C0E8D87B49F8A6AFE6F10AF0E7C740B889D'
$dll='FC42645DBEF06110D24D9144472ECE8AF18BD0AAFC68A7789E563402B96704B0';$runtime='D'*64
$current=[pscustomobject]@{sdk='C:/PGN/native/20261012-fused-fixed-high10-v2';executable='C:/PGN/native/20261012-fused-fixed-high10-v2/bin/pgn-gpu-engine-164.exe';uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';start='199909000000000000000';end='199910000000000000000';length='1000000000000000';variant=[pscustomobject]@{walk_grid=164;walk_block=512;status='old'};pins=@();queued_intent='keep-exact-queue';telemetry=[pscustomobject]@{progress_seconds=0.2;checkpoint_seconds=30};test_scope=[pscustomobject]@{enabled=$true;start='199900000000000000000';end='200000000000000000000'}}
$current.pins=@([pscustomobject]@{path=$current.executable;sha256=$baseline},[pscustomobject]@{path=($current.sdk+'/bin/phase0gpu.dll');sha256=$dll},[pscustomobject]@{path=($current.sdk+'/bin/cudart64_12.dll');sha256=$runtime})
$manifest=[pscustomobject]@{run='a'*32;pid=99;birth='exact-current'}
$sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
$profile=[pscustomobject]@{schema='pgn-one-boundary-engine-upgrade-1';writer='root';numeric_policy='GPU_OWNED_HIGH10_SOURCE_V1';expected_predecessor=$manifest.run;sdk=$sdk;executable=($sdk+'/bin/pgn-continuous-gpu.exe');before_executable=$current.executable;before_sha256=$baseline;candidate_sha256=$candidate;qualification_path='C:/PGN/evidence/gpu-owned-source-qualified-20261012-v1/result.json';qualification_sha256='E'*64;pins=@([pscustomobject]@{path=($sdk+'/bin/pgn-continuous-gpu.exe');sha256=$candidate},[pscustomobject]@{path=($sdk+'/bin/cudart64_12.dll');sha256=$runtime})}
$qualification=[pscustomobject]@{schema='pgn-gpu-owned-source-qualification-1';status='FINITE_NATIVE_SCIENCE_ABI_LIFECYCLE_PROOF_TRACE_PASS_NOT_PRODUCTION';candidate_sha256=$candidate;baseline_executable_sha256=$baseline;baseline_device_sha256=$dll;device='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265';arch='sm86';independent_coverage=$false;production_validated=$false;source_sha256='242F1F18C608F3D945BB53B06E729C8A3C088E5B5C769BAD5C51C43456A0CEA6';header_sha256='CABAEAFA63F4C57C8B448282DD58B43B58E218C1A49ED3221A15C74C36A1E436';science=[pscustomobject]@{bitmap_words=545259520;bitmap_errors=0;pairs=175;tests='3437901640';host_graph_launches=1;gpu_iterations=17};lifecycle=[pscustomobject]@{resumed_pairs=175;omissions=0;duplicates=0};proofs=[pscustomobject]@{point_cases=9;gap_cases=3;errors=0;invalid_published=0};trace=[pscustomobject]@{kernels=69;dropped=0;errors=0};cases=@([pscustomobject]@{case='one-position';exit=0;image_sha256=$candidate;length='1';last_counter='[pgn-counters] time_ns=1 positions=1 gaps=0 tests=3576912 batches=1 processed_gaps=0'},[pscustomobject]@{case='golden-prefix';exit=0;image_sha256=$candidate;length='1030792151040';last_counter='[pgn-counters] time_ns=2 positions=1030792151040 gaps=175 tests=3437901640 batches=17 processed_gaps=175'})}
function Clone($Value){$Value|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$before=$current|ConvertTo-Json -Depth 12 -Compress
$changed=Get-PgnQualifiedEngineSuccessor $current $current $manifest $profile $qualification
if(($current|ConvertTo-Json -Depth 12 -Compress) -cne $before -or $changed.executable -cne $profile.executable -or $changed.engine_upgrade.policy -cne $profile.numeric_policy){throw 'Non-mutating module transition failed'}
foreach($name in @('start','end','length','uuid','queued_intent','telemetry','test_scope')){if(($changed.$name|ConvertTo-Json -Compress) -cne ($current.$name|ConvertTo-Json -Compress)){throw ('Source identity changed '+$name)}}
$tests=0
function Reject($P,$Q,$N){$refused=$false;try{Get-PgnQualifiedEngineSuccessor $current $N $manifest $P $Q|Out-Null}catch{$refused=$true};if(!$refused){throw 'Unsafe native module admission accepted'};$script:tests++}
$p=Clone $profile;$p.candidate_sha256='F'*64;Reject $p $qualification $current
$p=Clone $profile;$p.executable=$sdk+'/bin/../pgn-continuous-gpu.exe';Reject $p $qualification $current
$p=Clone $profile;$p.pins+=$p.pins[0];Reject $p $qualification $current
$p=Clone $profile;$p.pins[1].sha256='F'*64;Reject $p $qualification $current
$p=Clone $profile;$p.qualification_path='C:/PGN/diagnostics/arbitrary/result.json';Reject $p $qualification $current
$q=Clone $qualification;$q.independent_coverage=$true;Reject $profile $q $current
$q=Clone $qualification;$q.production_validated=$true;Reject $profile $q $current
$q=Clone $qualification;$q.science.bitmap_errors=1;Reject $profile $q $current
$q=Clone $qualification;$q.science.host_graph_launches=17;Reject $profile $q $current
$q=Clone $qualification;$q.science.pairs=174;Reject $profile $q $current
$q=Clone $qualification;$q.lifecycle.duplicates=1;Reject $profile $q $current
$q=Clone $qualification;$q.lifecycle.omissions=1;Reject $profile $q $current
$q=Clone $qualification;$q.proofs.invalid_published=1;Reject $profile $q $current
$q=Clone $qualification;$q.trace.dropped=1;Reject $profile $q $current
$q=Clone $qualification;$q.cases[0].last_counter=$q.cases[0].last_counter.Replace('positions=1','positions=0');Reject $profile $q $current
$n=Clone $current;$n.start='221360928884514619392';$n.end='221361928884514619392';Reject $profile $qualification $n
$n=Clone $current;$n.uuid='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265';Reject $profile $qualification $n
$n=Clone $current;$n.length='999';Reject $profile $qualification $n
$old=Clone $profile;$old.expected_predecessor='b'*32
if((Get-PgnQualifiedEngineSuccessor $current $current $manifest $old $qualification).executable -cne $current.executable){throw 'Consumed/stale profile applied to wrong epoch'}
if(!(Test-PgnEngineQualificationPath $profile.qualification_path)){throw 'Named standalone qualification path rejected'}
@{status='PASS';rejection_cases=$tests;non_mutating=$true;scope='Pure admission contract fixtures, not a hardware/runtime deployment'}|ConvertTo-Json -Compress
