param([Parameter(Mandatory=$true)][string]$SetupPath, [Parameter(Mandatory=$true)][string]$SourceExe)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:BSI_DISPOSABLE_INSTALL_TEST -ne '1') {
    throw 'Installer lifecycle tests are restricted to explicitly marked disposable hosted CI machines; they must not overwrite a user installation registration.'
}
$setup = (Resolve-Path -LiteralPath $SetupPath).Path
$root = Join-Path $env:RUNNER_TEMP ('BSI-setup-test-' + [guid]::NewGuid().ToString('N'))
$dest = Join-Path $root 'installed'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$log = Join-Path $root 'install.log'
# Do not use /NOICONS: shortcut creation is one of the things being exercised.
$args = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOLAUNCH', "/DIR=`"$dest`"", "/LOG=`"$log`"")
$p = Start-Process -FilePath $setup -ArgumentList $args -PassThru
if (-not $p.WaitForExit(180000)) { $p.Kill(); throw "Setup timed out; log: $log" }
if ($p.ExitCode -notin @(0,3010)) { Get-Content $log -Tail 50; throw "Setup failed with exit $($p.ExitCode)" }
$exe = Join-Path $dest 'BSI-Customizer.exe'
if (-not (Test-Path $exe)) { throw 'Setup did not install BSI-Customizer.exe' }
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $SourceExe -Algorithm SHA256).Hash) {
    throw 'The installed EXE differs from the verified build artifact'
}
& "$PSScriptRoot/Test-Manifest.ps1" -ExecutablePath $exe
if (-not (Test-Path (Join-Path $dest 'unins000.exe'))) { throw 'Setup did not install its uninstaller' }
# Exercise an in-place upgrade, preserving the AppId and existing installation.
$p = Start-Process -FilePath $setup -ArgumentList $args -Wait -PassThru
if ($p.ExitCode -notin @(0,3010)) { throw "Reinstall/upgrade failed: $($p.ExitCode)" }
$uninstaller = Join-Path $dest 'unins000.exe'
$p = Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Wait -PassThru
if ($p.ExitCode -notin @(0,3010)) { throw "Uninstall failed: $($p.ExitCode)" }
if (Test-Path $exe) { throw 'Uninstaller left the installed program behind' }
Write-Output 'PASS: actual silent install, manifest, reinstall/upgrade and uninstall'
