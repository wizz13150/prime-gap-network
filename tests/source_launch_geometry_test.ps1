param([Parameter(Mandatory=$true)][string]$Module)
$ErrorActionPreference='Stop'
Import-Module $Module -Force -ErrorAction Stop
$script:checks=0
function Check($ok,[string]$why){$script:checks++;if(!$ok){throw "CHECK $script:checks $why"}}
function Refused([scriptblock]$body,[string]$why){$failed=$false;try{& $body|Out-Null}catch{$failed=$true};Check $failed $why}
$window=[Numerics.BigInteger]::Pow(2,64);$max=$window-1;$high10=10*$window
$block=[Numerics.BigInteger]1006632960;$width=64*$block
$sdk='C:/PGN/native/20261012-gpu-owned-source-v1'
$binding=[pscustomobject]@{sdk=$sdk;executable=($sdk+'/bin/pgn-continuous-gpu.exe');
 engine_upgrade=[pscustomobject]@{policy='GPU_OWNED_HIGH10_SOURCE_V1';qualification_sha256='99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684'};
 pins=@([pscustomobject]@{path=($sdk+'/bin/pgn-continuous-gpu.exe');sha256='A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43'},[pscustomobject]@{path=($sdk+'/bin/cudart64_12.dll');sha256='9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768'});unknown_extension='preserved'}
