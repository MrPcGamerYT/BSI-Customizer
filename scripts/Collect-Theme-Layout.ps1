#requires -Version 5.1
<#
Read-only fallback for the truncated theme probe in BSI Customizer <= 2.6.3.
Reads metadata and bounded hex excerpts, never loads/executes/patches HD-Player.
No network requests, account access, registry changes or third-party packages.
The sole output is a new %TEMP%\BSI-theme-layout-<id>.txt file. Review before sharing.
Candidate tables are diagnostic evidence, NOT permission to patch them.
#>
param(
    [string]$PlayerPath = 'C:\Program Files\BlueStacks_msi5\HD-Player.exe'
)
$ErrorActionPreference = 'Stop'
$PlayerPath = (Resolve-Path -LiteralPath $PlayerPath).Path
[byte[]]$bytes = [IO.File]::ReadAllBytes($PlayerPath)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
    throw 'The selected file is not a Windows executable.'
}
$report = New-Object 'System.Collections.Generic.List[string]'
function Line([string]$Text = '') { $report.Add($Text) }
function BE32([int]$At) {
    return [long]$bytes[$At] * 16777216 + [long]$bytes[$At + 1] * 65536 +
        [long]$bytes[$At + 2] * 256 + [long]$bytes[$At + 3]
}
function Hex-Window([string]$Label, [long]$At, [int]$Length) {
    Line ('{0}: file offset 0x{1:x}, requested {2} bytes' -f $Label, $At, $Length)
    if ($At -lt 0 -or $At -ge $bytes.Length) { Line '  (outside file)'; return }
    $end = [Math]::Min([long]$bytes.Length, $At + $Length)
    for ($p = $At; $p -lt $end; $p += 16) {
        $count = [int][Math]::Min(16, $end - $p)
        $hex = [BitConverter]::ToString($bytes, [int]$p, $count).Replace('-', ' ')
        Line ('  {0:x8}: {1}' -f $p, $hex)
    }
}
# Latin-1 is a one-to-one byte view. Ordinal searches preserve NUL bytes and
# offsets; culture-sensitive searches would be incorrect for binary data.
$latin1 = [Text.Encoding]::GetEncoding(28591)
$view = $latin1.GetString($bytes)
function Find-Offsets([byte[]]$Needle, [int]$Limit = 128) {
    $s = $latin1.GetString($Needle)
    $next = 0
    for ($n = 0; $n -lt $Limit; $n++) {
        $at = $view.IndexOf($s, $next, [StringComparison]::Ordinal)
        if ($at -lt 0) { break }
        $at
        $next = $at + 1
    }
}
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or
    [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw 'Invalid PE header.' }
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
$count = [BitConverter]::ToUInt16($bytes, $pe + 6)
$optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
$table = $pe + 24 + $optionalSize
if ($count -gt 96 -or $table + 40 * $count -gt $bytes.Length) { throw 'Invalid PE section table.' }
$sections = @()
for ($i = 0; $i -lt $count; $i++) {
    $p = $table + 40 * $i
    $sections += [pscustomobject]@{
        Name = [Text.Encoding]::ASCII.GetString($bytes, $p, 8).Trim([char]0)
        VA = [long][BitConverter]::ToUInt32($bytes, $p + 12)
        RawSize = [long][BitConverter]::ToUInt32($bytes, $p + 16)
        Raw = [long][BitConverter]::ToUInt32($bytes, $p + 20)
        Flags = [long][BitConverter]::ToUInt32($bytes, $p + 36)
    }
}
function Raw-To-Rva([long]$At) {
    foreach ($s in $sections) {
        if ($At -ge $s.Raw -and $At -lt $s.Raw + $s.RawSize) { return $s.VA + $At - $s.Raw }
    }
    return -1
}
function Rva-To-Raw([long]$At) {
    foreach ($s in $sections) {
        if ($At -ge $s.VA -and $At -lt $s.VA + $s.RawSize) {
            $raw = $s.Raw + $At - $s.VA
            if ($raw -ge 0 -and $raw -lt $bytes.Length) { return $raw }
        }
    }
    return -1
}
Line 'BSI read-only theme layout report v1'
Line 'Candidate resource tables below are NOT verified mappings.'
Line ('File: ' + $PlayerPath)
Line ('Size: ' + $bytes.Length)
$sha = [Security.Cryptography.SHA256]::Create()
try { Line ('SHA256: ' + [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()) }
finally { $sha.Dispose() }
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($PlayerPath)
Line ('File version: ' + $version.FileVersion)
Line ('Product version: ' + $version.ProductVersion)
Line ('Machine: 0x{0:x}; PE header: 0x{1:x}' -f $machine, $pe)
Line '[1] PE sections (raw offsets and RVAs are different address spaces)'
foreach ($s in $sections) {
    Line ('  {0}: raw=0x{1:x} size=0x{2:x} RVA=0x{3:x} flags=0x{4:x}' -f $s.Name, $s.Raw, $s.RawSize, $s.VA, $s.Flags)
}
Line '[2] Theme filename markers (at most 128 matches)'
$markers = @(Find-Offsets ([Text.Encoding]::BigEndianUnicode.GetBytes('UiThemeObject.qml')))
Line ('UTF-16BE marker count: ' + $markers.Count)
foreach ($at in $markers) { Hex-Window 'Filename record context' ([Math]::Max(0, $at - 16)) 128 }
Line '[3] Previously observed theme record (offset from the supplied screenshot, not a write target)'
Hex-Window 'Record at 0x107ca90' 0x107ca90 128
Line '[4] Canonical root candidates (search capped at 4096 prefix hits; at most 64 excerpts)'
$roots = @(Find-Offsets ([byte[]](0, 0, 0, 0, 0, 2)) 4096)
$shown = 0
foreach ($at in $roots) {
    if ($at + 14 -gt $bytes.Length) { continue }
    $children = BE32 ($at + 6)
    $first = BE32 ($at + 10)
    if ($children -le 0 -or $children -ge 200000 -or $first -ne 1) { continue }
    Line ('  root=0x{0:x}, child count={1}, first child={2}' -f $at, $children, $first)
    Hex-Window 'Root and first nodes (stride not assumed)' $at 384
    $shown++
    if ($shown -ge 64) { break }
}
Line ('Root excerpts shown: ' + $shown)
Line '[5] x64 tree-pointer LEA candidates (NOT verified resource registrations)'
# A bounded window retains the neighboring version/R8/R9 argument bytes.
# This is evidence for inspection only; no call or pointer is executed.
$shown = 0
$seen = New-Object 'System.Collections.Generic.HashSet[long]'
if ($machine -eq 0x8664) {
    foreach ($at in @(Find-Offsets ([byte[]](0x48, 0x8d, 0x15)) 8192)) {
        if ($at + 7 -gt $bytes.Length) { continue }
        $executable = $false
        foreach ($s in $sections) {
            if ($at -ge $s.Raw -and $at + 7 -le $s.Raw + $s.RawSize -and ($s.Flags -band 0x20000000)) {
                $executable = $true; break
            }
        }
        if (-not $executable) { continue }
        $rva = Raw-To-Rva $at
        $destination = Rva-To-Raw ($rva + 7 + [BitConverter]::ToInt32($bytes, $at + 3))
        if ($destination -lt 0 -or $destination + 14 -gt $bytes.Length) { continue }
        if ($bytes[$destination + 4] -ne 0 -or $bytes[$destination + 5] -ne 2) { continue }
        if (-not $seen.Add($destination)) { continue }
        Line ('  LEA=0x{0:x}, candidate tree=0x{1:x}' -f $at, $destination)
        Hex-Window 'Argument-setting instruction context' ([Math]::Max(0, $at - 32)) 96
        Hex-Window 'Pointed-to candidate tree' $destination 384
        $shown++
        if ($shown -ge 64) { break }
    }
}
Line ('LEA excerpts shown: ' + $shown)
Line '[6] Binary QRC headers (at most 64 matches)'
foreach ($at in @(Find-Offsets ([Text.Encoding]::ASCII.GetBytes('qres')) 64)) {
    if ($at + 20 -gt $bytes.Length) { continue }
    $v = BE32 ($at + 4)
    if ($v -ge 1 -and $v -le 3) { Hex-Window 'qres header' $at 32 }
}
Line 'Layout report complete. Emulator files were read only; nothing was uploaded.'
$reportPath = Join-Path $env:TEMP ('BSI-theme-layout-{0}.txt' -f [Guid]::NewGuid().ToString('N'))
# CreateNew refuses to overwrite an existing file or follow an existing link.
$stream = [IO.File]::Open($reportPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $encoded = [Text.Encoding]::UTF8.GetBytes(($report -join "`r`n") + "`r`n")
    $stream.Write($encoded, 0, $encoded.Length)
} finally { $stream.Dispose() }
Write-Host ('Saved: ' + $reportPath)
Write-Host 'Review the report; upload just this small text file or paste its contents.'
Start-Process -FilePath 'notepad.exe' -ArgumentList ('"' + $reportPath + '"')
