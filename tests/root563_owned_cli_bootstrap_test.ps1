param(
 [Parameter(Mandatory=$true)][string]$Image,
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-F0-9]{64}$')][string]$ExpectedSha256
)
$ErrorActionPreference='Stop'
$resolved=[IO.Path]::GetFullPath($Image)
if(!(Test-Path -LiteralPath $resolved -PathType Leaf) -or (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash -cne $ExpectedSha256){throw 'Exact candidate image required'}
$directory=Join-Path ([IO.Path]::GetTempPath()) ('pgn-owned-cli-bootstrap-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $directory
$priorPlatform=$env:QT_QPA_PLATFORM
$results=@()
try{
 # Deliberately unavailable platform: valid IPC mode must use QtCore only.
 # PID0 is rejected before any pipe connect, so no existing process is touched.
 # This is NOT a qualification of the real GUI or its platform plugins.
 $env:QT_QPA_PLATFORM='pgn-intentionally-unavailable-cli-test'
 foreach($verb in @('inspect','close')){
  $stdout=Join-Path $directory ($verb+'.stdout');$stderr=Join-Path $directory ($verb+'.stderr')
  $child=Start-Process -FilePath $resolved -ArgumentList @(('--'+$verb+'-owned'),'0') -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
  try{
   $null=$child.Handle
   if(!$child.WaitForExit(3000)){throw 'Owned CLI deadline; retain target identity, no automatic kill'}
   $code=$child.ExitCode
   if($code -ne 2){throw ('Owned CLI invalid-target exit mismatch: '+$code)}
   if((Get-Item -LiteralPath $stdout).Length -ne 0 -or (Get-Item -LiteralPath $stderr).Length -ne 0){throw 'Owned CLI unexpectedly loaded/reported a GUI or produced output'}
   $results += [pscustomobject]@{verb=$verb;pid=$child.Id;actual_exit_code=$code;stdout_bytes=0;stderr_bytes=0}
  }finally{$child.Dispose()}
 }
}finally{
 if($null -eq $priorPlatform){Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue}else{$env:QT_QPA_PLATFORM=$priorPlatform}
}
[pscustomobject]@{schema='pgn-owned-cli-bootstrap-test-1';accepted=$true;image=$resolved;sha256=$ExpectedSha256;cases=$results;artifacts=$directory;scope='OWN_CLI_INVALID_TARGET_ONLY_NOT_GUI_OR_REAL_IPC_OR_GPU_QUALIFICATION'}|ConvertTo-Json -Depth 4 -Compress
