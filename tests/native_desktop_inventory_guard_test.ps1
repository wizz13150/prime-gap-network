param([string]$Controller=(Join-Path $PSScriptRoot '../tools/native-control.ps1'))
$ErrorActionPreference='Stop'
# Execute the actual inventory foreach body with closed injected observations.
# No controller top-level, process inventory, signature query, GPU or launch.
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Controller,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Controller parse error'}
$loops=@($ast.FindAll({param($n);$n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Variable.Extent.Text -ceq '$line' -and $n.Condition.Extent.Text -ceq '$busy'},$true))
if($loops.Count-ne1){throw 'Actual unique GPU inventory loop unavailable'}
$loop=[scriptblock]::Create($loops[0].Extent.Text)
$script:cim=0;$script:signature=0;$checks=0
function Get-PgnGpuProcessIdentity {param($ProcessId);$script:cim++;if($script:cim-eq1){return $script:first};return $script:second}
function Get-AuthenticodeSignature {param($LiteralPath,$ErrorAction);$script:signature++;if([string]::IsNullOrWhiteSpace($LiteralPath)){throw 'Unexpected unsafe signature call'};return @{Status='Valid'}}
function Test-PgnDesktopGpuProcess {param($Process,$Signature,$WindowsRoot,$ProgramFilesRoot);return $script:admittedDesktop}
function Test-PgnPinnedDesktopGpuProcess {param($Process,$Signature,$ApprovedImages,$HashFile);return $false}
$binding=[pscustomobject]@{ambient_gpu_contexts=@()}
function Verify([string]$name,$first,$second,[bool]$allowed,[string]$expected,[int]$signatures,[string]$line='FIXTURE-UUID,4'){
 $script:first=$first;$script:second=$second;$script:admittedDesktop=$allowed;$script:cim=0;$script:signature=0
 $busy=@($line);$failure=$null
 try{& $loop}catch{$failure=$_.Exception.Message}
 if(($expected -and (!$failure-or!$failure.Contains($expected))) -or (!$expected-and$failure) -or $script:signature-ne$signatures){throw ('Fixture '+$name+' unexpected outcome: '+$failure+' signatures='+$script:signature)}
 $script:checks++
}
foreach($path in @($null,'','  ')){
 $p=[pscustomobject]@{ExecutablePath=$path;CreationDate='fixture-birth'}
 Verify 'opaque-path' $p $p $true 'uninspectable process' 0
}
Verify 'exited-before-CIM' $null $null $false '' 0
$p=[pscustomobject]@{ExecutablePath='C:/FIXTURE/desktop.exe';CreationDate='fixture-birth'}
Verify 'unqualified-inspectable' $p $p $false 'unqualified process' 1
Verify 'known-desktop-identity-stable' $p $p $true '' 1
Verify 'desktop-exits-after-signature' $p $null $true '' 1
$changed=[pscustomobject]@{ExecutablePath=$p.ExecutablePath;CreationDate='reused-pid'}
Verify 'pid-reuse' $p $changed $true 'PID identity changed' 1
$changed=[pscustomobject]@{ExecutablePath='C:/FIXTURE/replacement.exe';CreationDate=$p.CreationDate}
Verify 'image-change' $p $changed $true 'PID identity changed' 1
Verify 'malformed-observation' $p $p $true 'Malformed GPU process identity' 0 'FIXTURE-UUID,unknown'
Write-Output ('PASS '+$script:checks+' actual inventory-loop closed cases; no real CIM/signature/driver/process control; opaque path HOLD before signature')
