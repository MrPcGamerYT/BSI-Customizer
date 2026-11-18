$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $root '.cache/optimizer-repair'
New-Item -ItemType Directory -Force $out | Out-Null
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path $csc)) { throw 'Windows x64 .NET Framework compiler not found.' }
$sources = @(Get-ChildItem (Join-Path $PSScriptRoot 'Source/*.cs') | ForEach-Object { $_.FullName })
$manifest = Join-Path $PSScriptRoot 'helper.manifest'
$exe = Join-Path $out 'VOID-Optimizer.exe'
& $csc /nologo /target:winexe /platform:x64 /codepage:65001 /optimize+ /warn:4 /warnaserror+ '/reference:System.Windows.Forms.dll,System.Drawing.dll,System.Xml.dll,System.Core.dll' "/out:$exe" "/win32manifest:$manifest" @sources
if ($LASTEXITCODE -ne 0) { throw "Compiler failed: $LASTEXITCODE" }
foreach ($bits in @('x86','x64')) {
    $fixture = Join-Path $out ('fixtures/'+$bits+'/HD-Player.exe')
    New-Item -ItemType Directory -Force (Split-Path $fixture -Parent) | Out-Null
    & $csc /nologo /target:exe "/platform:$bits" "/out:$fixture" (Join-Path $PSScriptRoot 'TestFixtures/ProbeChild.cs')
    if ($LASTEXITCODE -ne 0) { throw 'Discovery fixture compile failed.' }
}
foreach ($mode in @('--self-test', '--dropdown-self-test', '--discovery-self-test', '--ui-smoke-test')) {
    $stdout = Join-Path $out ($mode.TrimStart('-') + '.stdout.txt')
    $stderr = Join-Path $out ($mode.TrimStart('-') + '.stderr.txt')
    $test = Start-Process -FilePath $exe -ArgumentList $mode -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Get-Content $stdout -ErrorAction SilentlyContinue | Write-Host
    Get-Content $stderr -ErrorAction SilentlyContinue | Write-Host
    if ($test.ExitCode -ne 0) {
        foreach ($file in @($stdout,$stderr,(Join-Path $out 'self-test-error.txt'))) {
            if (Test-Path $file) {
                foreach ($line in Get-Content $file) { Write-Host ('::error::' + ($line -replace '%','%25')) }
            }
        }
        throw "Actual helper failed $mode ($($test.ExitCode)). No protocol was registered."
    }
}
$env:VOID_OPTIMIZER_TESTED_EXE = $exe
Set-Location $root
python tools/optimizer-repair/package.py
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
