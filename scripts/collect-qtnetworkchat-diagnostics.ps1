param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$BuildDir = "build-qt6-mingw",
    [string]$InstallDir = "",
    [string]$AppDataDir = "",
    [string]$NotesPath = "",
    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    '(^|["''\s{,])secret[-_\s]?key["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])access[-_\s]?key["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])session[-_\s]?token["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-RepoPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Get-Sha256Hex([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return "unknown"
    }
    $stream = [System.IO.File]::OpenRead($PathValue)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hashBytes = $sha256.ComputeHash($stream)
            return (($hashBytes | ForEach-Object { $_.ToString("x2") }) -join "")
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return
    }
    if (([System.IO.Path]::GetExtension($PathValue)).ToLowerInvariant() -in @(".zip", ".dll", ".exe", ".dmp", ".mdmp")) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $PathValue -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "(<redacted>|\\u003credacted\\u003e|&lt;redacted&gt;)") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
    }
}

function Copy-SafeArtifact(
    [string]$SourcePath,
    [string]$TargetRoot,
    [string]$TargetRelativePath,
    [string]$Kind,
    [System.Collections.ArrayList]$ManifestInputs,
    [System.Collections.ArrayList]$ScanPaths
) {
    if ([string]::IsNullOrWhiteSpace($SourcePath) -or -not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        return $false
    }
    $targetPath = Join-Path $TargetRoot $TargetRelativePath
    $targetParent = Split-Path -Parent $targetPath
    if (-not [string]::IsNullOrWhiteSpace($targetParent)) {
        New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
    }
    Copy-Item -LiteralPath $SourcePath -Destination $targetPath -Force
    [void]$ScanPaths.Add($SourcePath)
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $Kind
        sourceName = Split-Path -Leaf $SourcePath
        packagedAs = ($TargetRelativePath -replace "\\", "/")
        bytes = (Get-Item -LiteralPath $SourcePath).Length
        sha256 = Get-Sha256Hex $SourcePath
    })
    $true
}

function Resolve-WindowsPackageManifestPath([string]$BuildRoot) {
    $preferred = Join-Path $BuildRoot "release-package\\QtNetworkChat-1.0.0-win-x64\\manifest.json"
    if (Test-Path -LiteralPath $preferred -PathType Leaf) {
        return $preferred
    }

    $releasePackageRoot = Join-Path $BuildRoot "release-package"
    if (-not (Test-Path -LiteralPath $releasePackageRoot -PathType Container)) {
        return $preferred
    }

    $manifests = Get-ChildItem -LiteralPath $releasePackageRoot -Recurse -Filter manifest.json -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending
    foreach ($candidate in $manifests) {
        try {
            $manifest = Get-Content -LiteralPath $candidate.FullName -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
            if (($manifest.PSObject.Properties.Name -contains "packageFormat") -and $manifest.packageFormat -eq "qtnetworkchat-windows-package-v1") {
                return $candidate.FullName
            }
        } catch {
        }
    }

    $preferred
}

