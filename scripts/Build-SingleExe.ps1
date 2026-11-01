[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AppExecutable,
    [Parameter(Mandatory)][string]$QtPrefix,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
trap {
    $message = ($_.Exception.Message + "`n" + $_.ScriptStackTrace).Replace('%','%25').Replace("`r",'%0D').Replace("`n",'%0A')
    Write-Host "::error::$message"
    throw
}
$repo = Split-Path $PSScriptRoot -Parent
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Version must be numeric, e.g. 2.0.0' }
$AppExecutable = (Resolve-Path -LiteralPath $AppExecutable).Path
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $BuildRoot, $OutputDirectory | Out-Null
# This script owns only this staging subdirectory, never a repository/install root.
$stage = Join-Path $BuildRoot 'stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $AppExecutable -Destination (Join-Path $stage 'BSI-Customizer.exe')

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
function Copy-Required([string]$From, [string]$To) {
    if (-not (Test-Path -LiteralPath $From -PathType Leaf)) { throw "Missing required package file: $From" }
    New-Item -ItemType Directory -Force -Path (Split-Path $To -Parent) | Out-Null
    Copy-Item -LiteralPath $From -Destination $To
}
function Copy-OptionalDocument([string]$From, [string]$To) {
    if (Test-Path -LiteralPath $From -PathType Leaf) {
        # Still fail on a real copy/access error; only absence is optional.
        Copy-Required $From $To
    } else {
        Write-Host "Skipping optional documentation: $From"
    }
}

$qtBin = Join-Path $QtPrefix 'bin'
$deploy = Join-Path $qtBin 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy)) { throw "windeployqt not found in $qtBin" }
# Do not deploy/run vc_redist.exe. The CRT is copied app-locally below.
$deployLog = Join-Path $BuildRoot 'windeployqt.log'
& $deploy --release --no-translations --no-compiler-runtime --qmldir (Join-Path $repo 'src/qml') `
    --dir $stage (Join-Path $stage 'BSI-Customizer.exe') *> $deployLog
$deployExit = $LASTEXITCODE
if ($deployExit -ne 0) {
    # Qt can warn about BSICustomizer, which is compiled into the executable.
    # Never use this warning as a substitute for dependency/launch checks.
    Get-Content -LiteralPath $deployLog -Tail 30 | Write-Host
    Write-Warning "windeployqt exited $deployExit; validating the staged dependency closure and real launch below"
}

# Chromium-capable builds need their QML imports as well as the dependent
# DLLs discovered by the first deployment pass. Keep native-only CI unchanged.
if (Test-Path -LiteralPath (Join-Path $stage 'Qt6WebEngineCore.dll')) {
    Invoke-Checked $deploy @('--release', '--no-compiler-runtime', '--qmldir',
        (Join-Path $repo 'src/chromium'), '--dir', $stage,
        (Join-Path $stage 'BSI-Customizer.exe'))
    # Official Windows Qt embeds ICU data in WebEngineCore; Linux/custom
    # SDKs may ship icudtl.dat separately. Do not reject a valid embedded build.
    if ((Test-Path -LiteralPath (Join-Path $QtPrefix 'resources/icudtl.dat')) -and
        -not (Test-Path -LiteralPath (Join-Path $stage 'resources/icudtl.dat'))) {
        throw 'Missing external ICU data required by this Qt SDK'
    }
    foreach ($relative in @('QtWebEngineProcess.exe', 'qml/QtWebEngine/qmldir',
                            'qml/QtWebChannel/qmldir', 'resources/qtwebengine_resources.pak',
                            'translations/qtwebengine_locales/en-US.pak')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $relative))) {
            throw "Incomplete animated UI runtime: $relative"
        }
    }
}

Copy-Required (Join-Path $qtBin 'Qt6Svg.dll') (Join-Path $stage 'Qt6Svg.dll')
Copy-Required (Join-Path $QtPrefix 'plugins/imageformats/qsvg.dll') (Join-Path $stage 'imageformats/qsvg.dll')
Copy-Required (Join-Path $QtPrefix 'plugins/platforms/qwindows.dll') (Join-Path $stage 'platforms/qwindows.dll')
Copy-Required (Join-Path $QtPrefix 'plugins/platforms/qoffscreen.dll') (Join-Path $stage 'platforms/qoffscreen.dll')
@'
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
Translations=translations
'@ | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding ascii

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe not found; install the Visual Studio C++ build tools' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'An x64 MSVC toolchain is required' }
$vs = ($vs | Select-Object -First 1).Trim()
$redistVersion = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory |
    Where-Object Name -match '^\d+\.\d+\.\d+' | Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (-not $redistVersion) { throw 'MSVC redistributable directory not found' }
$crt = Get-ChildItem -LiteralPath (Join-Path $redistVersion.FullName 'x64') -Directory |
    Where-Object Name -match '^Microsoft\.VC\d+\.CRT$' | Select-Object -First 1
if (-not $crt) { throw 'App-local x64 MSVC CRT files were not found' }
Get-ChildItem -LiteralPath $crt.FullName -File | Where-Object Extension -in '.dll', '.manifest' |
    Copy-Item -Destination $stage

$tools = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory |
    Where-Object Name -match '^\d+\.\d+\.\d+' | Sort-Object { [version]$_.Name } | Select-Object -Last 1
$dumpbin = Join-Path $tools.FullName 'bin/Hostx64/x64/dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin)) { throw 'dumpbin.exe not found' }
function Get-Imports([string]$File) {
    $text = & $dumpbin /nologo /dependents $File
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect PE imports: $File" }
    @($text | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') { $Matches[1] } } | Sort-Object -Unique)
}
# Validate actual imports, not just a few filenames. Qt/CRT dependencies must
# be in the bundle even when the hosted runner already has them installed.
foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object Extension -in '.dll', '.exe')) {
    foreach ($dll in (Get-Imports $file.FullName)) {
        $local = (Test-Path -LiteralPath (Join-Path $stage $dll)) -or
                 (Test-Path -LiteralPath (Join-Path $file.DirectoryName $dll))
        $appDependency = $dll -match '^(Qt\d|msvcp\d|msvcr\d|vcruntime\d|concrt\d)'
        $system = $dll -match '^(api-ms-|ext-ms-)' -or (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $dll))
        if (-not $local -and ($appDependency -or -not $system)) {
            throw "Unbundled dependency: $($file.Name) requires $dll"
        }
    }
}
foreach ($relative in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6QuickTemplates2.dll',
                        'Qt6Svg.dll', 'vcruntime140.dll', 'msvcp140.dll', 'platforms/qwindows.dll',
                        'platforms/qoffscreen.dll', 'imageformats/qsvg.dll', 'qml/QtQuick/qmldir', 'qt.conf')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $relative))) { throw "Incomplete runtime: $relative" }
}
if (Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object Name -match '^(vc_redist|vcredist|setup).*\.exe$') {
    throw 'An installer must not be included in the portable payload'
}

# Guides are useful, but are not needed to start the bundled application.
Copy-OptionalDocument (Join-Path $repo 'README.md') (Join-Path $stage 'README.md')
Copy-OptionalDocument (Join-Path $repo 'docs/PORTABLE.md') (Join-Path $stage 'docs/PORTABLE.md')
# License notices remain mandatory, independently of the optional guides.
Copy-Required (Join-Path $repo 'LICENSE') (Join-Path $stage 'LICENSE')
Copy-Required (Join-Path $repo 'src/third_party/hde64/LICENSE.txt') (Join-Path $stage 'licenses/HDE64.txt')
Copy-Required (Join-Path $repo 'src/third_party/7zip/LICENSE.txt') (Join-Path $stage 'licenses/7zip/LICENSE.txt')
$qtVersion = (& (Join-Path $qtBin 'qmake.exe') -query QT_VERSION).Trim()
if ($LASTEXITCODE -ne 0 -or $qtVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Cannot determine the deployed Qt version' }
# Licence/attribution collection is a CMake script, so packaging needs nothing
# beyond the toolchain that already builds the application - no interpreter.
Invoke-Checked 'cmake' @("-DQT_VERSION=$qtVersion",
                         "-DBSI_WITH_WEBENGINE=$(Test-Path -LiteralPath (Join-Path $stage 'Qt6WebEngineCore.dll'))",
                         "-DOUTPUT_DIR=$(Join-Path $stage 'licenses/Qt')",
                         "-DCACHE_DIR=$(Join-Path $BuildRoot 'notices-cache')",
                         '-P', (Join-Path $repo 'cmake/CollectQtNotices.cmake'))
if (Test-Path -LiteralPath (Join-Path $stage 'Qt6WebEngineCore.dll')) {
    & (Join-Path $PSScriptRoot 'Collect-WebEngineNotices.ps1') -Version $qtVersion `
        -CacheDirectory (Join-Path $BuildRoot 'notices-cache/chromium') -OutputDirectory (Join-Path $stage 'licenses/Qt/Chromium')
}
@"
BSI Customizer portable runtime

