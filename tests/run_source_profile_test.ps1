param([string]$ExportManifest='')
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/run-source-profile.psm1') -Force -ErrorAction Stop
$script:checks=0;$script:hashes=0;$script:launches=0;$script:held=$false
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw "CHECK $script:checks $why"}}
function Refused([scriptblock]$body){$before=$script:launches;$failed=$false;try{& $body|Out-Null}catch{$failed=$true};Check $failed 'explicit refusal';Check ($script:launches -eq $before) 'refusal before mock launch'}
$image='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'
$dll='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'
$sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
function Binding {
 @{uuid='GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac';sdk=$sdk;executable=($sdk+'/bin/pgn-continuous-gpu.exe');start='199932000019059617039';end='199932000019059617463';length='424';
 engine_upgrade=@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'};
 pins=@(@{path=($sdk+'/bin/pgn-continuous-gpu.exe');sha256=$image},@{path=($sdk+'/bin/cudart64_12.dll');sha256=$dll});unknown_extension='retained'}|ConvertTo-Json -Depth 8 -Compress|ConvertFrom-Json
}
$hash={param($path);Check $script:held 'all hash reads under mock worker transition';$script:hashes++;if($path -ceq ($sdk+'/bin/pgn-continuous-gpu.exe')){return $image};if($path -ceq ($sdk+'/bin/cudart64_12.dll')){return $dll};throw 'Unknown test pin'}
function MockWorker($binding,[scriptblock]$hashFile=$hash){
 $script:held=$true
 try{
  $profile=Get-PgnVerifiedRunSourceProfile -Binding $binding -HashFile $hashFile
  # This is not a process launcher. It records the point AFTER all real helper
  # admission/hash checks; no installed controller, GPU, key, SSH or file opens.
  $script:launches++
  return @{schema='pgn-native-process-1';run=('a'*32);source_profile=$profile}
 }finally{$script:held=$false}
}
$b=Binding;$before=$b|ConvertTo-Json -Depth 8 -Compress;$m=MockWorker $b
Check ($script:hashes -eq 2 -and $script:launches -eq 1) 'both actual-hash callback results precede one mock launch'
Check (($b|ConvertTo-Json -Depth 8 -Compress) -ceq $before) 'helper preserves whole launch binding including extensions'
Check (($m.source_profile.Keys|Sort-Object) -join ',' -ceq 'executable,image_sha256,protocol,schema') 'exact four-field launch ABI'
Check ($m.source_profile.schema -ceq 'pgn-run-source-profile-1' -and $m.source_profile.protocol -ceq 'pgn-source-legacy-a73' -and $m.source_profile.image_sha256 -ceq $image.ToLowerInvariant()) 'only known A73 maps to legacy'
$saved=$m|ConvertTo-Json -Depth 6 -Compress;$b.executable='C:/NOT-LAUNCHED/later.exe'
Check (($m|ConvertTo-Json -Depth 6 -Compress) -ceq $saved) 'later binding changes cannot alter captured profile'
foreach($kind in @('device','image','sdk','qualification','pin','length','range','decimal','v2declared','nulldeclared','duplicate')){
 $b=Binding
 switch($kind){
  device {$b.uuid='GPU-OTHER'}
  image {$b.executable='C:/UNKNOWN-v2.exe'}
  sdk {$b.sdk='C:/UNKNOWN-SDK'}
  qualification {$b.engine_upgrade.qualification_sha256=('0'*64)}
  pin {$b.pins[0].sha256=('0'*64)}
  length {$b.length='425'}
  range {$b.start='0';$b.end='424'}
  decimal {$b.start=123}
  v2declared {$b|Add-Member source_boundary_protocol 'pgn-source-boundary-v2'}
  nulldeclared {$b|Add-Member source_boundary_protocol $null}
  duplicate {$b.pins=@($b.pins[0],$b.pins[0])}
 }
 Refused {MockWorker $b}
}
Refused {MockWorker (Binding) {param($path);return ('f'*64)}}
Refused {MockWorker (Binding) {param($path);throw 'closed simulated hash failure'}}
Check (!$script:held) 'mock transition released on every refusal'
# Source-contract check only: NEVER dot-source/execute native-control.ps1.
$errors=$null;$tokens=$null;$path=Join-Path $PSScriptRoot '../tools/native-control.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'productive controller remains parseable'
$workers=@($ast.FindAll({param($node);$node -is [Management.Automation.Language.IfStatementAst] -and $node.Clauses[0].Item1.Extent.Text -ceq '$Action -eq ''worker'''},$true))
Check ($workers.Count -eq 1) 'one genuine worker branch'
$text=$workers[0].Clauses[0].Item2.Extent.Text
$gate=$text.IndexOf('$workerTransition=EnterControlTransition');$verify=$text.IndexOf('$sourceProfile=Get-PgnVerifiedRunSourceProfile');$launch=$text.IndexOf('$p=Start-Process');$attach=$text.IndexOf('$nextManifest.source_profile=$sourceProfile');$publish=$text.IndexOf('WriteJson $manifestPath $nextManifest')
Check ($gate -ge 0 -and $verify -gt $gate -and $launch -gt $verify -and $attach -gt $launch -and $publish -gt $attach) 'same real worker gate verifies before launch and includes tuple in first manifest publication'
Check ($text.IndexOf('Get-PgnConfiguredProfile') -lt 0) 'worker does not reacquire the non-reentrant profile gate'
if($ExportManifest){
 if(![IO.Path]::IsPathRooted($ExportManifest) -or (Test-Path -LiteralPath $ExportManifest)){throw 'Test export requires fresh absolute target'}
 [IO.File]::WriteAllText($ExportManifest,$saved,[Text.UTF8Encoding]::new($false))
}
Write-Output "PASS checks=$script:checks failures=0; real helper with synthetic hash/mock launch gate, worker parsed only, no installed image/process/SSH/GPU"
