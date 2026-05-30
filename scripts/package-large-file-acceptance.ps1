param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,

    [string[]]$RouteLogPath,

    [string]$RouteSummaryPath,

    [string]$S3SummaryPath,

    [string]$ReconcileDir,

    [string]$RotationSummaryPath,

    [string]$NotesPath,

    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    "endpoint\s*=",
    "bucket\s*=",
    "objectUrl\s*=",
    "object-url\s*=",
    "https?://",
    "access[-_\s]?key",
    "secret[-_\s]?key",
    "session[-_\s]?token",
    "Authorization",
    "Credential",
    "Signature"
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }
    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw "Input path not found: $PathValue"
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return
    }

    $lineNumber = 0
    Get-Content -LiteralPath $PathValue | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern) {
                [void]$Hits.Add(("{0}:{1}:{2}" -f $PathValue, $lineNumber, $pattern))
            }
        }
    }
}

function Copy-InputFile([string]$SourcePath, [string]$TargetDir, [string]$NamePrefix, [System.Collections.ArrayList]$ManifestInputs) {
    if ([string]::IsNullOrWhiteSpace($SourcePath)) {
        return
    }
    $resolved = Resolve-OptionalPath $SourcePath
    $targetName = if ([string]::IsNullOrWhiteSpace($NamePrefix)) {
        Split-Path -Leaf $resolved
    } else {
        "{0}-{1}" -f $NamePrefix, (Split-Path -Leaf $resolved)
    }
    $target = Join-Path $TargetDir $targetName
    Copy-Item -LiteralPath $resolved -Destination $target -Force
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $NamePrefix
        source = $resolved
        packagedAs = $targetName
        bytes = (Get-Item -LiteralPath $resolved).Length
    })
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "large-file-acceptance-package.zip"
}
$resolvedPackagePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackagePath)
$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}

$stagingDir = Join-Path $resolvedOutputDir "large-file-acceptance-package"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

if ($null -ne $RouteLogPath) {
    $index = 0
    foreach ($pathValue in $RouteLogPath) {
        $index += 1
        $resolved = Resolve-OptionalPath $pathValue
        [void]$scanPaths.Add($resolved)
        Copy-InputFile $resolved $stagingDir ("route-log-{0:D2}" -f $index) $manifestInputs
    }
}

foreach ($entry in @(
        @{ Kind = "route-summary"; Path = $RouteSummaryPath },
        @{ Kind = "s3-summary"; Path = $S3SummaryPath },
        @{ Kind = "rotation-summary"; Path = $RotationSummaryPath },
        @{ Kind = "notes"; Path = $NotesPath })) {
    $resolved = Resolve-OptionalPath $entry.Path
    if ($null -ne $resolved) {
        [void]$scanPaths.Add($resolved)
        Copy-InputFile $resolved $stagingDir $entry.Kind $manifestInputs
    }
}

if (-not [string]::IsNullOrWhiteSpace($ReconcileDir)) {
    $resolvedReconcileDir = Resolve-OptionalPath $ReconcileDir
    if (-not (Test-Path -LiteralPath $resolvedReconcileDir -PathType Container)) {
        throw "ReconcileDir is not a directory: $ReconcileDir"
    }
    $targetReconcileDir = Join-Path $stagingDir "reconcile"
    New-Item -ItemType Directory -Path $targetReconcileDir -Force | Out-Null
    foreach ($name in @("receipts.jsonl", "fallbacks.jsonl", "reconcile.log", "s3-analysis-summary.json", "s3-analysis.log", "rotation-summary.json", "rotation.log")) {
        $candidate = Join-Path $resolvedReconcileDir $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            [void]$scanPaths.Add($candidate)
            Copy-Item -LiteralPath $candidate -Destination (Join-Path $targetReconcileDir $name) -Force
            [void]$manifestInputs.Add([pscustomobject]@{
                kind = "reconcile"
                source = $candidate
                packagedAs = "reconcile/$name"
                bytes = (Get-Item -LiteralPath $candidate).Length
            })
        }
    }
}

if ($manifestInputs.Count -eq 0) {
    throw "No acceptance artifacts were provided."
}

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($scanPath in $scanPaths) {
    Add-SensitiveHits $scanPath $sensitiveHits
}

if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; package was not created."
}

$manifestPath = Join-Path $stagingDir "manifest.json"
[pscustomobject]@{
    createdAt = (Get-Date).ToUniversalTime().ToString("o")
    packageFormat = "qtnetworkchat-large-file-acceptance-v1"
    readOnly = $true
    sensitiveHits = $sensitiveHits.Count
    inputs = @($manifestInputs)
    notes = "Package contains only local acceptance artifacts; it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}

Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

Write-Host "large file acceptance package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)
Write-Host ""
Write-Host "This packaging run is read-only: it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
