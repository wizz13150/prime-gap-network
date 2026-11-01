$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
function Clone($value){
 $json=$value|ConvertTo-Json -Depth 12 -Compress
 if((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')){return ($json|ConvertFrom-Json -DateKind String)}
 return ($json|ConvertFrom-Json)
}
function Reject([scriptblock]$action){$rejected=$false;try{$null=& $action}catch{$rejected=$true};if(!$rejected){throw 'Unsafe engine successor accepted'}}
foreach($path in @('C:/PGN/evidence/final-counter-20261011-v1/result.json','C:/PGN/evidence/euler96-qualified-20261011-v1/result.json','C:/PGN/evidence/fused-fixed-high10-qualified-20261012-v1/result.json')){
 if(!(Test-PgnEngineQualificationPath $path)){throw "Qualified controller receipt path rejected: $path"}
}
foreach($path in @('C:/PGN/evidence/fused-fixed-high10-qualified-20261012-v2/result.json','C:/PGN/evidence/fused-fixed-high10-final-20261012-v2/result.json','C:/PGN/evidence/fused-fixed-high10-qualified-20261012-v1/../result.json','C:/PGN/evidence/fused-fixed-high10-qualified-20261012-v1/resultXjson')){
 if(Test-PgnEngineQualificationPath $path){throw "Unqualified controller receipt path accepted: $path"}
}
$controller=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/native-control.ps1'))
if($controller -notmatch 'if\(!\(Test-PgnEngineQualificationPath \$evidencePath\) -or'){
 throw 'Actual controller does not use the shared qualification path gate'
}
$names=@('libgcc_s_seh-1.dll','libgmp-10.dll','libwinpthread-1.dll','pgn-gpu-engine-164.exe','phase0gpu.dll','cudart64_12.dll')
$sdk='C:/PGN/native/20261011-host-counter-v2';$stage='C:/PGN/native/20261011-final-counter-v1';$run='a'*32
$before='A'*64;$after='B'*64;$library='C'*64
$current=[pscustomobject]@{sdk=$sdk;executable=$sdk+'/bin/pgn-gpu-engine-164.exe';uuid='3090';start='200';end='300';pins=@($names|ForEach-Object {@{path=$sdk+'/bin/'+$_;sha256=if($_ -ceq 'pgn-gpu-engine-164.exe'){$before}else{$library}}})}
$next=Clone $current;$next.start='100';$next.end='200';$next|Add-Member NoteProperty queued_intent 'EXACT_QUEUE';$next|Add-Member NoteProperty telemetry @{progress_seconds=0.2};$next|Add-Member NoteProperty variant @{walk_grid=164;walk_block=512;status='OLD'}
$manifest=@{run=$run;pid=8436;birth='2026-10-11T06:02:20.9931787Z'}
$profile=[pscustomobject]@{schema='pgn-one-boundary-engine-upgrade-1';writer='root';expected_predecessor=$run;before_executable=$current.executable;before_sha256=$before;sdk=$stage;executable=$stage+'/bin/pgn-gpu-engine-164.exe';candidate_sha256=$after;qualification_path='C:/PGN/evidence/final-counter-20261011-v2/result.json';qualification_sha256='D'*64;pins=@($names|ForEach-Object {@{path=$stage+'/bin/'+$_;sha256=if($_ -ceq 'pgn-gpu-engine-164.exe'){$after}else{$library}}})}
$qualification=[pscustomobject]@{status='FINAL_COUNTER_AND_EXACT_GOLDEN_PARITY_PASS_NOT_PRODUCTION_PROMOTION';candidate_sha256=$after;constant_pid=8436;constant_birth=$manifest.birth;constant_unchanged=$true;device_dll_unchanged=$true;cases=@(
 @{case='one-position';exit=0;owner=@{image=$profile.executable;length='1'};last_counter='[pgn-counters] time_ns=123 positions=1 gaps=0 tests=5 batches=1 processed_gaps=0';pairs=0},
 @{case='golden-prefix';exit=0;owner=@{image=$profile.executable;length='1030792151040'};last_counter=@{value='[pgn-counters] time_ns=456 positions=1030792151040 gaps=175 tests=6 batches=17 processed_gaps=175'};pairs=175})}
$result=Get-PgnQualifiedEngineSuccessor $current $next $manifest $profile $qualification
if($result.executable -cne $profile.executable -or $next.executable -cne $current.executable -or $result.start -cne $next.start -or $result.end -cne $next.end -or $result.queued_intent -cne $next.queued_intent -or $result.telemetry.progress_seconds -ne 0.2){throw 'Upgrade mutated original or range/queue/cadence'}
if((Get-PgnQualifiedEngineSuccessor $current $next @{run='b'*32} $profile $qualification).executable -cne $current.executable){throw 'Wrong predecessor consumed upgrade'}
Reject {$q=Clone $qualification;$q.constant_birth='REUSED';Get-PgnQualifiedEngineSuccessor $current $next $manifest $profile $q}
Reject {$q=Clone $qualification;$q.cases[0].exit=$null;Get-PgnQualifiedEngineSuccessor $current $next $manifest $profile $q}
Reject {$q=Clone $qualification;$q.cases[1].pairs=174;Get-PgnQualifiedEngineSuccessor $current $next $manifest $profile $q}
Reject {$q=Clone $qualification;$q.cases[0].last_counter='[pgn-counters] positions=0';Get-PgnQualifiedEngineSuccessor $current $next $manifest $profile $q}
Reject {$p=Clone $profile;$p.pins[4].sha256='D'*64;Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $qualification}
Reject {$p=Clone $profile;$p.pins[0].path='C:/PGN/other.dll';Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $qualification}
Reject {$p=Clone $profile;$p.pins+=,$p.pins[0];Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $qualification}
Reject {$p=Clone $profile;$p.before_sha256='E'*64;Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $qualification}
Reject {$p=Clone $profile;$p.sdk='C:/Users/fixture-user';Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $qualification}
$arithProfile=Clone $profile;$arithQualification=Clone $qualification
$arithProfile|Add-Member NoteProperty numeric_policy 'EULER96_EXACT_ORACLE_AND_GOLDEN_V1'
$arithProfile|Add-Member NoteProperty before_dll_sha256 $library
$arithProfile|Add-Member NoteProperty candidate_dll_sha256 ('F'*64)
$arithProfile.pins[4].sha256='F'*64
$arithQualification.device_dll_unchanged=$false
$arithQualification|Add-Member NoteProperty device_dll_sha256 ('F'*64)
$batches=@(1..17|ForEach-Object {'positions='+$_+' gaps=175 tests=6 batches='+$_+' processed_gaps=175'})
$batches[16]='positions=1030792151040 gaps=175 tests=6 batches=17 processed_gaps=175'
$gapBatches=@(0..16|ForEach-Object {'first_block='+($_*64)+' blocks='+$(if($_ -eq 16){1}else{64})+' gaps=0'})
$arithQualification|Add-Member NoteProperty arithmetic ([pscustomobject]@{policy=$arithProfile.numeric_policy;domain='ODD_2^64_LT_N_LT_2^96_EULER_JACOBI_BASE2';before_dll_sha256=$library;after_dll_sha256='F'*64;oracle=@{exit=0;cases=12072;mismatches=0;uuid='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265';fixture_sha256='A8B85D689B36FC950F917CC962E2F8AA14FACB906975D43B81F10E61A44737FC'};reference_batches=$batches;candidate_batches=$batches;reference_gap_batches=$gapBatches;candidate_gap_batches=$gapBatches})
$result=Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $arithQualification
if($result.pins[4].sha256 -cne ('F'*64) -or $result.variant.status -cne 'EULER96_NUMERIC_PARITY_QUALIFIED_TARGET_PERFORMANCE_UNCONFIRMED' -or $result.start -cne $next.start -or $result.queued_intent -cne $next.queued_intent){throw 'Qualified arithmetic upgrade changed range/queue or claimed unmeasured speed'}
Reject {$p=Clone $arithProfile;$p.numeric_policy='OTHER';Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $arithQualification}
Reject {$q=Clone $arithQualification;$q.arithmetic.oracle.mismatches=1;Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $q}
Reject {$q=Clone $arithQualification;$q.arithmetic.oracle.exit=$null;Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $q}
Reject {$q=Clone $arithQualification;$q.arithmetic.candidate_batches[3]='positions=4 gaps=174 tests=6 batches=4 processed_gaps=174';Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $q}
Reject {$q=Clone $arithQualification;$q.arithmetic.reference_batches=$batches[0..14];Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $q}
Reject {$q=Clone $arithQualification;$q.arithmetic.candidate_gap_batches[3]='first_block=192 blocks=64 gaps=1';Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $q}
$partial=Clone $arithQualification;$partial.arithmetic.reference_batches=@($batches[0..14]+$batches[16]);$partial.arithmetic.candidate_batches=$partial.arithmetic.reference_batches
$null=Get-PgnQualifiedEngineSuccessor $current $next $manifest $arithProfile $partial
Reject {$p=Clone $arithProfile;$p.pins[0].sha256='F'*64;Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $arithQualification}
Reject {$p=Clone $arithProfile;$p.candidate_dll_sha256='E'*64;Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $arithQualification}
$fusedProfile=Clone $arithProfile;$fusedQualification=Clone $arithQualification
$fusedProfile.sdk='C:/PGN/native/20261012-fused-fixed-high10-v2';$fusedProfile.executable=$fusedProfile.sdk+'/bin/pgn-gpu-engine-164.exe'
foreach($pin in $fusedProfile.pins){$pin.path=$fusedProfile.sdk+'/bin/'+[IO.Path]::GetFileName($pin.path)}
foreach($case in $fusedQualification.cases){$case.owner.image=$fusedProfile.executable}
$fusedResult=Get-PgnQualifiedEngineSuccessor $current $next $manifest $fusedProfile $fusedQualification
if($fusedResult.executable -cne $fusedProfile.executable -or $fusedResult.start -cne $next.start -or $fusedResult.end -cne $next.end -or $fusedResult.queued_intent -cne $next.queued_intent){throw 'Corrected fused SDK mutated range/queue'}
Reject {$p=Clone $fusedProfile;$p.sdk='C:/PGN/native/20261012-fused-fixed-high10-v1';Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $fusedQualification}
Reject {$p=Clone $fusedProfile;$p.sdk='C:/PGN/native/20261012-unknown';Get-PgnQualifiedEngineSuccessor $current $next $manifest $p $fusedQualification}
Reject {$q=Clone $fusedQualification;$q.constant_pid=9999;Get-PgnQualifiedEngineSuccessor $current $next $manifest $fusedProfile $q}
Reject {$q=Clone $fusedQualification;$q.cases[1].pairs=174;Get-PgnQualifiedEngineSuccessor $current $next $manifest $fusedProfile $q}
'PASS one named completed predecessor, unchanged original/range/queue/cadence, finite exits/final counters/golden parity, exact dependency closure, corrected-v2-only SDK, wrong epoch/pins/path/DLL/partial rejection'
