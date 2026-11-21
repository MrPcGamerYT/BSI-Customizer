param(
    [Parameter(Mandatory=$true)][string]$Prepared,
    [Parameter(Mandatory=$true)][string]$Build,
    [Parameter(Mandatory=$true)][string]$Results,
    [switch]$ExplicitlyAllowOriginalDll
)
$ErrorActionPreference = 'Stop'
# Nonzero child outcomes are evidence rows, not an excuse to stop after case 1.
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest
function Write-BoundedEvidence {
    param([string]$Title, [object]$Data)
    $compact = ConvertTo-Json -InputObject $Data -Compress -Depth 8
    # Check messages truncate at 4096 characters, and per-row notices hit the
    # per-step count cap. Pair rows: <=8 notices and <3800 characters each.
    if ($compact.Length -gt 3800) { throw 'Sanitized annotation exceeds fixed reporting budget' }
    Write-Output ('::notice title=' + $Title + '::' + $compact)
}
if (-not $ExplicitlyAllowOriginalDll) { throw 'Explicit original-DLL execution opt-in required' }
$manifest = Get-Content (Join-Path $Prepared 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.archiveSha256 -ne '80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17' -or $manifest.mesaArchiveSha256 -ne '19c74aad7b09e682473b8de4c35b47e6498de97b71cae5511a231b8eb02f2b22' -or $manifest.variants.Count -ne 5) { throw 'Wrong reviewed archive/variant set' }
New-Item -ItemType Directory -Path $Results | Out-Null
$recipient = Join-Path $Prepared 'offline recipients'
New-Item -ItemType Directory -Path $recipient | Out-Null
$programs = @{}
$hashes = @{}
foreach ($kind in @('control','dinput','version')) {
    $dir = Join-Path $recipient $kind
    New-Item -ItemType Directory -Path $dir | Out-Null
    $program = Join-Path $dir 'PotatoProbe.exe'
    Copy-Item (Join-Path $Build "Release/potato-probe-$kind.exe") $program
    foreach ($entry in $manifest.mesaDlls) {
        $source = Join-Path $manifest.mesaProviderDirectory $entry.name
        if ((Get-FileHash $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw 'Mesa bytes changed after preparation' }
        Copy-Item $source (Join-Path $dir $entry.name)
    }
    $programs[$kind] = $program
    $hashes[$kind] = (Get-FileHash $program -Algorithm SHA256).Hash
}
# All DLLs above are trusted pinned Mesa; no supplied DLL has been made loadable
# beside a host yet. Establish/verify the block BEFORE staging/running one.
$profiles = @(Get-NetFirewallProfile | Select-Object Name,Enabled)
$rules = @()
$rows = @()
$alternatives = @()
$complete = $false
try {
    Set-NetFirewallProfile -Profile Domain,Private,Public -Enabled True
    if (@(Get-NetFirewallProfile | Where-Object { $_.Enabled -ne 'True' }).Count -ne 0) { throw 'Firewall unavailable; original DLL NOT run' }
    foreach ($kind in @('control','dinput','version')) {
        $name = 'BSI-Potato-Offline-' + [guid]::NewGuid().ToString('N')
        New-NetFirewallRule -Name $name -DisplayName $name -Direction Outbound -Program $programs[$kind] -Action Block -Profile Any -Enabled True | Out-Null
        $rules += $name
        $rule = Get-NetFirewallRule -Name $name
        $app = $rule | Get-NetFirewallApplicationFilter
        if ($rule.Enabled -ne 'True' -or $rule.Action -ne 'Block' -or $rule.Direction -ne 'Outbound' -or $app.Program -ne $programs[$kind]) { throw 'Per-image outbound block verification failed; original DLL NOT run' }
    }
    $env:BSI_POTATO_PROBE_APPROVED = '1'
    # The launcher CONSTRUCTS an allowlisted child environment (no tokens) and
    # resumes it only after assigning a kill-on-close / process-limit-one job.
    $cases = @(@{ sha256='none';proxyKind='control';expectedLod=0.0;forcesMinimumLod=$false }) + @($manifest.variants)
    foreach ($case in $cases) {
        $kind = $case.proxyKind
        $target = $programs[$kind]
        $dll = Join-Path (Split-Path $target) 'BsiPotatoOriginal.dll'
        if ($kind -ne 'control') {
            if (Test-Path $dll) { throw 'Previous original DLL was not removed after its process exit' }
            $source = Join-Path $Prepared ('members/' + $case.sha256 + '.bin')
            if ((Get-FileHash $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $case.sha256) { throw 'Original bytes changed before test' }
            Copy-Item $source $dll
            if ((Get-FileHash $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $case.sha256) { throw 'Staged original differs' }
        }
        try {
            foreach ($sampling in @('texture','sampler')) {
                $childReport = Join-Path $Prepared ($case.sha256 + '-' + $sampling + '.json')
                $launcher = $programs['control']
                & $launcher --launch $target $childReport $sampling
                $exit = $LASTEXITCODE
                $row = [ordered]@{ sha256=$case.sha256;proxyKind=$kind;sampling=$sampling;outboundBlocked=$true;launcherExitCode=$exit;containedBeforeResume=$false;reportAvailable=$false;passed=$false;gameExecuted=$false;physicalGpuTested=$false }
                $parentReport = $childReport + '.launch.json'
                if (Test-Path $parentReport) {
                    if ((Get-Item $parentReport).Length -gt 1024) { throw 'Oversize parent report refused' }
                    $launch = Get-Content $parentReport -Raw | ConvertFrom-Json
                    if ($launch.createdSuspended -ne $true -or $launch.containedBeforeResume -ne $true -or $launch.processLimit -ne 1 -or $launch.timeout -isnot [bool]) { throw 'Parent did not verify bounded child execution' }
                    $row.containedBeforeResume = $true
                    $row.timeout = $launch.timeout
                    $row.childExitCode = [long]$launch.childExitCode
                }
                if (Test-Path $childReport) {
                    if ((Get-Item $childReport).Length -gt 8192) { throw 'Oversize child report refused' }
                    $r = Get-Content $childReport -Raw | ConvertFrom-Json
                    $row.reportAvailable = $true
                    if ($r.kind -eq 'original-probe-prerequisite-failure') {
                        $row.failureStage = [int]$r.failureStage
                        $row.win32Error = [long]$r.win32Error
                    } elseif ($r.kind -eq 'original-supplied-DLL-WGL-software-probe') {
                        # Select only known schema fields; never upload raw child
                        # files, logs, module paths, dumps, IDs or original bytes.
                        foreach ($key in @('importLoaded','anchorCalled','samplerBound','depthTestEnabled','activeUnitRestored','softwareRenderer','parametersPersistAfterDraw','pixelsChanged','drawMutationAfterSwap','swapEntryLeadsIntoOriginal','drawEntryLeadsIntoOriginal','bindEntryLeadsIntoOriginal','bindExportEntryLeadsIntoOriginal','bindEntriesDiffer','mechanismObserved','gameExecuted','physicalGpuTested')) {
                            if ($r.$key -isnot [bool]) { throw 'Nonboolean child evidence field' }
                            $row[$key] = $r.$key
                        }
                        foreach ($key in @('textureBias','textureMinLod','samplerBias','samplerMinLod','bindOnlyBias','bindOnlyMinLod','bindExportOnlyBias','bindExportOnlyMinLod','sampledMip','minifiedMip','warmupAttempts','firstRemainingGlError')) {
                            $value = [double]$r.$key
                            if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or [math]::Abs($value) -gt 1000000) { throw 'Invalid numeric child evidence field' }
                            $row[$key] = $value
                        }
                        foreach ($key in @('beforePixel','afterPixel','nativeMinifiedPixel','minifiedPixel')) {
                            if ($r.$key.Count -ne 4) { throw 'Bad pixel evidence' }
                            $pixel = @();foreach ($channel in $r.$key) { $n=[int]$channel;if ($n -lt 0 -or $n -gt 255) { throw 'Bad pixel channel' };$pixel += $n }
                            $row[$key] = $pixel
                        }
                        $expectedMinimum = if ($case.forcesMinimumLod) { [double]$case.expectedLod } else { -1000.0 }
                        $parameters = if ($kind -eq 'control') { $row.textureBias -eq 0 -and $row.textureMinLod -eq -1000 } else {
                            $row.textureBias -eq $case.expectedLod -and $row.textureMinLod -eq $expectedMinimum -and
                            ($sampling -ne 'sampler' -or ($row.samplerBias -eq $case.expectedLod -and $row.samplerMinLod -eq $expectedMinimum))
                        }
                        $bindParameters = $row.bindOnlyBias -eq $case.expectedLod -and $row.bindOnlyMinLod -eq $expectedMinimum
                        $expectedMinified = if ($kind -eq 'control') { 4 } else { 6 }
                        $row.passed = $exit -eq 0 -and $row.minifiedMip -eq $expectedMinified -and $row.containedBeforeResume -and $row.mechanismObserved -and $row.softwareRenderer -and $parameters -and $bindParameters -and -not $row.anchorCalled -and -not $row.depthTestEnabled -and $row.activeUnitRestored -and -not $row.gameExecuted -and -not $row.physicalGpuTested -and ($row.importLoaded -eq ($kind -ne 'control'))
                    } else { throw 'Unknown child evidence kind' }
                }
                $rows += $row
                $safeEvidence = ($row | ConvertTo-Json -Compress -Depth 5)
                Write-Output $safeEvidence
                # Publish the complete selected schema once in finally below;
                # per-row notices hit the runner's per-step annotation cap.
                if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $hashes[$kind]) { throw 'Host bytes changed' }
                if ($kind -ne 'control' -and (Get-FileHash $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $case.sha256) { throw 'Original DLL bytes changed during test' }
            }
        } finally {
            # Every launcher has exited and closed/killed its contained job.
            if (Test-Path $dll) { Remove-Item -LiteralPath $dll }
        }
    }
    # Another actual attempt: the independently authored policy on a directly
    # known resource with depth disabled. No payload loaded; reversible private
    # sampler binding rather than persistent host-object mutation. This is NOT
    # a claim that the player's unidentified depth-disabled draws qualify.
    foreach ($sampling in @('owned-texture','owned-sampler')) {
        $target = $programs['control']
        $childReport = Join-Path $Prepared ($sampling + '.json')
        & $target --launch $target $childReport $sampling
        $exit = $LASTEXITCODE
        $row = [ordered]@{ sampling=$sampling;outboundBlocked=$true;launcherExitCode=$exit;containedBeforeResume=$false;reportAvailable=$false;passed=$false;gameExecuted=$false;physicalGpuTested=$false }
        $parentReport = $childReport + '.launch.json'
        if (Test-Path $parentReport) {
            if ((Get-Item $parentReport).Length -gt 1024) { throw 'Oversize independent parent report refused' }
            $launch = Get-Content $parentReport -Raw | ConvertFrom-Json
            if ($launch.createdSuspended -ne $true -or $launch.containedBeforeResume -ne $true -or $launch.processLimit -ne 1 -or $launch.timeout -isnot [bool]) { throw 'Independent child was not bounded before execution' }
            $row.containedBeforeResume = $true
            $row.timeout = $launch.timeout
            $row.childExitCode = [long]$launch.childExitCode
        }
        if (Test-Path $childReport) {
            if ((Get-Item $childReport).Length -gt 8192) { throw 'Oversize independent report refused' }
            $r = Get-Content $childReport -Raw | ConvertFrom-Json
            $row.reportAvailable = $true
            if ($r.kind -eq 'original-probe-prerequisite-failure') {
                $row.failureStage = [int]$r.failureStage
                $row.win32Error = [long]$r.win32Error
            } elseif ($r.kind -eq 'independent-owned-resource-WGL-alternative') {
                foreach ($key in @('originalDllLoaded','softwareRenderer','depthTestEnabled','offExact','sourceParametersUnchanged','unrelatedSamplerUntouched','minimumLodsVerified','biasFloorDifferenceObserved','privateBiasOffExact','gameExecuted','physicalGpuTested')) {
                    if ($r.$key -isnot [bool]) { throw 'Nonboolean independent evidence field' }
                    $row[$key] = $r.$key
                }
                $row.cycles = [int]$r.cycles
                $row.firstGlError = [int]$r.firstGlError
                foreach ($key in @('beforePixel','lightPixel','strongPixel','offPixel','nativeMinifiedPixel','lightFloorMinifiedPixel','strongFloorMinifiedPixel','privateBiasPixel','privateBiasMinifiedPixel','privateBiasOffMinifiedPixel')) {
                    if ($r.$key.Count -ne 4) { throw 'Bad independent pixel evidence' }
                    $pixel = @();foreach ($channel in $r.$key) { $n=[int]$channel;if ($n -lt 0 -or $n -gt 255) { throw 'Bad independent pixel channel' };$pixel += $n }
                    $row[$key] = $pixel
                }
                $row.passed = $exit -eq 0 -and $row.containedBeforeResume -and $r.passed -eq $true -and -not $row.originalDllLoaded -and $row.softwareRenderer -and -not $row.depthTestEnabled -and $row.offExact -and $row.sourceParametersUnchanged -and $row.unrelatedSamplerUntouched -and $row.minimumLodsVerified -and $row.biasFloorDifferenceObserved -and $row.privateBiasOffExact -and $row.cycles -eq 32 -and $row.firstGlError -eq 0 -and -not $row.gameExecuted -and -not $row.physicalGpuTested -and (($row.offPixel -join ',') -eq ($row.beforePixel -join ','))
            } else { throw 'Unknown independent evidence kind' }
        }
        $alternatives += $row
        Write-Output ($row | ConvertTo-Json -Compress -Depth 5)
        if ((Get-FileHash $target -Algorithm SHA256).Hash -ne $hashes['control']) { throw 'Independent test host bytes changed' }
    }
    $complete = $true
} finally {
    foreach ($name in $rules) { Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule }
    foreach ($profile in $profiles) { Set-NetFirewallProfile -Profile $profile.Name -Enabled $profile.Enabled }
    Remove-Item Env:BSI_POTATO_PROBE_APPROVED -ErrorAction SilentlyContinue
    $originalsPassed = $complete -and $rows.Count -eq 12 -and @($rows | Where-Object { -not $_.passed }).Count -eq 0
    $alternativesPassed = $complete -and $alternatives.Count -eq 2 -and @($alternatives | Where-Object { -not $_.passed }).Count -eq 0
    $allPassed = $originalsPassed -and $alternativesPassed
    $evidence = [ordered]@{ scope='Original supplied Potato DLLs AND independent known-resource comparison in authored WGL/llvmpipe hosts, NOT a player/game';archiveSha256=$manifest.archiveSha256;mesaArchiveSha256=$manifest.mesaArchiveSha256;complete=$complete;allMechanismCasesPassed=$originalsPassed;allIndependentCasesPassed=$alternativesPassed;rows=$rows;independentComparisons=$alternatives;installed=$false;redistributed=$false;gameAcceptance=$false }
    $evidence | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Results 'supplied-potato-results.json') -Encoding utf8
    Write-BoundedEvidence -Title 'Potato probe summary' -Data ([ordered]@{ scope=$evidence.scope;archiveSha256=$manifest.archiveSha256;mesaArchiveSha256=$manifest.mesaArchiveSha256;complete=$complete;allMechanismCasesPassed=$originalsPassed;allIndependentCasesPassed=$alternativesPassed;originalCaseCount=$rows.Count;independentCaseCount=$alternatives.Count;installed=$false;redistributed=$false;gameAcceptance=$false })
    for ($i = 0; $i -lt $rows.Count; $i += 2) {
        Write-BoundedEvidence -Title ('Potato original cases ' + $i) -Data @($rows | Select-Object -Skip $i -First 2)
    }
    if ($alternatives.Count -gt 0) { Write-BoundedEvidence -Title 'Independent owned sampler cases' -Data $alternatives }
}
if (-not $allPassed) { throw 'One or more actual original-DLL or independent comparison cases failed; inspect their separate evidence, do not claim a game fix' }
'PASS: five unique original DLLs, normal renamed import (anchor NOT called), actual detours and real Mesa software mip pixels with depth disabled; separately, private floor samplers restore exact Off output/state over 64 cycles; a private desktop bias+floor comparison matches the original Strong mip choices at native and minified gradients and also restores Off. Outbound block and bounded child lifetime. NOT player/physical-GPU acceptance.' | Out-File $env:GITHUB_STEP_SUMMARY -Append
