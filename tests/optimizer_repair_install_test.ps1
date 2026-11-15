# Real Windows filesystem/state tests. Never registers protocols or launches tuning.
$ErrorActionPreference='Stop'
$scriptPath=Join-Path $PSScriptRoot '../assets/optimizer-repair/INSTALL-OPTIMIZER.ps1'
$pack=Join-Path $PSScriptRoot '../assets/optimizer-repair'
$root=Join-Path ([IO.Path]::GetTempPath()) ('void studio test '+[Guid]::NewGuid().ToString('N'))
$utf8=New-Object Text.UTF8Encoding($false)
function Need($Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Run([string[]]$Options,[int]$Expected) {
    $script:lastOutput=(& powershell.exe -NoLogo -NoProfile -File $scriptPath -EmulatorRoot $root @Options 2>&1 | Out-String)
    Write-Host $script:lastOutput
    Need ($LASTEXITCODE -eq $Expected) ('Unexpected installer exit '+$LASTEXITCODE+' for '+($Options -join ' '))
}
try {
    $assets=Join-Path $root 'Assets/VOIDReferenceUI'
    New-Item -ItemType Directory -Path $assets -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'HD-Player.exe'),'fixture, never executed')
    [IO.File]::WriteAllText((Join-Path $assets 'SettingsShell.qml'),'untouched shell fixture')
    [IO.File]::WriteAllText((Join-Path $root 'Assets/OptimizerPage.qml'),'wrong top-level page must remain untouched')
    $page=Join-Path $assets 'OptimizerPage.qml'
    [IO.File]::WriteAllText($page,'old page fixture')
    $stateFile=Join-Path $root '.VOID-Reference-UI.json'
    $originalPage=Hash $page
    $state=@{format=1;edition=5;installation=$root;assets_url=([Uri]($assets+'\')).AbsoluteUri;
        assets=@{'Assets/VOIDReferenceUI/OptimizerPage.qml'=$originalPage;'Assets/VOIDReferenceUI/SettingsShell.qml'=(Hash (Join-Path $assets 'SettingsShell.qml'))};
        resources=@{'UiSettingsWindow.qml'=@{sha256=('a'*64);container='HD-Player.exe'}};
        extra=@{preserve='unknown extension field'}}
    [IO.File]::WriteAllText($stateFile,($state|ConvertTo-Json -Depth 15),$utf8)
    $originalState=Hash $stateFile
    $wrong=Hash (Join-Path $root 'Assets/OptimizerPage.qml')
    Run @('-Mode','Check') 1
    # Refuse stale page state before touching files.
    [IO.File]::AppendAllText($page,'external change')
    Run @('-Mode','Install','-ConfirmInstall') 1
    Need ((Hash $stateFile) -eq $originalState) 'Refusal changed state'
    Need (@(Get-ChildItem $root -Force -Filter '.VOID-Optimizer-Studio04-*').Count -eq 0) 'Refusal created backup'
    [IO.File]::WriteAllText($page,'old page fixture')
    # Wrong assets_url must not redirect the installer.
    $state.assets_url='file:///C:/another-install/Assets/VOIDReferenceUI/'
    [IO.File]::WriteAllText($stateFile,($state|ConvertTo-Json -Depth 15),$utf8)
    Run @('-Mode','Install','-ConfirmInstall') 1
    $state.assets_url=([Uri]($assets+'\')).AbsoluteUri
    [IO.File]::WriteAllText($stateFile,($state|ConvertTo-Json -Depth 15),$utf8)
    Run @('-Mode','Install','-ConfirmInstall') 0
    Need ((Hash $page) -eq (Hash (Join-Path $pack 'Assets/OptimizerPage.qml'))) 'Wrong page installed'
    Need ((Hash (Join-Path $root 'Assets/OptimizerPage.qml')) -eq $wrong) 'Top-level decoy touched'
    $after=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    Need ($after.extra.preserve -eq 'unknown extension field') 'Unknown state fields lost'
    Need ($after.resources.'UiSettingsWindow.qml'.sha256 -eq ('a'*64)) 'Native resource state changed'
    Need ($after.assets.'Assets/VOIDReferenceUI/OptimizerPage.qml' -eq (Hash $page)) 'Page state not updated'
    foreach ($icon in @('power','balanced','performance','background','session','timing')) {
        $rel='Assets/VOIDReferenceUI/VoidOptimizerUI/icons/'+$icon+'.png'
        Need ($after.assets.$rel -eq (Hash (Join-Path $root $rel))) 'Icon state mismatch'
    }
    $backup=@(Get-ChildItem $root -Force -Directory -Filter '.VOID-Optimizer-Studio04-*')[0].FullName
    # Changed-file restore refusal leaves every file intact.
    [IO.File]::AppendAllText($page,'later change')
    $changed=Hash $page
    Run @('-Mode','Restore','-BackupFolder',$backup) 1
    Need ((Hash $page) -eq $changed) 'Refused restore overwrote changed page'
    Copy-Item -LiteralPath (Join-Path $pack 'Assets/OptimizerPage.qml') -Destination $page -Force
    Run @('-Mode','Restore','-BackupFolder',$backup) 0
    Need ((Hash $page) -eq $originalPage) 'Page not restored exactly'
    Need ((Hash $stateFile) -eq $originalState) 'State not restored byte-for-byte'
    Need (!(Test-Path (Join-Path $assets 'VoidOptimizerUI/icons/power.png'))) 'Added icon not removed'
    Write-Host 'PASS: actual Windows install/read-back, stale-state/wrong-root refusal, state field preservation, decoy isolation, changed-file restore refusal and exact restore. No real emulator/helper/registry activity.'
} catch {
    $details=($_.Exception.Message+"`n"+$script:lastOutput).Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host ('::error::'+$details)
    throw
} finally { if (Test-Path $root) { Remove-Item -LiteralPath $root -Recurse -Force } }
