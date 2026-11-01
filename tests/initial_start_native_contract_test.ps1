param([Parameter(Mandatory=$true)][string]$FixtureRoot,[Parameter(Mandatory=$true)][string]$ProfileAudit)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/initial-start-binding.psm1') -Force
$marker=[IO.File]::ReadAllText((Join-Path $FixtureRoot 'fixture.json'))|ConvertFrom-Json
if($marker.schema -cne 'pgn-closed-native-initial-contract-1'){throw 'Closed fixture only'}
$wire=[IO.File]::ReadAllText((Join-Path $FixtureRoot 'admission.base64'))
$admission=Read-PgnInitialAdmission $wire
if((Get-Item -LiteralPath $ProfileAudit).Length -gt 131072){throw 'Profile audit size'}
$audit=[IO.File]::ReadAllText($ProfileAudit)|ConvertFrom-Json
$profile=$audit.files|Where-Object path -ceq 'C:/PGN/runtime/binding.json'
$binding=$profile.text|ConvertFrom-Json
# Memory-only fixture adaptation. The observed live profile/data are not edited.
$binding.community.source=$admission.receipt.registry_provenance.url
$csv=[IO.File]::ReadAllText((Join-Path $FixtureRoot 'community-source.csv'))
$next=Get-PgnInitialStartBinding $binding $admission $csv '2026-10-14T00:00:01.000Z'
if($next.start -cne '199907000000000000000' -or $next.end -cne '199908000000000000000' -or
 $next.queued_intent -cne $admission.receipt.booking_id -or $null -ne $next.predecessor){throw 'Cross-language native initial binding contract mismatch'}
'PASS actual native C++/SQLite -> production Node envelope -> production PowerShell decoder/binding; B8 not configured B33; no controller/network/task/GPU or live data mutation'
