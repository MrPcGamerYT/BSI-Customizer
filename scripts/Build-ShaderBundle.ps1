[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Qt5Prefix,
    [Parameter(Mandatory)][string]$Qt6Prefix,
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Toolset = 'v143'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path $PSScriptRoot -Parent
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count) {
    throw 'Use a new/empty shader bundle output directory; existing files are never deleted.'
}
New-Item -ItemType Directory -Force -Path $BuildRoot, $OutputDirectory | Out-Null
$oldPath = $env:PATH
try {
    # These are BUILD baselines, not an exhaustive list of player runtimes.
    # Newer same-major players still undergo full imported-symbol/provider checks.
    foreach ($entry in @(@{Major = 5; Prefix = $Qt5Prefix; Baseline = '5.15.2'}, @{Major = 6; Prefix = $Qt6Prefix; Baseline = '6.2.4'})) {
        $prefix = (Resolve-Path -LiteralPath $entry.Prefix).Path
        $qmake = Join-Path $prefix 'bin/qmake.exe'
        $version = (& $qmake -query QT_VERSION).Trim()
        if ($LASTEXITCODE -ne 0 -or $version -notmatch "^$($entry.Major)\.\d{1,3}\.\d{1,3}$") {
            throw "Invalid Qt $($entry.Major) SDK: $prefix"
        }
        if ($version -ne $entry.Baseline) {
            throw "Release coverage requires the actual Qt $($entry.Baseline) build baseline, not $version. Never relabel a DLL or replace a player's runtime."
        }
        $build = Join-Path $BuildRoot "qt-$version"
        $sdkVersion = [Version]$version
        $packedVersion = ($sdkVersion.Major -shl 16) -bor ($sdkVersion.Minor -shl 8) -bor $sdkVersion.Build
        # The app's SDK is also installed. Isolate BOTH target components and
        # host moc/rcc tools: Qt6_DIR alone still allowed 6.8 CoreTools into 6.2.
        $env:PATH = (Join-Path $prefix 'bin') + ';' + $oldPath
        $sdkArguments = @("-DCMAKE_PREFIX_PATH=$prefix", "-DBSI_SHADERS_EXPECT_QT_VERSION=$packedVersion")
        if ($entry.Major -eq 6) {
            $sdkArguments += "-DQT_HOST_PATH=$prefix"
            $sdkArguments += "-DQt6CoreTools_DIR=$prefix/lib/cmake/Qt6CoreTools"
        }
        # Do not inherit a moving VS 2026 generator: its STL removed stdext
        # iterators used by the genuine older Qt SDK headers. No SDK edits.
        cmake -S (Join-Path $repo 'src/shaders') -B $build -G $Generator -A x64 -T $Toolset @sdkArguments `
            "-DBSI_SHADERS_QT_MAJOR=$($entry.Major)" "-DQt$($entry.Major)_DIR=$prefix/lib/cmake/Qt$($entry.Major)" `
            '-DBSI_SHADERS_BUILD_TESTS=ON' '-DBSI_TEST_NATIVE_SETTINGS=ON' '-DBSI_TEST_VULKAN=OFF'
        if ($LASTEXITCODE -ne 0) { throw "Cannot configure Qt $version shader module" }
        cmake --build $build --config Release --parallel 2
        if ($LASTEXITCODE -ne 0) { throw "Cannot build Qt $version shader module" }
        # Only this SDK is first on PATH, not another Qt major. Package checking
        # reads actual imports/exports/fixed versions; no arbitrary DLL is loaded.
        $env:PATH = (Join-Path $prefix 'bin') + ';' + $oldPath
        ctest --test-dir $build -C Release --output-on-failure --timeout 60
        if ($LASTEXITCODE -ne 0) { throw "Qt $version shader/package/recovery/DirectX tests failed" }
        $out = Join-Path $OutputDirectory "qt-$version"
        New-Item -ItemType Directory -Path $out | Out-Null
        foreach ($name in @('BSIShaders.dll', 'qmldir')) {
            Copy-Item -LiteralPath (Join-Path $build "module/BSIShaders/$name") -Destination (Join-Path $out $name)
        }
        Write-Host "Verified regular shader payload: Qt $version build baseline / public-v1 / x64 / release / MSVC"
    }
} finally { $env:PATH = $oldPath }