Qt $qtVersion is dynamically linked. Full Qt licensing and third-party notices
are in licenses/Qt. HDE64's BSD notices are in licenses/HDE64.txt. The
bundled 7-Zip decoder's public-domain notice is in licenses/7zip/LICENSE.txt.
Microsoft Visual C++ app-local redistributable DLLs are copyright Microsoft.

To inspect this runtime or substitute ABI-compatible Qt libraries:
  <portable-exe> --extract-to <new-directory>
Then replace the desired Qt DLLs and run BSI-Customizer.exe from that directory.
No installer, registry installation entry, shortcut, or runtime setup is used.
The normal application still stores its preferences and user-requested backups.

App source: https://github.com/MrPcGamerYT/BSI-Customizer
Qt source: https://download.qt.io/archive/qt/$($qtVersion -replace '\.\d+$','')/$qtVersion/single/
"@ | Set-Content -LiteralPath (Join-Path $stage 'PORTABLE-README.txt') -Encoding utf8

$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
if ($files.Count -gt 10000 -or ($files | Measure-Object Length -Sum).Sum -gt 1GB) { throw 'Portable runtime is too large' }
$lines = [Collections.Generic.List[string]]::new()
$cabDirectory = Join-Path $BuildRoot 'cab'
if (Test-Path -LiteralPath $cabDirectory) { Remove-Item -LiteralPath $cabDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $cabDirectory | Out-Null
$lines.Add('.OPTION EXPLICIT')
$lines.Add('.Set CabinetNameTemplate=payload.cab')
$lines.Add(".Set DiskDirectoryTemplate=`"$cabDirectory`"")
$lines.Add('.Set Cabinet=on')
$lines.Add('.Set Compress=on')
$lines.Add('.Set CompressionType=LZX')
$lines.Add('.Set CompressionMemory=21')
$lines.Add('.Set MaxDiskSize=0')
$lines.Add('.Set MaxCabinetSize=0')
$lines.Add('.Set FolderSizeThreshold=0')
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length).TrimStart([char]'\', [char]'/').Replace('/', '\')
    if ($relative.Length -gt 220 -or $relative -match '[^\x20-\x7e]' -or $relative.Contains('"') -or -not $seen.Add($relative)) {
        throw "Unsupported/ambiguous cabinet entry: $relative"
    }
    $lines.Add("`"$relative`" `"$relative`"")
}
# makecab reads DDF paths in the system code page; fail rather than corrupt a
# non-ASCII build path. The finished EXE supports Unicode run/extraction paths.
if ($cabDirectory -match '[^\x20-\x7e]') { throw 'Use an ASCII build directory for makecab' }
$ddf = Join-Path $BuildRoot 'payload.ddf'
$lines | Set-Content -LiteralPath $ddf -Encoding ascii
Push-Location $stage
try {
    $cabLog = Join-Path $BuildRoot 'makecab.log'
    & (Join-Path $env:SystemRoot 'System32/makecab.exe') /F $ddf *> $cabLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $cabLog -Tail 40 | Write-Host
        throw "makecab failed with exit code $LASTEXITCODE"
    }
    Get-Content -LiteralPath $cabLog -Tail 8 | Write-Host
} finally { Pop-Location }
$cabinets = @(Get-ChildItem -LiteralPath $cabDirectory -Filter '*.cab' -File)
if ($cabinets.Count -ne 1 -or $cabinets[0].Name -ne 'payload.cab') { throw 'Packaging must produce one complete cabinet' }

