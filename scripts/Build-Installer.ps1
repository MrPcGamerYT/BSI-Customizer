#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PortableExe,
    [Parameter(Mandatory)][string]$Version,
    [string]$OutputDirectory = 'build/installer',
    [string]$AdbDirectory,
    [string]$IsccPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot -Parent
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Version must be numeric, e.g. 3.0.1' }
$source = (Get-Item -LiteralPath $PortableExe).FullName
if ([IO.Path]::GetExtension($source) -ne '.exe') { throw 'PortableExe must be the completed single-file .exe, not a ZIP or setup.' }
# An inner Qt application with no deployed runtime is not distributable alone.
# A successful deployment test identifies our outer portable launcher.
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $source
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.ArgumentList.Add('--deployment-test')
$info.Environment['QT_QPA_PLATFORM'] = 'offscreen'
$info.Environment['QT_QUICK_BACKEND'] = 'software'
# Diagnostic only, with disposable app settings; no install/patch needs elevation.
$info.Environment['__COMPAT_LAYER'] = 'RUNASINVOKER'
$process = [Diagnostics.Process]::new()
$process.StartInfo = $info
try {
    if (-not $process.Start()) { throw 'Cannot start the portable validation probe.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(120000)) { $process.Kill($true); throw 'Portable validation timed out.' }
    $probe = $stdout.GetAwaiter().GetResult()
    $errors = $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0 -or $probe -notmatch '(?m)^DEPLOYMENT_PORTABLE=1\r?$' -or $probe -notmatch 'DEPLOYMENT TEST PASSED') {
        throw "Input is not a working BSI single-file portable build. Use build/artifacts/BSI-Customizer-<version>-win64.exe.`n$probe`n$errors"
    }
} finally { $process.Dispose() }
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (-not $IsccPath) {
    $IsccPath = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
}
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Install Inno Setup 6 first, or supply -IsccPath with the full path to ISCC.exe.'
}
# ADB is optional and external. Include only this allowlist, never adbkey,
# unrelated tools, emulator data, or the entire platform-tools directory.
if (-not $AdbDirectory -and (Test-Path -LiteralPath (Join-Path $repo 'tools/adb/adb.exe') -PathType Leaf)) {
    $AdbDirectory = Join-Path $repo 'tools/adb'
}
$defines = @("/DAppVersion=$Version", "/DSourceExe=$source", "/DOutputDir=$out")
if ($AdbDirectory) {
    $adb = (Get-Item -LiteralPath $AdbDirectory).FullName
    foreach ($file in @('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $adb $file) -PathType Leaf)) {
            throw "Missing $file in $adb. Supply a complete, trusted Windows platform-tools set with its notices."
        }
    }
    Write-Warning 'Bundling user-supplied ADB. Confirm the files are trusted, matching versions, and you have permission to redistribute them with the supplied notices.'
    $defines += "/DAdbDirectory=$adb"
}
& $IsccPath @defines (Join-Path $repo 'packaging/installer/BSI-Customizer.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }
$setup = Join-Path $out "BSI-Customizer-$Version-setup.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Installer missing: $setup" }
Write-Host "Installer: $setup"
Get-FileHash -LiteralPath $setup -Algorithm SHA256
