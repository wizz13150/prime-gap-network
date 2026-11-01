param([string]$Module=(Join-Path $PSScriptRoot '../tools/desktop-gpu-context.psm1'))
$ErrorActionPreference='Stop';Import-Module $Module -Force
$script:checks=0
function Check([bool]$ok,[string]$name){if(!$ok){throw $name};$script:checks++}
$p=[pscustomobject]@{ExecutablePath='C:\Program Files\Fixture\desktop.exe'}
$cert=[pscustomobject]@{Thumbprint=('A'*40)};$signature=[pscustomobject]@{Status='Valid';SignerCertificate=$cert}
$pin=[pscustomobject]@{executable=$p.ExecutablePath;sha256=('B'*64);signer_thumbprint=$cert.Thumbprint}
$hash={param($path);return ('B'*64)}
Check (Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) $hash) 'exact configured signed image'
Check (!(Test-PgnPinnedDesktopGpuProcess $p $signature @() $hash)) 'absence grants no exception'
Check (!(Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) {param($path);return ('C'*64)})) 'changed image refused'
$signature.Status='NotSigned';Check (!(Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) $hash)) 'unsigned refused';$signature.Status='Valid'
$cert.Thumbprint='C'*40;Check (!(Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) $hash)) 'changed signer refused';$cert.Thumbprint='A'*40
$p.ExecutablePath='C:\Program Files\Fixture\other.exe';Check (!(Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) $hash)) 'different process refused';$p.ExecutablePath=$pin.executable
$pin|Add-Member NoteProperty extra 'bad';$held=$false;try{Test-PgnPinnedDesktopGpuProcess $p $signature @($pin) $hash}catch{$held=$true};Check $held 'unknown pin fields refused'
Write-Output ('PASS '+$script:checks+' pinned desktop admission checks; no real inventory or process control')
