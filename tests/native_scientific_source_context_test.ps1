param([Parameter(Mandatory=$true)][string]$WorkFile)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../tools/run-source-profile.psm1') -Force
function Clone($v){$v|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$script:checks=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw $why}}
$w=Get-Content -Raw -LiteralPath $WorkFile|ConvertFrom-Json
$b=Clone @{source_kind='NATIVE_PGN_SCIENCE';native_work=$w;start=$w.start;length=$w.length;end=([Numerics.BigInteger]::Parse($w.start)+[Numerics.BigInteger]::Parse($w.length)).ToString();engine_upgrade=@{policy='GPU_OWNED_HIGH47_BARRETT_TWO_LANES_FUSED_SQUARE_V1'}}
$before=$b|ConvertTo-Json -Depth 12 -Compress
$c=Get-PgnNativeScientificSourceContext $b
Check ($c.schema-ceq'pgn-native-scientific-source-context-1'-and$c.rpc_work_sha256-cmatch'\A[0-9a-f]{64}\z') 'actual work context captured'
Check (($b|ConvertTo-Json -Depth 12 -Compress)-ceq$before) 'capture never rewrites binding/public work'
Check (($c.rpc_work|ConvertTo-Json -Depth 6 -Compress)-ceq($w|ConvertTo-Json -Depth 6 -Compress)) 'all actual work fields survive'
Check ($c.authority-ceq'SEARCH_CONTEXT_NOT_NATIVE_ACCEPTANCE_OR_RANGE_CERTIFICATE') 'search is not native acceptance'
foreach($poison in @('missingwork','kind','domain','start','end','length','accepted','schema','parent','zero-target','floatingheight','excesssegments','script','unknown','challenge')){
 $x=Clone $b
 switch($poison){
  missingwork {$x.PSObject.Properties.Remove('native_work')}
  kind {$x.source_kind='COMMUNITY'}
  domain {$x.engine_upgrade.policy='GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_FUSED_SQUARE_V1'}
  start {$x.start='878591000000000000001'}
  end {$x.end='878592000000000000001'}
  length {$x.length='1'}
  accepted {$x.native_work.accepted=$true}
  schema {$x.native_work.schema='UNKNOWN'}
  parent {$x.native_work.parent='X'*64}
  zero-target {$x.native_work.target_numerator='0'}
  floatingheight {$x.native_work.height=[double]5}
  excesssegments {$x.native_work.segments=4097}
  script {$x.native_work.science_policy_script='0'}
  unknown {$x.native_work|Add-Member NoteProperty unsupported 'value'}
  challenge {$x.native_work.challenge="line`nother"}
 }
 $failed=$false;try{Get-PgnNativeScientificSourceContext $x|Out-Null}catch{$failed=$true};Check $failed ('poison must refuse before launch: '+$poison)
}
$legacy=Clone $b;$legacy.PSObject.Properties.Remove('native_work');$legacy.engine_upgrade.policy='GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_FUSED_SQUARE_V1'
Check ($null-eq(Get-PgnNativeScientificSourceContext $legacy)) 'legacy high10 without native context remains unchanged'
Write-Output ('PASS '+$script:checks+' native work identity/range/canonical/poison checks; public metadata only, no GPU/RPC/chain mutation')
