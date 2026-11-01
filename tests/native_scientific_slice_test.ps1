param([Parameter(Mandatory=$true)][string]$ModulePath,[Parameter(Mandatory=$true)][string]$WorkFile)
$ErrorActionPreference='Stop';Import-Module $ModulePath -Force
function Clone($v){$v|ConvertTo-Json -Depth 12 -Compress|ConvertFrom-Json}
$work=Get-Content -LiteralPath $WorkFile -Raw|ConvertFrom-Json
$origin=[Numerics.BigInteger]::Parse($work.start);$length=[Numerics.BigInteger]::Parse($work.length)
$first=$origin+[Numerics.BigInteger]::Divide($length,4);$last=$first+[Numerics.BigInteger]::Divide($length,1000)
$binding=Clone @{source_kind='NATIVE_PGN_SCIENCE';native_work=$work;start=$first.ToString();end=$last.ToString();length=($last-$first).ToString();engine_upgrade=@{policy='ADAPTIVE_PRIME_GAP_67_95'};engine=@{schema='pgn-prime-gap-engine-1'};native_search_slice=@{schema='pgn-native-search-slice-1';work_id=$work.work_id;start=$first.ToString();end=$last.ToString()}}
$before=$binding|ConvertTo-Json -Depth 12 -Compress
$context=Get-PgnNativeScientificSourceContext $binding
if($context.schema-cne'pgn-native-scientific-source-context-2'-or($context.rpc_work|ConvertTo-Json -Depth 8 -Compress)-cne($work|ConvertTo-Json -Depth 8 -Compress)){throw 'Slice changed full assigned work'}
if(($binding|ConvertTo-Json -Depth 12 -Compress)-cne$before){throw 'Slice inspection mutated input'}
$checks=2
foreach($poison in @('left','right','reverse','identity','start','end','length','field','schema','legacy','accepted')){
 $x=Clone $binding
 switch($poison){
  left {$x.native_search_slice.start=($origin-1).ToString()}
  right {$x.native_search_slice.end=($origin+$length+1).ToString()}
  reverse {$x.native_search_slice.end=$x.native_search_slice.start}
  identity {$x.native_search_slice.work_id='a'*64}
  start {$x.start=($first+1).ToString()}
  end {$x.end=($last+1).ToString()}
  length {$x.length='1'}
  field {$x.native_search_slice|Add-Member NoteProperty extra 'unknown'}
  schema {$x.native_search_slice.schema='unknown'}
  legacy {$x.engine.schema='unknown';$x.engine_upgrade.policy='GPU_OWNED_HIGH47_BARRETT_TWO_LANES_FUSED_SQUARE_V1'}
  accepted {$x.native_work.accepted=$true}
 }
 $failed=$false;try{Get-PgnNativeScientificSourceContext $x|Out-Null}catch{$failed=$true}
 if(!$failed){throw ('Invalid slice admitted: '+$poison)};$checks++
}
@{PASS=$true;checks=$checks;scope='Full assigned work immutable, exact physical subset, no network acceptance/whole-range completion'}|ConvertTo-Json -Compress
