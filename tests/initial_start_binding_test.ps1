$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/range-successor.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../tools/initial-start-binding.psm1') -Force
$script:checks=0
function Check($Condition,[string]$Message){++$script:checks;if(!$Condition){throw $Message}}
function Reject($Action){$bad=$false;try{& $Action|Out-Null}catch{$bad=$true};Check $bad 'Expected initial admission refusal'}
function Clone($Value){$Value|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$binding=Clone @{
 uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';start='199932000000000000000';end='199933000000000000000';length='1000000000000000'
 executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sdk='C:/PGN/native/20261012-gpu-owned-source-v1';engine_upgrade=@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'}
 pins=@(@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/cudart64_12.dll';sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'})
 test_scope=@{start='199900000000000000000';end='200000000000000000000';order='ascending'}
 community=@{source='https://example.invalid/fixture.csv';source_sha256=('a'*64);observed_utc='2026-10-14T00:00:00Z';status='DECLARED_OPEN';exclusive_reservation=$false;row=1001}
 predecessor=@{run=('b'*32)};queued_intent='old'
}
$receipt=@{schema='pgn-initial-start-receipt-1';origin='INITIAL_START';request_id='11111111-1111-4111-8111-111111111111';observation_id='22222222-2222-4222-8222-222222222222';booking_id='33333333-3333-4333-8333-333333333333';device=$binding.uuid.ToLowerInvariant();mode='manual-reservations';start='199907000000000000000';end='199908000000000000000';input_fingerprint=('c'*64)
 scope=@{execution_start=$binding.test_scope.start;execution_end=$binding.test_scope.end;auto_start=$binding.test_scope.start;auto_end=$binding.test_scope.end;block_anchor=$binding.test_scope.start;block_width='1000000000000000'}
 registry_provenance=@{url=$binding.community.source;sha256=('d'*64);observed_utc='2026-10-14T01:00:00Z';semantic_digest=('e'*64)}}
