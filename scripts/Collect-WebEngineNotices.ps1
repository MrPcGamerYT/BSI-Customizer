[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$CacheDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid Qt version' }
# Qt's normal docs/source repository excludes WebEngine. Resolve the exact
# Chromium submodule from Qt's release tag, not a moving Chromium branch.
if ($Version -eq '6.8.3') { $commit = '55749ed0af5869215b88007df0cba430746583ae' }
else {
    $metadata = Invoke-RestMethod -UserAgent 'BSI-Customizer-notice-collector' `
        -Uri "https://api.github.com/repos/qt/qtwebengine/contents/src/3rdparty?ref=v$Version"
    $commit = $metadata.sha
}
if ($commit -notmatch '^[0-9a-f]{40}$') { throw 'Invalid Chromium source commit' }
New-Item -ItemType Directory -Force -Path $CacheDirectory, $OutputDirectory | Out-Null
$repository = Join-Path $CacheDirectory 'chromium.git'
if (-not (Test-Path -LiteralPath $repository)) {
    & git init --bare $repository
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize source notice cache' }
    & git "--git-dir=$repository" remote add origin https://github.com/qt/qtwebengine-chromium.git
    if ($LASTEXITCODE -ne 0) { throw 'Could not configure source notice cache' }
}
function Invoke-Git([string[]]$Arguments) {
    $result = @(& git "--git-dir=$repository" @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Notice-source git operation failed: $($Arguments[0])" }
    return $result
}
Invoke-Git @('config', 'remote.origin.promisor', 'true')
Invoke-Git @('config', 'remote.origin.partialclonefilter', 'blob:none')
Invoke-Git @('fetch', '--filter=blob:none', '--depth=1', 'origin', $commit)
$paths = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
$selected = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
foreach ($line in (Invoke-Git @('ls-tree', '-r', '--format=%(objectname) %(path)', 'FETCH_HEAD'))) {
    if ($line -notmatch '^([0-9a-f]{40}) (.+)$') { continue }
    $hash = $Matches[1]; $path = $Matches[2]; $paths[$path] = $hash
    $name = ($path -split '/')[-1]
    $license = $name -match '^(licen[cs]es?|copying|notices?|copyrights?|patents|authors)(?:[-._].*)?$'
    $source = $name -match '\.(cc|cpp|c|h|py|js|gn|gni|patch|png|gif|jpg|zip|pdf|json|html|xml|sh|bat|ps1|rs)$'
    if ($name -eq 'README.chromium' -or ($license -and -not $source) -or $path -match '/LICENSES/[^/]+\.txt$') { $selected[$path] = $hash }
}
if ($selected.Count -lt 500 -or -not $selected.ContainsKey('chromium/LICENSE')) { throw 'Incomplete Chromium source index' }
# Request only notice/readme blobs in one batch. No checkout, scripts, compiler,
# multi-gigabyte source archive or unrelated source payload is downloaded/run.
function Read-Blobs([string[]]$Hashes) {
    $Hashes = @($Hashes | Sort-Object -Unique)
    $Hashes | & git "--git-dir=$repository" -c fetch.negotiationAlgorithm=noop fetch --no-write-fetch-head --no-tags --filter=blob:none origin --stdin
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch source notice blobs' }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = 'git'; $info.UseShellExecute = $false
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in @("--git-dir=$repository", 'cat-file', '--batch')) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    $memory = [IO.MemoryStream]::new()
    try {
        if (-not $process.Start()) { throw 'Could not read notice blobs' }
        $copy = $process.StandardOutput.BaseStream.CopyToAsync($memory)
        $errors = $process.StandardError.ReadToEndAsync()
        $process.StandardInput.Write(($Hashes -join "`n") + "`n"); $process.StandardInput.Close()
        if (-not $process.WaitForExit(120000)) { $process.Kill($true); throw 'Source notice read timed out' }
        $copy.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Source notice read failed: $($errors.GetAwaiter().GetResult())" }
        $bytes = $memory.ToArray(); $offset = 0
        $texts = [Collections.Generic.Dictionary[string,string]]::new()
        foreach ($hash in $Hashes) {
            $start = $offset
            while ($offset -lt $bytes.Length -and $bytes[$offset] -ne 10) { ++$offset }
            $header = [Text.Encoding]::ASCII.GetString($bytes, $start, $offset - $start).Split(' '); ++$offset
            if ($header.Count -ne 3 -or $header[0] -ne $hash -or $header[1] -ne 'blob') { throw 'Invalid source notice object' }
            $size = [int]$header[2]
            if ($size -lt 0 -or $size -gt 4194304 -or $offset + $size -ge $bytes.Length) { throw 'Invalid/oversized source notice' }
            try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes, $offset, $size) }
            catch [Text.DecoderFallbackException] { $text = [Text.Encoding]::Latin1.GetString($bytes, $offset, $size) }
            if ($text.IndexOf([char]0) -ge 0) { throw 'Binary data in source notice' }
            $texts[$hash] = $text; $offset += $size + 1
        }
        return ,$texts
    } finally { $memory.Dispose(); $process.Dispose() }
}
$texts = Read-Blobs @($selected.Values)
# Include nonstandard license filenames referenced by vendored README metadata.
# Some readmes describe optional, absent SDK/build/test inputs. Preserve those
# readmes and report absent references, rather than claiming they are shipped.
$absent = [Collections.Generic.List[object]]::new()
foreach ($path in @($selected.Keys)) {
    if (-not $path.EndsWith('/README.chromium')) { continue }
    foreach ($match in [regex]::Matches($texts[$selected[$path]], '(?mi)^License File:[ \t]*(.+)$')) {
        foreach ($file in ($match.Groups[1].Value.Trim() -split ',[ \t]*')) {
            if ($file -eq 'NOT_SHIPPED') { continue }
            $relative = if ($file.StartsWith('/')) { 'chromium/' + $file.TrimStart('/') } else { ($path.Substring(0, $path.LastIndexOf('/') + 1)) + $file }
            $parts = [Collections.Generic.List[string]]::new()
            foreach ($part in ($relative -split '/')) {
                if ($part -eq '..') { if ($parts.Count) { $parts.RemoveAt($parts.Count - 1) } }
                elseif ($part -ne '.' -and $part) { $parts.Add($part) }
            }
            $resolved = $parts -join '/'
            if ($paths.ContainsKey($resolved)) { $selected[$resolved] = $paths[$resolved] }
            else { $absent.Add(@{readme=$path; reference=$file}) }
        }
    }
}
$extra = @($selected.Values | Where-Object { -not $texts.ContainsKey($_) } | Sort-Object -Unique)
if ($extra.Count) {
    $more = Read-Blobs $extra
    foreach ($hash in $more.Keys) { $texts[$hash] = $more[$hash] }
}
$output = Join-Path $OutputDirectory 'CHROMIUM-NOTICES.txt'
$writer = [IO.StreamWriter]::new($output, $false, [Text.UTF8Encoding]::new($false))
try {
    $writer.WriteLine("Qt WebEngine $Version / Chromium vendored-source notices")
    $writer.WriteLine("Source: https://github.com/qt/qtwebengine-chromium/tree/$commit")
    $writer.WriteLine('Conservative superset: includes build/test/platform notices; inclusion does not assert that a component is shipped.')
    foreach ($path in ($selected.Keys | Sort-Object)) {
        $writer.WriteLine("`n==============================================================================`n$path`nGit blob: $($selected[$path])`n")
        $writer.WriteLine($texts[$selected[$path]])
    }
} finally { $writer.Dispose() }
@{qtVersion=$Version; commit=$commit; source='https://github.com/qt/qtwebengine-chromium'; files=@($selected.Keys | Sort-Object | ForEach-Object { @{path=$_; object=$selected[$_]} }); nonVendoredReferences=$absent.ToArray()} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SOURCES.json') -Encoding utf8
Write-Host "Bundled $($selected.Count) Chromium notice/readme files from exact Qt $Version sources (without checking out source code)."