function New-DumpInventoryEntry([System.IO.FileInfo]$FileInfo, [string]$RootLabel, [string]$RootPath) {
    [pscustomobject]@{
        root = $RootLabel
        relativePath = $FileInfo.FullName.Substring($RootPath.Length).TrimStart('\', '/')
        bytes = $FileInfo.Length
        lastWriteTimeUtc = $FileInfo.LastWriteTimeUtc.ToString("o")
    }
}

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$stagingDir = Join-Path $resolvedOutputDir "release-diagnostics"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$buildRoot = Resolve-RepoPath $BuildDir
$resolvedInstallDir = Resolve-OptionalPath $InstallDir
$resolvedAppDataDir = Resolve-OptionalPath $AppDataDir
$resolvedNotesPath = Resolve-OptionalPath $NotesPath

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList
$dumpInventory = New-Object System.Collections.ArrayList

[void](Copy-SafeArtifact (Join-Path $buildRoot "local-verification-status.json") $stagingDir "verification/local-verification-status.json" "local-verification-status" $manifestInputs $scanPaths)
[void](Copy-SafeArtifact (Join-Path $buildRoot "Testing\Temporary\LastTest.log") $stagingDir "verification/LastTest.log" "ctest-log" $manifestInputs $scanPaths)
[void](Copy-SafeArtifact (Resolve-RepoPath "docs\automation-status.md") $stagingDir "docs/automation-status.md" "automation-status" $manifestInputs $scanPaths)
[void](Copy-SafeArtifact (Join-Path $buildRoot "local-release-review\local-release-review-manifest.json") $stagingDir "release-review/local-release-review-manifest.json" "local-release-review-manifest" $manifestInputs $scanPaths)
[void](Copy-SafeArtifact (Join-Path $buildRoot "release-delivery-handoff\release-delivery-handoff-manifest.json") $stagingDir "release-delivery/release-delivery-handoff-manifest.json" "release-delivery-handoff-manifest" $manifestInputs $scanPaths)
[void](Copy-SafeArtifact (Resolve-WindowsPackageManifestPath $buildRoot) $stagingDir "windows/manifest.json" "windows-package-manifest" $manifestInputs $scanPaths)

if (-not [string]::IsNullOrWhiteSpace($resolvedInstallDir) -and (Test-Path -LiteralPath $resolvedInstallDir -PathType Container)) {
    [void](Copy-SafeArtifact (Join-Path $resolvedInstallDir "manifest.json") $stagingDir "install/manifest.json" "installed-package-manifest" $manifestInputs $scanPaths)
    [void](Copy-SafeArtifact (Join-Path $resolvedInstallDir "install-record.json") $stagingDir "install/install-record.json" "install-record" $manifestInputs $scanPaths)
    Get-ChildItem -LiteralPath $resolvedInstallDir -Recurse -Include *.dmp,*.mdmp -File -ErrorAction SilentlyContinue | ForEach-Object {
        [void]$dumpInventory.Add((New-DumpInventoryEntry $_ "install" $resolvedInstallDir))
    }
}

if (-not [string]::IsNullOrWhiteSpace($resolvedAppDataDir) -and (Test-Path -LiteralPath $resolvedAppDataDir -PathType Container)) {
    Get-ChildItem -LiteralPath $resolvedAppDataDir -Recurse -Include *.dmp,*.mdmp -File -ErrorAction SilentlyContinue | ForEach-Object {
        [void]$dumpInventory.Add((New-DumpInventoryEntry $_ "appdata" $resolvedAppDataDir))
    }
}

if (-not [string]::IsNullOrWhiteSpace($resolvedNotesPath) -and (Test-Path -LiteralPath $resolvedNotesPath -PathType Leaf)) {
    [void](Copy-SafeArtifact $resolvedNotesPath $stagingDir "notes/notes.txt" "notes" $manifestInputs $scanPaths)
}

if ($manifestInputs.Count -eq 0) {
    throw "No diagnostic artifacts were collected."
}

$inventoryPath = Join-Path $stagingDir "crash-dump-inventory.json"
([ordered]@{
    format = "qtnetworkchat-crash-dump-inventory-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    dumpCount = $dumpInventory.Count
    dumps = @($dumpInventory)
    notes = "Crash dump files are inventoried by relative path, size, and timestamp only. Raw dump bytes are not bundled into this sanitized diagnostics package."
} | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $inventoryPath -Encoding UTF8
[void]$scanPaths.Add($inventoryPath)
[void]$manifestInputs.Add([pscustomobject]@{
    kind = "crash-dump-inventory"
    sourceName = "generated"
    packagedAs = "crash-dump-inventory.json"
    bytes = (Get-Item -LiteralPath $inventoryPath).Length
    sha256 = Get-Sha256Hex $inventoryPath
})

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Sensitive-looking fields were found in diagnostics artifacts: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifestPath = Join-Path $stagingDir "manifest.json"
$packagePath = Join-Path $resolvedOutputDir "release-diagnostics.zip"
$manifest = [ordered]@{
    format = "qtnetworkchat-release-diagnostics-package-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = ($sensitiveHits.Count -eq 0)
    dumpInventoryOnly = $true
    dumpCount = $dumpInventory.Count
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    sensitiveHits = @($sensitiveHits)
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}
Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $packagePath -Force

Write-Host "release diagnostics package"
Write-Host ("  package: {0}" -f $packagePath)
Write-Host ("  dump inventory count: {0}" -f $dumpInventory.Count)
Write-Host ("  sanitized inputs: {0}" -f $manifestInputs.Count)