$admission=Clone @{schema='pgn-initial-start-admission-1';receipt=$receipt;base=@{base_run=('b'*32);base_birth='2026-10-14T00:00:00.0000000Z';base_start=$binding.start;base_end=$binding.end}}
$csv="Start *1e15,End *1e15,Status,Owner`n199900,200000,Open,`n"
$original=$binding|ConvertTo-Json -Depth 12 -Compress
$next=Get-PgnInitialStartBinding $binding $admission $csv '2026-10-14T02:00:00Z'
Check ($next.start -ceq $receipt.start -and $next.end -ceq $receipt.end) 'Manual head must replace unrelated configured B33 with B8'
Check ($null -eq $next.predecessor -and $next.queued_intent -ceq $receipt.booking_id) 'Initial origin cannot invent a completed predecessor'
Check ($next.initial_start.origin -ceq 'INITIAL_START' -and $next.initial_start.local_booking_not_public_lease -eq $true) 'Initial/local scope'
Check ($next.community.observed_utc -ceq '2026-10-14T02:00:00Z' -and $null -eq $next.community.row) 'Fresh provenance not old row'
Check (($binding|ConvertTo-Json -Depth 12 -Compress) -ceq $original) 'Binding input mutated'
$auto=Clone $admission;$auto.receipt.mode='automatic';$auto.receipt.start=$binding.test_scope.start;$auto.receipt.end='199901000000000000000'
$first=Get-PgnInitialStartBinding $binding $auto $csv '2026-10-14T02:00:00Z'
Check ($first.start -ceq $binding.test_scope.start) 'Auto first full block not configured B33'
foreach($field in @('execution_start','execution_end','auto_start','auto_end','block_anchor','block_width')){
 $bad=Clone $admission;$bad.receipt.scope.$field=$null;Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
}
foreach($field in @('request_id','observation_id','booking_id')){$bad=Clone $admission;$bad.receipt.$field='00000000-0000-0000-0000-000000000000';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}}
$bad=Clone $admission;$bad.receipt.device='GPU-other';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
$bad=Clone $admission;$bad.receipt.mode='Automatic';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
$bad=Clone $auto;$bad.receipt.start='199900000000000000001';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
$bad=Clone $admission;$bad.receipt.start='134000000000000000000';$bad.receipt.end='134001000000000000000';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
$bad=Clone $admission;$bad.receipt.end='199909000000000000000';Reject {Get-PgnInitialStartBinding $binding $bad $csv 'now'}
Reject {Get-PgnInitialStartBinding $binding $admission ($csv.Replace('Open,','Reserved,Alice')) 'now'}
$bad=Clone $binding;$bad.pins=@();Reject {Get-PgnInitialStartBinding $bad $admission $csv 'now'}
$bad=Clone $binding;$bad.pins[0].sha256=('0'*64);Reject {Get-PgnInitialStartBinding $bad $admission $csv 'now'}
$wire=[Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes(($admission|ConvertTo-Json -Depth 12 -Compress)))
$decoded=Read-PgnInitialAdmission $wire;Check ($decoded.receipt.booking_id -ceq $receipt.booking_id) 'Wire round trip'
foreach($badWire in @('!',($wire+"`n"),('A'*16388),[Convert]::ToBase64String([byte[]]@(255)))){Reject {Read-PgnInitialAdmission $badWire}}
$bomWire=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetPreamble()+[Text.Encoding]::UTF8.GetBytes(($admission|ConvertTo-Json -Depth 12 -Compress)));Reject {Read-PgnInitialAdmission $bomWire}
$bad=Clone $admission;$bad.base.base_birth=$null;$badWire=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($bad|ConvertTo-Json -Depth 12 -Compress)));Reject {Read-PgnInitialAdmission $badWire}
$empty=Clone $admission;foreach($name in @('base_run','base_birth','base_start','base_end')){$empty.base.$name=$null};$emptyWire=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($empty|ConvertTo-Json -Depth 12 -Compress)));Check ($null -eq (Read-PgnInitialAdmission $emptyWire).base.base_run) 'Explicit empty base decode only; not physical Ready proof'
$ready=Get-PgnInitialReadyEvidence $binding $null $false $false $null '' $empty
Check ($ready.origin -ceq 'OBSERVED_INITIAL_EMPTY' -and $null -eq $ready.completed_predecessor) 'Empty initial boundary invented predecessor'
Reject {Get-PgnInitialReadyEvidence $binding $null $true $false $null '' $empty}
Reject {Get-PgnInitialReadyEvidence $binding $null $null $false $null '' $empty}
Reject {Get-PgnInitialReadyEvidence $binding $null $false $null $null '' $empty}
Reject {Get-PgnInitialReadyEvidence $binding $null $false $true $null '' $empty}
Reject {Get-PgnInitialReadyEvidence $binding $null $false $false @{run=('b'*32)} '' $empty}
$manifest=Clone @{schema='pgn-native-process-1';run=('b'*32);birth=$admission.base.base_birth;start=$binding.start;end=$binding.end;uuid=$binding.uuid}
$exit=Clone @{run=$manifest.run;exit_code=0}
$tail="[pgn-counters] time_ns=1 positions=1000000000000000 gaps=1 tests=1 batches=1 processed_gaps=1`n[phase0-gpu] gaps reported=1 verification_failures=0 records_new=0`n[phase0-gpu] --- end ---`n"
$ready=Get-PgnInitialReadyEvidence $binding $manifest $false $false $exit $tail $admission
Check ($ready.source_range_complete -eq $true -and $ready.independent_coverage -eq $false) 'Initial previous full SOURCE scope'
Reject {Get-PgnInitialReadyEvidence $binding $manifest $false $true $exit $tail $admission}
Reject {Get-PgnInitialReadyEvidence $binding $manifest $false $false $exit ($tail.Replace('positions=1000000000000000','positions=1')) $admission}
$wrong=Clone $admission;$wrong.base.base_run=('c'*32);Reject {Get-PgnInitialReadyEvidence $binding $manifest $false $false $exit $tail $wrong}
Reject {Get-PgnInitialReadyEvidence $binding $manifest $false $false @{run=$manifest.run;exit_code='0'} $tail $admission}
# Parse the REAL controller but never execute its top-level runtime actions.
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../tools/native-control.ps1'),[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'Actual controller syntax'
Check ($ast.Extent.Text.Contains('Get-PgnInitialReadyEvidence $binding $manifest')) 'Controller must pay the tested Ready verifier'
('PASS initial binding '+$script:checks+' checks; pure module, no controller/network/GPU/task/UI/files changed')
