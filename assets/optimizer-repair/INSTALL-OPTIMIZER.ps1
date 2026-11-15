# Windows PowerShell 5.1. Only a tracked external page; never patches a player/DLL.
[CmdletBinding()]
param([ValidateSet('Check','Install','Restore')][string]$Mode='Check',
      [string]$EmulatorRoot, [string]$BackupFolder, [switch]$ConfirmInstall)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$utf8=New-Object System.Text.UTF8Encoding($false)
$relativePage='Assets/VOIDReferenceUI/OptimizerPage.qml'
$relativeState='.VOID-Reference-UI.json'
$icons=@('power','balanced','performance','background','session','timing')
$allowed=@($relativePage)+@($icons | ForEach-Object { 'Assets/VOIDReferenceUI/VoidOptimizerUI/icons/'+$_+'.png' })+@($relativeState)
function Assert-PlainPath([string]$Path) {
    $cursor=[IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Links/junctions are not supported: $cursor" }
        }
        $parent=Split-Path -Parent $cursor
        if ($parent -eq $cursor) { break }; $cursor=$parent
    }
}
function Read-Small([string]$Path) {
    Assert-PlainPath $Path
    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -gt 2097152) { throw "Missing/oversized file: $Path" }
    return ,([IO.File]::ReadAllBytes($Path))
}
function Digest([byte[]]$Bytes) {
    if ($null -eq $Bytes) { return 'ABSENT' }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Current([string]$Path) {
    Assert-PlainPath $Path
    if (!(Test-Path -LiteralPath $Path)) { return 'ABSENT' }
    return Digest (Read-Small $Path)
}
function Write-Atomic([string]$Path,[byte[]]$Bytes) {
    Assert-PlainPath $Path
    $directory=Split-Path -Parent $Path
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $temp=Join-Path $directory ([IO.Path]::GetRandomFileName())
    try {
        [IO.File]::WriteAllBytes($temp,$Bytes)
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp,$Path,[System.Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temp,$Path) }
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
}
function Closed {
    if (@(Get-Process -Name 'HD-Player','VOID-Optimizer' -ErrorAction SilentlyContinue).Count) {
        throw 'Restore/close optimizer helpers, then close every HD-Player instance before changing UI files.'
    }
}
function Json-Bytes($Object) { return ,($utf8.GetBytes(($Object | ConvertTo-Json -Depth 80)+"`n")) }
function Main {
    if (!$EmulatorRoot) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog=New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description='Select the emulator folder containing HD-Player.exe (not its Assets folder)'
        try { if ($dialog.ShowDialog() -ne 'OK') { throw 'Cancelled. No changes.' }; $script:EmulatorRoot=$dialog.SelectedPath } finally { $dialog.Dispose() }
    }
    $root=[IO.Path]::GetFullPath($EmulatorRoot).TrimEnd('\','/')
    Assert-PlainPath $root
    if (!(Test-Path -LiteralPath (Join-Path $root 'HD-Player.exe') -PathType Leaf)) { throw 'Select the folder containing HD-Player.exe.' }
    if ($Mode -eq 'Restore') {
        Closed
        if (!$BackupFolder) { $script:BackupFolder=Read-Host 'Paste the exact backup folder printed by INSTALL-OPTIMIZER' }
        $backup=[IO.Path]::GetFullPath($BackupFolder)
        if ((Split-Path -Parent $backup) -ne $root -or (Split-Path -Leaf $backup) -notlike '.VOID-Optimizer-Studio04-*') { throw 'Backup must be a Studio04 backup directly inside this emulator root.' }
        $manifest=$utf8.GetString((Read-Small (Join-Path $backup 'manifest.json'))) | ConvertFrom-Json
        if ($manifest.format -ne 1 -or $manifest.root -ne $root -or @($manifest.files).Count -ne $allowed.Count) { throw 'Invalid/wrong-install backup.' }
        $seen=@{}
        foreach ($entry in $manifest.files) {
            if ($entry.path -notin $allowed -or $seen.ContainsKey($entry.path)) { throw 'Invalid/duplicate backup path.' }
            $seen[$entry.path]=$true
            if ((Current (Join-Path $root $entry.path)) -notin @($entry.after,$entry.before)) { throw "Changed since install; restore stopped before writing: $($entry.path)" }
            if ($entry.before -ne 'ABSENT' -and (Current (Join-Path $backup $entry.path)) -ne $entry.before) { throw "Backup hash mismatch: $($entry.path)" }
        }
        foreach ($entry in $manifest.files) {
            $target=Join-Path $root $entry.path
            $observed=Current $target
            if ($observed -eq $entry.before) { continue }
            if ($observed -ne $entry.after) { throw 'File changed during restore. Backup retained.' }
            if ($entry.before -eq 'ABSENT') { if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target } }
            else { Write-Atomic $target (Read-Small (Join-Path $backup $entry.path)) }
            if ((Current $target) -ne $entry.before) { throw 'Restore read-back failed. Backup retained.' }
        }
        Write-Host 'RESTORED: prior page, private icons and exact UI state bytes. Helper registration is unchanged.'
        return
    }
    $statePath=Join-Path $root $relativeState
    $stateBytes=Read-Small $statePath
    $state=$utf8.GetString($stateBytes).TrimStart([char]0xfeff) | ConvertFrom-Json
    $assetPath=Join-Path $root 'Assets/VOIDReferenceUI'
    $assetUrl=([Uri]($assetPath+'\')).AbsoluteUri
    if ($state.format -ne 1 -or $state.installation -ne $root -or $state.assets_url -ne $assetUrl -or !$state.assets -or !$state.resources) {
        throw 'State does not identify this tracked Assets/VOIDReferenceUI installation. No guessing or EXE patching. Share the UI state/loader for diagnosis.'
    }
    $shell=Join-Path $assetPath 'SettingsShell.qml'
    $null=Read-Small $shell
    $pagePath=Join-Path $root $relativePage
    $pageBytes=Read-Small $pagePath
    $tracked=$state.assets.PSObject.Properties[$relativePage]
    if (!$tracked -or $tracked.Value -ne (Digest $pageBytes)) {
        throw 'Optimizer page differs from its tracked UI state. No files changed. Share .VOID-Reference-UI.json and the active OptimizerPage.qml; do not delete state or backups.'
    }
    $desired=@{}
    $desired[$relativePage]=Read-Small (Join-Path $PSScriptRoot 'Assets/OptimizerPage.qml')
    foreach ($icon in $icons) {
        $rel='Assets/VOIDReferenceUI/VoidOptimizerUI/icons/'+$icon+'.png'
        $desired[$rel]=Read-Small (Join-Path $PSScriptRoot ('Assets/VoidOptimizerUI/icons/'+$icon+'.png'))
        $old=$state.assets.PSObject.Properties[$rel]
        $observed=Current (Join-Path $root $rel)
        if (($old -and $old.Value -ne $observed) -or (!$old -and $observed -ne 'ABSENT')) { throw "Private icon collision/changed state: $rel" }
    }
    Write-Host "Tracked UI directory: $assetPath"
    Write-Host ('Page SHA256: '+(Digest $pageBytes))
    Write-Host 'This verifies tracked external assets, not the binary loader or live emulator cache.'
    if ($Mode -eq 'Check') {
        $matches=$true
        foreach ($rel in $desired.Keys) { if ((Current (Join-Path $root $rel)) -ne (Digest $desired[$rel])) { $matches=$false } }
        if ($matches) { Write-Host 'PAGE OK: exact Studio04 page and isolated PNG assets match this pack.' }
        else { Write-Host 'PAGE OLD: run INSTALL-OPTIMIZER.cmd from this pack.' }
        $key='HKCU:\Software\Classes\voidoptimizer-v3\shell\open\command'
        $actual=if (Test-Path $key) { (Get-Item $key).GetValue('') } else { '' }
        $expected='"'+(Join-Path $PSScriptRoot 'VOID-Optimizer.exe')+'" --request "%1"'
        $report=$utf8.GetString((Read-Small (Join-Path $PSScriptRoot 'BUILD-REPORT.json'))) | ConvertFrom-Json
        $helperOk=(Current (Join-Path $PSScriptRoot 'VOID-Optimizer.exe')) -eq $report.helperSha256
        if ($actual -ceq $expected -and $helperOk) { Write-Host 'LINK OK: v3 points to this pack helper and its build-report hash matches.' }
        else { Write-Host 'LINK NOT VERIFIED: run SETUP-HELPER.cmd. A different owner must unregister itself; no registry override.' }
        Write-Host 'LIVE CHECK: restart Settings and look for STUDIO 04 / HELPER V3. If absent, stop and share the active loader; do not copy into other guessed folders.'
        if (!$matches -or !$helperOk -or $actual -cne $expected) { throw 'Check incomplete. Read the PAGE and LINK results above.' }
        return
    }
    Closed
    Write-Host 'Changes: optimizer page, six PRIVATE PNG icons, and only their hash entries in .VOID-Reference-UI.json.'
    Write-Host 'No SettingsShell, shared Icons/Theme, player EXE, DLL, boot or native navigation changes.'
    if (!$ConfirmInstall -and (Read-Host 'Type INSTALL to back up and replace these tracked assets') -cne 'INSTALL') { throw 'Cancelled. No changes.' }
    $backup=Join-Path $root ('.VOID-Optimizer-Studio04-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N'))
    $entries=@()
    foreach ($rel in @($desired.Keys)) { $state.assets | Add-Member -MemberType NoteProperty -Name $rel -Value (Digest $desired[$rel]) -Force }
    $desired[$relativeState]=Json-Bytes $state
    foreach ($rel in $allowed) {
        $target=Join-Path $root $rel
        $before=Current $target
        if ($rel -eq $relativePage -and $before -ne (Digest $pageBytes)) { throw 'Page changed during preflight.' }
        if ($rel -eq $relativeState -and $before -ne (Digest $stateBytes)) { throw 'State changed during preflight.' }
        $entry=[pscustomobject]@{path=$rel;before=$before;after=(Digest $desired[$rel])}
        $entries+= $entry
        if ($before -ne 'ABSENT') {
            Write-Atomic (Join-Path $backup $rel) (Read-Small $target)
            if ((Current (Join-Path $backup $rel)) -ne $before) { throw 'Backup read-back mismatch; install stopped before changing UI.' }
        }
    }
    Write-Atomic (Join-Path $backup 'manifest.json') (Json-Bytes ([pscustomobject]@{format=1;root=$root;files=$entries}))
    Write-Host "BACKUP (keep): $backup"
    $written=@()
    try {
        foreach ($entry in $entries) {
            $target=Join-Path $root $entry.path
            if ((Current $target) -ne $entry.before) { throw "File changed during install: $($entry.path)" }
            $written+= $entry
            Write-Atomic $target $desired[$entry.path]
            if ((Current $target) -ne $entry.after) { throw "Read-back failed: $($entry.path)" }
        }
    } catch {
        foreach ($entry in @($written | Select-Object -Last 100)) {
            $target=Join-Path $root $entry.path
            $now=Current $target
            if ($now -eq $entry.before) { continue }
            if ($now -ne $entry.after) { Write-Warning "Concurrent change retained; manual recovery needed: $target"; continue }
            if ($entry.before -eq 'ABSENT') { Remove-Item -LiteralPath $target }
            else { Write-Atomic $target (Read-Small (Join-Path $backup $entry.path)) }
        }
        throw
    }
    Write-Host 'INSTALLED and hashes read back. Run SETUP-HELPER.cmd, then CHECK-INSTALL.cmd.'
    Write-Host 'Restart the emulator: STUDIO 04 / HELPER V3 is the required live-page confirmation.'
}
try { Main; exit 0 } catch { Write-Host ('STOPPED: '+$_.Exception.Message) -ForegroundColor Red; exit 1 }