$before=$binding|ConvertTo-Json -Depth 6 -Compress
# New discriminator goes FIRST: the previous gate accepted this interval,
# whereas the actual producer rejects its class30 floor before any CUDA call.
Refused {Assert-PgnQualifiedSourceRange $binding $high10 ($high10+1)} 'qualified gate rejects low0 length1 class30 borrow'
$g=Get-PgnSourceLaunchGeometry ($high10+20) ($high10+21)
Check ($g.remainder -ceq '0' -and $g.base_low -ceq '20' -and $g.blocks -ceq '1' -and $g.descriptors -ceq '1') 'immediately admissible low20 uses high10 contribution'
Assert-PgnQualifiedSourceRange $binding ($high10+20) ($high10+21)
foreach($low in 0..19){Refused {Get-PgnSourceLaunchGeometry ($high10+$low) ($high10+$low+1)} 'all earlier high10 class30 floors borrow'}
# Last representable class30 base below UINT64_MAX - 3*BlockNumbers.
# BlockNumbers is divisible by30, UINT64_MAX%30=15, and a high10
# class30-aligned low is20mod30: the exact last base is25 below the limit.
$lastBase=$max-3*$block-25
$g=Get-PgnSourceLaunchGeometry ($high10+$lastBase) ($high10+$lastBase+1)
Check ($g.remainder -ceq '0' -and $g.blocks -ceq '1' -and [Numerics.BigInteger]::Parse($g.padded_end_low) -eq $max-25) 'upper margin admits last class30-aligned base'
Assert-PgnQualifiedSourceRange $binding ($high10+$lastBase) ($high10+$lastBase+1)
Refused {Get-PgnSourceLaunchGeometry ($high10+$lastBase+30) ($high10+$lastBase+31)} 'next aligned base exceeds exact blocks+2 margin'
Refused {Assert-PgnQualifiedSourceRange $binding ($high10+$lastBase+30) ($high10+$lastBase+31)} 'capture/profile gate inherits upper margin refusal'
# Original coordinates are never rewritten into a fictitious new checkpoint.
$g=Get-PgnSourceLaunchGeometry $high10 ($high10+21) '20'
Check ($g.requested_start -ceq $high10.ToString() -and $g.requested_end -ceq ($high10+21).ToString() -and $g.initial_positions -ceq '20' -and $g.resume_start -ceq ($high10+20).ToString() -and $g.remaining -ceq '1') 'explicit saved position changes geometry, not original identity'
Assert-PgnQualifiedSourceRange $binding $high10 ($high10+21) '20'
foreach($value in @('01','-1','18446744073709551616','21',0,1.0,$null)){Refused {Get-PgnSourceLaunchGeometry $high10 ($high10+21) $value} 'saved position must be canonical exact string below length'}
Refused {Get-PgnSourceLaunchGeometry ($high10+20) ($high10+20)} 'zero length refuses'
Refused {Get-PgnSourceLaunchGeometry ($high10+20) ($high10+19)} 'descending range refuses'
Refused {Get-PgnSourceLaunchGeometry ($high10-$window+20) ($high10-$window+21)} 'unsupported high word refuses'
Refused {Get-PgnSourceLaunchGeometry ($high10+$max-1) ($high10+$window)} 'one-past-low64 endpoint refuses as producer does'
$g=Get-PgnSourceLaunchGeometry ($high10+20) ($high10+20+4194304*$block)
Check ($g.blocks -ceq '4194304' -and $g.descriptors -ceq '65536') 'pure geometry exact maximum block count, no allocation'
Refused {Get-PgnSourceLaunchGeometry ($high10+20) ($high10+21+4194304*$block)} 'next block exceeds producer bound'
Refused {Assert-PgnQualifiedSourceRange $binding ($high10+20) ($high10+20+4194304*$block)} 'geometric API never enlarges existing qualified1e15 policy'
$start=[Numerics.BigInteger]::Parse('199932000000000000000');$end=$start+[Numerics.BigInteger]::Pow(10,15)
$g=Get-PgnSourceLaunchGeometry $start $end
Check ($g.blocks -ceq '993411' -and $g.descriptors -ceq '15523' -and $g.remainder -ceq '0') 'actual full-request diagnostic geometry remains admissible'
Assert-PgnQualifiedSourceRange $binding $start $end
$g=Get-PgnSourceLaunchGeometry $start $end '193273528320'
Check ($g.descriptors -ceq '15520' -and $g.resume_start -ceq '199932000193273528320' -and $g.initial_positions -ceq '193273528320') 'actual STOP3 resume geometry retains original full interval'
$tail=[Numerics.BigInteger]::Parse('199932999868386508800')
$g=Get-PgnSourceLaunchGeometry $tail $end
Check ($g.blocks -ceq '131' -and $g.descriptors -ceq '3' -and $g.remaining -ceq '131613491200') 'proposed tail3 diagnostic remains admissible'
Assert-PgnQualifiedSourceRange $binding $tail $end
$small=[Numerics.BigInteger]::Parse('199932000019059617039');$smallEnd=[Numerics.BigInteger]::Parse('199932000147908635890')
$g=Get-PgnSourceLaunchGeometry $small $smallEnd
Check ($g.descriptors -ceq '2' -and $g.remainder -ceq '29') 'already-qualified small2 descriptor interval retains alignment'
$g=Get-PgnSourceLaunchGeometry $small $smallEnd '64424509411'
Check ($g.descriptors -ceq '1' -and $g.remainder -ceq '0' -and $g.remaining -ceq '64424509440') 'small2 actual checkpoint resume remains exact'
Check (($binding|ConvertTo-Json -Depth 6 -Compress) -ceq $before) 'gate does not mutate profile binding/extensions'
# The cross-language arithmetic is intentionally duplicated, not a claim that
# PowerShell calls CUDA's implementation. Pin the exact producer expressions
# that this small admission mirror depends on, not unrelated source bytes.
$producer=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/continuous-gpu-runtime.cu'))
$compact=$producer -replace '\s',''
foreach($expression in @(
 'constexpruint64_tWords=(1ULL<<28)/64,BlockNumbers=30ULL<<25;',
 'constexprunsignedK=64,SlotBlocks=K+1,Capacity=4096;',
 'if(length>UINT64_MAX-requested.low)',
 'prior.positions>=length',
 'remaining=length-saved.positions,resumeLow=requested.low+saved.positions;',
 'remainder=(resumeLow%30+(requested.high%30)*16)%30,baseLow=resumeLow-remainder;',
 'if(resumeLow<remainder)',
 'blocks=(remaining+remainder+BlockNumbers-1)/BlockNumbers;',
 'if(blocks>4194304||baseLow>UINT64_MAX-(blocks+2)*BlockNumbers)'
)){Check ($compact.Contains($expression)) 'producer admission/stride expression unchanged; review mirror if changed'}
Write-Output "PASS checks=$script:checks failures=0; shared host-geometry preflight/source reads only; no checkpoint/image/driver/worker child/GPU I/O"
