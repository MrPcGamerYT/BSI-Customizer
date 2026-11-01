[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$QtPrefix,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Use the matching SDK's generated notices, including Chromium's vendored
# third parties. QtWebEngine's Git archive omits that enormous submodule;
# collecting only its top-level BSD/LGPL files would be incomplete.
$qtRoot = Split-Path (Split-Path $QtPrefix -Parent) -Parent
$docs = Join-Path $qtRoot "Docs/Qt-$Version/qtwebengine"
$notices = @(Get-ChildItem -LiteralPath $docs -File -Filter 'qtwebengine-3rdparty-*.html')
if ($notices.Count -lt 100) { throw "Missing/incomplete Qt $Version Chromium notices. Install matching qtwebengine documentation: $docs" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
foreach ($notice in $notices) { Copy-Item -LiteralPath $notice.FullName -Destination $OutputDirectory -Force }
Copy-Item -LiteralPath (Join-Path $docs 'qtwebengine-licensing.html') -Destination $OutputDirectory -Force
if (Test-Path -LiteralPath (Join-Path $docs 'style')) {
    Copy-Item -LiteralPath (Join-Path $docs 'style') -Destination $OutputDirectory -Recurse -Force
}
Write-Host "Bundled $($notices.Count) matching Qt WebEngine/Chromium third-party notice pages."
