param([Parameter(Mandatory=$true)][string]$FixtureDirectory)
$ErrorActionPreference='Stop'
if((Split-Path $FixtureDirectory -Leaf) -cnotmatch '^root554-native-resume-contract(?:-v[2-9])?$'){throw 'Explicit TESTONLY native resume fixture required'}
Import-Module (Join-Path $PSScriptRoot '../tools/checkpoint-resume-binding.psm1') -Force -ErrorAction Stop
$path=Join-Path $FixtureDirectory 'admission.base64'
if((Get-Item -LiteralPath $path).Length -gt 10924){throw 'Native fixture exceeds resume wire bound'}
$admission=Read-PgnCheckpointResumeAdmission ([IO.File]::ReadAllText($path,[Text.UTF8Encoding]::new($false,$true)))
$receipt=[IO.File]::ReadAllText((Join-Path $FixtureDirectory 'receipt.json'),[Text.UTF8Encoding]::new($false,$true))|ConvertFrom-Json
if($admission.receipt.run -cne $receipt.run -or $admission.receipt.previous_run -cne $receipt.previous_run -or
   $admission.receipt.checkpoint.resume.tests -cne '9007199254741995' -or $admission.receipt.checkpoint.checkpoint_base64 -cne $receipt.checkpoint.checkpoint_base64){throw 'Native/Node/PS checkpoint contract changed'}
# This fixture deliberately tests >2^53 positions outside the qualified1e15
# GPU slice. Decoding it is not permission to run it on the physical producer.
$binding=[pscustomobject]@{engine_upgrade=[pscustomobject]@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'};executable='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sdk='C:/PGN/native/20261012-gpu-owned-source-v1';pins=@([pscustomobject]@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/pgn-continuous-gpu.exe';sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},[pscustomobject]@{path='C:/PGN/native/20261012-gpu-owned-source-v1/bin/cudart64_12.dll';sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'})}
$rejected=$false
try{Assert-PgnQualifiedSourceBinding $binding ([System.Numerics.BigInteger]::Parse($receipt.checkpoint.resume.start)) ([System.Numerics.BigInteger]::Parse($receipt.checkpoint.resume.end))}catch{$rejected=$true}
if(!$rejected){throw 'Synthetic oversized fixture must NOT be admitted as qualified physical slice'}
Write-Output 'PASS actual native JSON -> production Node -> PS5 exact decoder; oversized GPU launch correctly refused, no hardware/lineage proof'
