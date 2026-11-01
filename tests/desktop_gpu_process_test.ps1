param([string]$Controller=(Join-Path $PSScriptRoot '../tools/native-control.ps1'))
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Controller,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('Controller parse error: '+(($errors|ForEach-Object {$_.Message+' @ '+$_.Extent.Text})-join '; '))}
$fn=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Test-PgnDesktopGpuProcess'},$true))
if($fn.Count -ne 1){throw 'Exact classifier missing'}
Invoke-Expression $fn[0].Extent.Text
$script:n=0
function Check($ok,$name){$script:n++;if(!$ok){throw $name}}
$cert=[pscustomobject]@{name='Microsoft Windows'};$cert|Add-Member ScriptMethod GetNameInfo {param($a,$b)return $this.name}
$signature=[pscustomobject]@{Status='Valid';SignerCertificate=$cert}
$paths=@('C:\Windows\explorer.exe','C:\Windows\SystemApps\Microsoft.Windows.Search_cw5n1h2txyewy\SearchApp.exe','C:\Windows\SystemApps\Microsoft.Windows.StartMenuExperienceHost_cw5n1h2txyewy\StartMenuExperienceHost.exe','C:\Windows\SystemApps\Microsoft.LockApp_cw5n1h2txyewy\LockApp.exe','C:\Windows\SystemApps\MicrosoftWindows.Client.CBS_cw5n1h2txyewy\TextInputHost.exe','C:\Windows\SystemApps\ShellExperienceHost_cw5n1h2txyewy\ShellExperienceHost.exe','C:\Program Files\WindowsApps\Microsoft.YourPhone_1.25072.81.0_x64__8wekyb3d8bbwe\PhoneExperienceHost.exe')
$paths+='C:\Windows\System32\dwm.exe'
foreach($p in $paths){Check (Test-PgnDesktopGpuProcess ([pscustomobject]@{ExecutablePath=$p}) $signature 'C:\Windows' 'C:\Program Files') 'signed exact desktop path';Check (!(Test-PgnDesktopGpuProcess ([pscustomobject]@{ExecutablePath=$p.Replace('C:\','C:\Fake\')}) $signature 'C:\Windows' 'C:\Program Files')) 'same filename elsewhere refused'}
foreach($p in @('C:\PGN\miner.exe','C:\Windows\SystemApps\unknown\SearchApp.exe','C:\Program Files\WindowsApps\Microsoft.YourPhone_evil\PhoneExperienceHost.exe')){Check (!(Test-PgnDesktopGpuProcess ([pscustomobject]@{ExecutablePath=$p}) $signature 'C:\Windows' 'C:\Program Files')) 'unknown compute refused'}
$signature.Status='NotSigned';Check (!(Test-PgnDesktopGpuProcess ([pscustomobject]@{ExecutablePath=$paths[0]}) $signature 'C:\Windows' 'C:\Program Files')) 'unsigned refused'
$signature.Status='Valid';$cert.name='Other signer';Check (!(Test-PgnDesktopGpuProcess ([pscustomobject]@{ExecutablePath=$paths[0]}) $signature 'C:\Windows' 'C:\Program Files')) 'non Microsoft refused'
Write-Output "DESKTOP_GPU_PROCESS $script:n checks / 0 failures; classifier only, no process mutation or GPU launch"
