[CmdletBinding()]
param(
    [string]$CustomizerPath,
    [string]$PlayerPath,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'This manual launcher requires Windows.'
}
Add-Type -AssemblyName System.Windows.Forms

function Select-Executable([string]$Title, [string]$Filter) {
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = $Title
    $dialog.Filter = $Filter
    $dialog.CheckFileExists = $true
    $dialog.Multiselect = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        throw "No file selected for: $Title"
    }
    return $dialog.FileName
}

if (-not $CustomizerPath) {
    $root = Split-Path -Parent $PSScriptRoot
    $candidates = @(
        (Join-Path $root 'build/Release/BSI-Customizer.exe'),
        (Join-Path $root 'build/BSI-Customizer.exe'),
        (Join-Path $root 'BSI-Customizer.exe')
    )
    $CustomizerPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $CustomizerPath) {
        $CustomizerPath = Select-Executable 'Select BSI-Customizer.exe' 'BSI-Customizer.exe|BSI-Customizer.exe|Programs (*.exe)|*.exe'
    }
}
if (-not (Test-Path -LiteralPath $CustomizerPath -PathType Leaf)) {
    throw "Customizer executable not found: $CustomizerPath"
}
$CustomizerPath = (Resolve-Path -LiteralPath $CustomizerPath).Path

if (-not $PlayerPath) {
    $PlayerPath = Select-Executable 'Select the HD-Player.exe you actually launch' 'HD-Player.exe|HD-Player.exe|Programs (*.exe)|*.exe'
}
if (-not (Test-Path -LiteralPath $PlayerPath -PathType Leaf)) {
    throw "HD-Player executable not found: $PlayerPath"
}
$PlayerPath = (Resolve-Path -LiteralPath $PlayerPath).Path

# The CLI reads the selected player, then tests Backend::applySettingsPanelRebuild
# on a disposable temporary copy. It hashes the selected file before/after, so
# this manual check never patches the user's actual HD-Player.exe.
$logRoot = Join-Path $env:TEMP ('bsi-settings-panel-manual-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $logRoot | Out-Null
$stdoutPath = Join-Path $logRoot 'report.json'
$stderrPath = Join-Path $logRoot 'diagnostic.log'
try {
    $process = Start-Process -FilePath $CustomizerPath `
        -ArgumentList @('--settings-panel-test', ('"{0}"' -f $PlayerPath)) `
        -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    if (Test-Path -LiteralPath $stderrPath) {
        $diagnostic = Get-Content -LiteralPath $stderrPath -Raw
        if (-not [string]::IsNullOrWhiteSpace($diagnostic)) { Write-Host $diagnostic.Trim() }
    }
    if (-not (Test-Path -LiteralPath $stdoutPath)) {
        throw "The tester returned no report (exit code $($process.ExitCode))."
    }
    $jsonText = Get-Content -LiteralPath $stdoutPath -Raw
    Write-Host $jsonText
    try { $report = $jsonText | ConvertFrom-Json }
    catch { throw "Could not parse the tester's JSON report: $($_.Exception.Message)" }

    $testPassed = $process.ExitCode -eq 0 -and $report.ok -and $report.supported `
        -and $report.temporaryCopyApplyOk -and $report.temporaryCopyAppliedAfter `
        -and $report.originalUnchanged
    $diagnostic = "Supported=$($report.supported); QML=$($report.embeddedQmlResourceCount); " +
        "temporary apply=$($report.temporaryCopyApplyOk); applied after=$($report.temporaryCopyAppliedAfter); " +
        "original unchanged=$($report.originalUnchanged). $($report.detail) $($report.temporaryCopyDetail)"
    if ($env:GITHUB_STEP_SUMMARY) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "## Settings-panel tester`n`n$diagnostic`n"
    }
    if (-not $testPassed -and $env:GITHUB_ACTIONS -eq 'true') {
        $annotation = $diagnostic -replace '%', '%25' -replace "`r", ' ' -replace "`n", ' '
        if ($annotation.Length -gt 400) { $annotation = $annotation.Substring(0, 400) }
        Write-Host "::error::Settings-panel safe-copy test failed: $annotation"
    }
    if ($testPassed) {
        Write-Host 'PASS: the selected player is reported Supported, and the Settings-panel apply succeeded on a temporary copy.' -ForegroundColor Green
    } else {
        Write-Warning 'The safe-copy test did not pass. The selected HD-Player.exe was not modified; the report above describes the unsupported step.'
    }

    if ($NoLaunch) {
        Write-Host 'GUI launch skipped (-NoLaunch).'
    } else {
        # The GUI gets its own disposable executable too. Even if Apply is
        # clicked while inspecting the real interface, only this copy changes.
        $guiSandbox = Join-Path $logRoot 'gui-sandbox'
        New-Item -ItemType Directory -Path $guiSandbox | Out-Null
        $guiPlayer = Join-Path $guiSandbox 'HD-Player.exe'
        Copy-Item -LiteralPath $PlayerPath -Destination $guiPlayer
        Write-Host 'Opening BSI Customizer directly on Interface → Settings panel in isolated manual-test mode.'
        Write-Host 'The GUI targets a disposable HD-Player copy; its Apply/Restore actions cannot change the selected original.'
        $gui = Start-Process -FilePath $CustomizerPath `
            -ArgumentList @('--settings-panel-manual-test', ('"{0}"' -f $guiPlayer)) `
            -Wait -PassThru
        if ($gui.ExitCode -ne 0) {
            throw "The manual-test GUI exited with code $($gui.ExitCode)."
        }
    }
    if (-not $testPassed) {
        throw 'Settings-panel manual test failed. Inspect the Customizer UI and the JSON report above.'
    }
}
catch {
    if ($env:GITHUB_ACTIONS -eq 'true') {
        $message = $_.Exception.Message -replace '%', '%25' -replace "`r", ' ' -replace "`n", ' '
        if ($message.Length -gt 400) { $message = $message.Substring(0, 400) }
        Write-Host "::error::Manual Settings-panel tester exception: $message"
    }
    throw
}
finally {
    Remove-Item -LiteralPath $logRoot -Recurse -Force -ErrorAction SilentlyContinue
}
