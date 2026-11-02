[CmdletBinding()]
param([Parameter(Mandatory)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
trap {
    $message = ($_.Exception.Message + "`n" + $_.ScriptStackTrace).Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error::$message"
    throw
}
$ExecutablePath = (Resolve-Path -LiteralPath $ExecutablePath).Path
$root = Join-Path ([IO.Path]::GetTempPath()) ('BSI-onefile-test-' + [guid]::NewGuid().ToString('N'))
$clean = Join-Path $root 'folder with spaces'
New-Item -ItemType Directory -Path $clean -Force | Out-Null
$portable = Join-Path $clean 'BSI Customizer portable.exe'
Copy-Item -LiteralPath $ExecutablePath -Destination $portable
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class BSIPortableTestErrorMode {
    [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);
}
'@
$oldErrorMode = [BSIPortableTestErrorMode]::SetErrorMode(0x0001 -bor 0x0002 -bor 0x8000)
function Run-Clean([string]$File, [string[]]$Arguments, [string]$Platform = 'offscreen') {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $File
    $info.WorkingDirectory = $clean
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    foreach ($key in @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH',
                       'QT_ROOT_DIR', 'QTDIR', 'BSI_PORTABLE', 'BSI_LAUNCHER_PATH')) { $info.Environment.Remove($key) | Out-Null }
    # A forged inherited marker must be replaced by the real module path.
    if ($File -eq $portable) { $info.Environment['BSI_LAUNCHER_PATH'] = 'C:\not-the-launcher.exe' }
    $info.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $info.Environment['QT_QPA_PLATFORM'] = $Platform
    $info.Environment['QT_QUICK_BACKEND'] = 'software'
    if ($Arguments -contains '--chromium-runtime-test') { $info.Environment.Remove('QT_QUICK_BACKEND') | Out-Null }
    $info.Environment['QML_DISABLE_DISK_CACHE'] = '1'
    $info.Environment['__COMPAT_LAYER'] = 'RUNASINVOKER'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Could not start the portable test' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(120000)) {
            $process.Kill($true)
            throw 'Portable smoke test timed out'
        }
        [pscustomobject]@{ Code = $process.ExitCode; Out = $stdout.GetAwaiter().GetResult(); Err = $stderr.GetAwaiter().GetResult() }
    } finally { $process.Dispose() }
}
function Check-Probe($Result, [bool]$Wrapped) {
    if ($Result.Code -ne 0 -or $Result.Out -notmatch 'DEPLOYMENT TEST PASSED') {
        throw "Portable launch failed ($($Result.Code))`n$($Result.Out)`n$($Result.Err)"
    }
    if ($Wrapped -and $Result.Out -notmatch 'DEPLOYMENT_PORTABLE=1') { throw 'The portable launcher was not exercised' }
}
try {
    # Test the actual copy helpers without running the packaging script again.
    # Neither README.md nor docs/PORTABLE.md may be a build prerequisite, while
    # absent runtime/license inputs and genuine copy failures must still fail.
    $tokens = $null
    $parseErrors = $null
    $packager = [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot 'Build-SingleExe.ps1'), [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count) { throw "Packaging script syntax errors: $parseErrors" }
    foreach ($name in @('Copy-Required', 'Copy-OptionalDocument')) {
        $definition = $packager.Find({ param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
        }, $true)
        if (-not $definition) { throw "Missing packaging helper: $name" }
        . ([scriptblock]::Create($definition.Extent.Text))
    }
    $copyTests = Join-Path $root 'documentation copy tests'
    New-Item -ItemType Directory -Path $copyTests | Out-Null
    $missing = Join-Path $copyTests 'missing guide.md'
    $target = Join-Path $copyTests 'staged/docs/PORTABLE.md'
    Copy-OptionalDocument $missing $target
    if (Test-Path -LiteralPath $target) { throw 'A missing optional guide must not be copied' }
    $guide = Join-Path $copyTests 'guide.md'
    'optional guide contents' | Set-Content -LiteralPath $guide -Encoding utf8
    Copy-OptionalDocument $guide $target
    if ((Get-FileHash -LiteralPath $guide).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
        throw 'An available optional guide must be copied unchanged'
    }
    $refused = $false
    try { Copy-Required $missing (Join-Path $copyTests 'LICENSE') } catch { $refused = $true }
    if (-not $refused) { throw 'A missing required runtime/license file must fail' }
    $blocked = Join-Path $copyTests 'not-a-directory'
    'block directory creation' | Set-Content -LiteralPath $blocked
    $refused = $false
    try { Copy-OptionalDocument $guide (Join-Path $blocked 'guide.md') } catch { $refused = $true }
    if (-not $refused) { throw 'Optional documentation must not hide real copy errors' }
    Write-Host 'PASS optional guides present/missing, required files enforced, copy failures propagated'

    $arguments = @('--deployment-test', 'argument with spaces', 'quote"inside', 'trailing\', '')
    foreach ($platform in @('offscreen', 'windows')) {
        $result = Run-Clean $portable $arguments $platform
        Check-Probe $result $true
        if ($result.Out -notmatch '(?m)^DEPLOYMENT_LAUNCHER=(.+)$' -or $Matches[1].Trim() -ine $portable) { throw 'Portable launcher path did not override the inherited marker' }
        if ($result.Out -notmatch '(?m)^DEPLOYMENT_ARGS=(.+)$') { throw 'Missing argument-forwarding proof' }
        $actual = @($Matches[1] | ConvertFrom-Json)
        if ($actual.Count -ne $arguments.Count + 1) { throw 'Argument count changed in the launcher' }
        for ($i = 0; $i -lt $arguments.Count; ++$i) {
            if ($actual[$i + 1] -cne $arguments[$i]) { throw "Argument $i was not preserved" }
        }
        if ($result.Out -notmatch '(?m)^DEPLOYMENT_ROOT=(.+)$') { throw 'Missing private runtime path' }
        $runtime = $Matches[1].Trim()
        if (Test-Path -LiteralPath $runtime) { throw "Runtime directory was not cleaned up: $runtime" }
        Write-Host "PASS single EXE / $platform platform / clean PATH / argument forwarding / cleanup"
    }
    if (@(Get-ChildItem -LiteralPath $clean -Force).Count -ne 1) { throw 'The launcher left loose runtime files next to the EXE' }

    # Only empty fixture bytes: discovery must not execute the candidate ADB.
    $adbDirectory = Join-Path $clean 'adb'
    New-Item -ItemType Directory -Path $adbDirectory | Out-Null
    $fakeAdb = Join-Path $adbDirectory 'adb.exe'
    [IO.File]::WriteAllBytes($fakeAdb,[byte[]]@(0))
    try {
        $result = Run-Clean $portable @('--deployment-test')
        Check-Probe $result $true
        if ($result.Out -notmatch '(?m)^DEPLOYMENT_ADB=(.+)$' -or $Matches[1].Trim() -ine $fakeAdb) { throw 'ADB beside the OUTER portable launcher was not discovered' }
        Write-Host 'PASS app-local ADB discovery outside the private extraction directory (no ADB execution)'
    } finally { Remove-Item -LiteralPath $adbDirectory -Recurse -Force }

    $extracted = Join-Path $clean 'explicit extraction'
    $result = Run-Clean $portable @('--extract-to', $extracted)
    if ($result.Code -ne 0) { throw "Explicit extraction failed: $($result.Err)" }
    foreach ($file in @('BSI-Customizer.exe', 'Qt6Core.dll', 'qt.conf', 'licenses/Qt/QT-NOTICES.txt', 'licenses/HDE64.txt', 'licenses/7zip/LICENSE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $extracted $file))) { throw "Explicit extraction missing $file" }
    }
    $inner = Join-Path $extracted 'BSI-Customizer.exe'
    Check-Probe (Run-Clean $inner @('--deployment-test')) $false
    if (Test-Path -LiteralPath (Join-Path $extracted 'Qt6WebEngineCore.dll')) {
        # Execute from the real packed launcher with no SDK on PATH. Merely
        # checking a file list would miss helpers/locales and QML imports.
        $web = Run-Clean $portable @('--chromium-runtime-test') 'windows'
        if ($web.Code -ne 0 -or $web.Out -notmatch 'CHROMIUM RUNTIME TEST PASSED') {
            throw "Packed animated UI failed: $($web.Out) $($web.Err)"
        }
        Write-Host 'PASS packed Chromium helper/resources/locales / clean PATH'
    }

    $sentinel = Join-Path $extracted 'do-not-overwrite.txt'
    'preserve this file' | Set-Content -LiteralPath $sentinel
    $result = Run-Clean $portable @('--extract-to', $extracted)
    if ($result.Code -eq 0 -or (Get-Content -LiteralPath $sentinel -Raw).Trim() -ne 'preserve this file') {
        throw 'Extraction must refuse existing folders without overwriting their contents'
    }
    Write-Host 'PASS inspectable/relinkable extraction mode and overwrite protection'

    # Negative control: a missing dependency must not be rescued by the Qt SDK
    # installed on the runner. Test only our disposable extracted copy.
    $quick = Join-Path $extracted 'Qt6Quick.dll'
    Move-Item -LiteralPath $quick -Destination ($quick + '.missing')
    $missing = Run-Clean $inner @('--deployment-test')
    if ($missing.Code -eq 0 -or $missing.Out -match 'DEPLOYMENT TEST PASSED') {
        throw 'The isolated test unexpectedly found Qt outside the bundle'
    }
    Write-Host 'PASS missing-DLL negative control (no SDK fallback)'
    Write-Host 'ALL PORTABLE TESTS PASSED'
} finally {
    [BSIPortableTestErrorMode]::SetErrorMode($oldErrorMode) | Out-Null
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
