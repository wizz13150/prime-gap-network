param([Parameter(Mandatory=$true)][string]$ModulePath)
$ErrorActionPreference='Stop'
Import-Module $ModulePath -Force
$device='GPU-'+[guid]::NewGuid().ToString('D')
$checks=0
function Probe([bool]$Busy){
 $module=$ModulePath.Replace("'","''")
 $code="`$ErrorActionPreference='Stop';Import-Module '$module' -Force;try{`$lease=Enter-PgnPhysicalGpuMutex '$device'}catch{exit 3};Exit-PgnPhysicalGpuMutex `$lease;exit 0"
 $info=[Diagnostics.ProcessStartInfo]::new()
 $info.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
 $info.Arguments='-NoProfile -NonInteractive -EncodedCommand '+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $process=[Diagnostics.Process]::Start($info)
 if(!$process.WaitForExit(15000)){$process.Kill();$process.WaitForExit();throw 'Private mutex probe startup exceeded15s'}
 $expected=if($Busy){3}else{0}
 if($process.ExitCode-ne$expected){throw ('Private mutex cross-process result differs: '+$process.ExitCode+' '+$process.StandardError.ReadToEnd())}
 $process.Dispose()
}
$lease=Enter-PgnPhysicalGpuMutex $device
try{
 if(!$lease.held){throw 'Lease was not acquired'};$checks++
 Probe $true;$checks++
}finally{Exit-PgnPhysicalGpuMutex $lease}
Probe $false;$checks++
$refused=$false;try{Enter-PgnPhysicalGpuMutex 'invalid-device'|Out-Null}catch{$refused=$true}
if(!$refused){throw 'Malformed device admitted'};$checks++
@{PASS=$true;checks=$checks;scope='Real Windows cross-process named mutex on private random UUID; no driver/GPU/real GPU mutex/file lease'}|ConvertTo-Json -Compress
