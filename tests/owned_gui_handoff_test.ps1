$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/owned-gui-handoff.ps1')
$script:operations=[Collections.Generic.List[string]]::new()
$script:failure=''
$script:checks=0
$old=[pscustomobject]@{Id=101}
$new=[pscustomobject]@{Id=202}
function Assert-PgnGuiIdentity($identity){
    $script:operations.Add('identity:'+ $identity.Id)
    $script:checks++
    if($script:failure -eq 'recycled' -and $script:checks -eq 3){throw 'Recycled old PID'}
}
function Read-PgnGuiQualification($identity){
    $script:operations.Add('inspect:'+ $identity.Id)
    if($script:failure -eq 'inspect'){throw 'Endpoint unavailable'}
    @{gui_pid=if($script:failure -eq 'wrong-pid'){999}else{202};
      window_visible=($script:failure -eq 'visible');
      pgn_wallet_frontend_loaded=($script:failure -ne 'wallet');
      pgn_wallet_frontend_error=if($script:failure -eq 'wallet-error'){'Unavailable'}else{''};
      pgn_relay_error=if($script:failure -eq 'relay-error'){'Unavailable'}else{''}}
}
function Close-PgnGui($identity){
    $script:operations.Add('close:'+ $identity.Id)
    if($script:failure -eq 'close-old' -and $identity.Id -eq 101){throw 'Old GUI still visible'}
}
function Show-PgnGui($identity,$section){$script:operations.Add('show:'+ $identity.Id+':'+$section)}
$start={ $script:operations.Add('start-hidden:202'); $new }
foreach($case in @('','inspect','wrong-pid','visible','wallet','wallet-error','relay-error','recycled','close-old')){
    $script:failure=$case;$script:checks=0;$script:operations.Clear();$thrown=$false
    try {$result=Invoke-PgnGuiHandoff $old $start 'Mining'}catch{$thrown=$true}
    if(!$case){
        if($thrown -or $result.Id -ne 202){throw 'Qualified replacement rejected'}
        if($script:operations.IndexOf('inspect:202') -gt $script:operations.IndexOf('close:101') -or
           $script:operations.IndexOf('close:101') -gt $script:operations.IndexOf('show:202:Mining')){throw 'Lifecycle order violated'}
    }else{
        if(!$thrown -or $script:operations.Contains('show:202:Mining') -or !$script:operations.Contains('close:202')){throw ('Unsafe failure path:'+ $case)}
        if($case -ne 'close-old' -and $script:operations.Contains('close:101')){throw ('Old GUI changed before qualification:'+ $case)}
    }
}
Write-Output 'PASS9 owned GUI handoff cases: hidden qualification -> exact close -> show; rejection preserves old UI and cleans replacement; no GPU operation'
$good=@{gui_pid=202;window_visible=$true;window_minimized=$false}
Assert-PgnShownReceipt $new $good ([IntPtr]123)
foreach($case in @('missing-flags','invisible','minimized','wrong-pid','native-hidden')){
    $receipt=$good.Clone();$handle=[IntPtr]123;$rejected=$false
    switch($case){
        'missing-flags'{$receipt.Remove('window_visible')}
        'invisible'{$receipt.window_visible=$false}
        'minimized'{$receipt.window_minimized=$true}
        'wrong-pid'{$receipt.gui_pid=999}
        'native-hidden'{$handle=[IntPtr]::Zero}
    }
    try{Assert-PgnShownReceipt $new $receipt $handle}catch{$rejected=$true}
    if(!$rejected){throw ('False visible claim accepted:'+ $case)}
}
Write-Output 'PASS6 show receipt cases: both logical Qt visibility and native Windows main window required'