$launcherBuild = Join-Path $BuildRoot 'launcher'
Invoke-Checked 'cmake' @('-S', (Join-Path $repo 'packaging/portable'), '-B', $launcherBuild, '-A', 'x64',
                        "-DBSI_PAYLOAD_CAB=$($cabinets[0].FullName)", "-DBSI_APP_VERSION=$Version")
Invoke-Checked 'cmake' @('--build', $launcherBuild, '--config', 'Release', '--parallel')
$launcher = Join-Path $launcherBuild 'Release/BSI-Customizer-Portable.exe'
foreach ($dll in (Get-Imports $launcher)) {
    if ($dll -match '^(Qt\d|msvcp\d|msvcr\d|vcruntime\d|concrt\d)' -or
        ($dll -notmatch '^(api-ms-|ext-ms-)' -and -not (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $dll)))) {
        throw "The bootstrap EXE itself is not standalone: $dll"
    }
}
$destination = Join-Path $OutputDirectory "BSI-Customizer-$Version-win64.exe"
Copy-Item -LiteralPath $launcher -Destination $destination -Force
& (Join-Path $PSScriptRoot 'Test-Manifest.ps1') -ExecutablePath $destination
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash.ToLowerInvariant()
# The CAB/EXE now owns the payload. Remove the intermediate deployment tree;
# older build-output discovery must not mistake its app for the SDK test build.
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Host "ONE-FILE EXE: $destination"
Write-Host "SHA256: $sha"
Write-Host "Payload files: $($files.Count). The only distributable is the EXE above."
if ($env:GITHUB_OUTPUT) {
    "exe=$destination" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "sha256=$sha" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
