param([Parameter(Mandatory=$true)][string]$PackageRoot,[Parameter(Mandatory=$true)][string]$EntryRoot,
 [string]$ExpectedPublishedPackageRoot='C:\PGN\runtime\deployments\root563-native-v2-closure-20261015-v1')
$ErrorActionPreference='Stop';$utf8=[Text.UTF8Encoding]::new($false,$true)
$tools=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../tools'))
$script:checks=0;$script:failures=0
function Check([bool]$pass,[string]$name){$script:checks++;if(!$pass){$script:failures++;Write-Output ('FAIL '+$name)}}
function Text([string]$p){return $utf8.GetString([IO.File]::ReadAllBytes($p)).Replace("`r`n","`n")}
function Parse([string]$p){$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($p,[ref]$tokens,[ref]$errors);Check ($errors.Count -eq 0) ('actual package PS5 AST '+[IO.Path]::GetFileName($p));return $ast}
$manifestPath=Join-Path $PackageRoot 'closure.json';$manifest=(Text $manifestPath)|ConvertFrom-Json
$pointer=(Text (Join-Path $EntryRoot 'active.json'))|ConvertFrom-Json
Check ($manifest.schema -ceq 'pgn-native-controller-closure-1') 'manifest schema'
Check ($manifest.files.Count -eq 9) 'complete nine-file code closure'
Check ($manifest.code_root -ceq $ExpectedPublishedPackageRoot) 'published immutable code root explicit'
Check ($manifest.state_root -ceq 'C:\PGN\runtime') 'state root remains runtime'
Check ($manifest.entry_root -ceq 'C:\PGN\runtime\controller-code') 'entry metadata separate from state'
Check ($pointer.manifest_sha256 -ceq (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()) 'pointer pins exact manifest'
Check ($pointer.generation -ceq $manifest.generation -and $pointer.package -ceq [IO.Path]::GetFileName($PackageRoot)) 'pointer selects generation once'
foreach($pin in $manifest.files){
 $path=Join-Path $PackageRoot $pin.name;$item=Get-Item -LiteralPath $path
 Check (([string]$item.Length) -ceq $pin.bytes -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $pin.sha256) ('real packaged file pin '+$pin.name)
 $ast=Parse $path
 if($pin.name -ne 'native-control.ps1'){
  Check ((Get-FileHash -LiteralPath (Join-Path $tools $pin.name) -Algorithm SHA256).Hash -ceq (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash) ('byte-identical module '+$pin.name)
 }
}
$originalAst=Parse (Join-Path $tools 'native-control.ps1')
$controllerAst=Parse (Join-Path $PackageRoot 'native-control.ps1')
$entryAst=Parse (Join-Path $EntryRoot 'native-control.ps1')
Check ($originalAst.ParamBlock.Extent.Text.Replace("`r`n","`n") -ceq $controllerAst.ParamBlock.Extent.Text.Replace("`r`n","`n")) 'controller argv ABI unchanged'
Check ($originalAst.ParamBlock.Extent.Text.Replace("`r`n","`n") -ceq $entryAst.ParamBlock.Extent.Text.Replace("`r`n","`n")) 'entry argv ABI unchanged'
$controller=Text (Join-Path $PackageRoot 'native-control.ps1')
$source=Text (Join-Path $tools 'native-control.ps1')
Check (([regex]::Matches($controller,'Join-Path \$root ''[^'']+\.psm1''')).Count -eq 0) 'no code dependency loaded from mutable state root'
Check ($controller.Contains('$a.Arguments=''-NoProfile -NonInteractive -WindowStyle Hidden -File "''+$PSCommandPath+''" -Action worker''')) 'task worker remains pinned to immutable controller'
Check ($controller.Contains('& $PSCommandPath -Action snapshot')) 'stream retains its resolved generation'
Check ($controller.Contains('Enter-PgnClosureTransition -Closure $pgnClosure -Action $Action')) 'actual transition hook present'
Check ($controller.Contains('finally {Close-PgnControllerClosure $pgnClosure}')) 'controller lease lifetime covers complete invocation'
$entry=Text (Join-Path $EntryRoot 'native-control.ps1')
Check ($entry.Contains("Import-Module 'C:\PGN\runtime\controller-code/native-controller-closure.psm1'")) 'loader imports bound entry helper even if installed at old command path'
Check ($entry.Contains('Invoke-PgnControllerEntry') -and $entry.Contains('-ControllerArguments $PSBoundParameters')) 'loader really delegates same arguments'
# Reverse ONLY the named representation edits. Equality proves the original
# control/policy/schema/geometry/TTL bodies have not been reimplemented here.
$paramEnd=$controller.IndexOf("`n)`n")
$bodyStart=$controller.IndexOf("`ntry {`n",$paramEnd)
$end="`n} finally {Close-PgnControllerClosure `$pgnClosure}`n"
Check ($bodyStart -gt 0 -and $controller.EndsWith($end)) 'single outer lease wrapper'
$wrapperLength="`ntry {`n".Length
$body=$controller.Substring($bodyStart+$wrapperLength,$controller.Length-($bodyStart+$wrapperLength)-$end.Length)
$newTransition=@'
function EnterControlTransition {
 Enter-PgnClosureTransition -Closure $pgnClosure -Action $Action
}
'@
$oldTransition=@'
function EnterControlTransition {
 Import-Module (Join-Path $PSScriptRoot 'observed-stop-binding.psm1') -Force -ErrorAction Stop
 Enter-PgnControlTransition $root
}
'@
$body=$body.Replace($newTransition.Replace("`r`n","`n"),$oldTransition.Replace("`r`n","`n")).Replace("`$root='C:\PGN\runtime';","`$root='C:/PGN/runtime';")
$range="(Join-Path `$PSScriptRoot 'range-successor.psm1')"
Check (([regex]::Matches($body,[regex]::Escape($range))).Count -eq 3) 'exact three state-to-code import changes'
$body=$body.Replace($range,"(Join-Path `$root 'range-successor.psm1')")
$body=$body.Replace('$a.Arguments=''-NoProfile -NonInteractive -WindowStyle Hidden -File "''+$PSCommandPath+''" -Action worker''','$a.Arguments=''-NoProfile -NonInteractive -WindowStyle Hidden -File "C:\PGN\runtime\native-control.ps1" -Action worker''')
$originalParamEnd=$source.IndexOf("`n)`n")
Check ($body -ceq $source.Substring($originalParamEnd+3)) 'all original action bodies preserved after exact inverse transform'
$before=@{};foreach($file in Get-ChildItem -LiteralPath $PackageRoot -File){$before[$file.Name]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
$rejected=$false
try{& (Join-Path $tools 'new-native-controller-closure.ps1') -PackageRoot $PackageRoot -EntryRoot $EntryRoot -StateRoot 'C:/PGN/runtime'|Out-Null}catch{$rejected=$_.Exception.Message.Contains('must be absent')}
Check $rejected 'existing package refuses before write/recreation'
foreach($file in Get-ChildItem -LiteralPath $PackageRoot -File){Check ($before[$file.Name] -ceq (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) ('refused rebuild byte invariant '+$file.Name)}
[ordered]@{schema='pgn-controller-closure-package-test-1';checks=$script:checks;failures=$script:failures;scope='PARSE_AND_PIN_ACTUAL_TRANSFORMED_CONTROLLER_NO_EXECUTION_NO_STATE_ROOT_ACCESS'}|ConvertTo-Json -Compress
if($script:failures){exit 1}
