[CmdletBinding()]
param([Parameter(Mandatory)][string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
$ExecutablePath = (Resolve-Path -LiteralPath $ExecutablePath).Path
$mt = Get-ChildItem -Path (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin') -Recurse -Filter mt.exe -File |
    Where-Object FullName -match '\\x64\\mt\.exe$' | Sort-Object FullName | Select-Object -Last 1
if (-not $mt) { throw 'Windows SDK mt.exe is required to verify the administrator manifest' }
$manifest = Join-Path ([IO.Path]::GetTempPath()) (([guid]::NewGuid().ToString('N')) + '.manifest')
try {
    & $mt.FullName -nologo "-inputresource:$ExecutablePath;#1" "-out:$manifest"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $manifest)) { throw 'Missing RT_MANIFEST resource #1' }
    [xml]$xml = Get-Content -LiteralPath $manifest -Raw
    $levels = @($xml.SelectNodes('//*[local-name()="requestedExecutionLevel"]'))
    if ($levels.Count -ne 1 -or $levels[0].level -ne 'requireAdministrator' -or $levels[0].uiAccess -ne 'false') {
        throw 'The EXE must have one requireAdministrator manifest with uiAccess=false'
    }
    Write-Host "PASS administrator manifest: $ExecutablePath"
} finally { Remove-Item -LiteralPath $manifest -Force -ErrorAction SilentlyContinue }
