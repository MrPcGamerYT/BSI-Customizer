[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtPrefix,
    [Parameter(Mandatory)][string]$Version
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$qtRoot = Split-Path (Split-Path $QtPrefix -Parent) -Parent
$docs = Join-Path $qtRoot "Docs/Qt-$Version/qtwebengine"
if (Test-Path -LiteralPath (Join-Path $docs 'qtwebengine-licensing.html')) { return }
# Keep SDK cache restoration independent from documentation installation.
# Do not assume Qt's binary and documentation archives share archive names.
python -m pip install --disable-pip-version-check --no-input 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw 'Could not install the Qt documentation downloader' }
$log = Join-Path $env:RUNNER_TEMP 'webengine-docs.log'
python -m aqt install-doc windows $Version --outputdir $qtRoot --modules qtwebengine 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $log -Tail 20 | ForEach-Object {
        $message = $_.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error::$message"
    }
    throw 'Matching Qt WebEngine notice documentation could not be installed'
}
