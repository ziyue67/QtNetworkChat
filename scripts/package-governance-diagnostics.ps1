param(
    [Parameter(Mandatory = $true)]
    [string]$GovernanceDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,

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

function Resolve-RequiredPath([string]$PathValue, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue)) {
        throw ("{0} not found: {1}" -f $Label, $PathValue)
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

function Copy-DiagnosticFile([string]$SourcePath, [string]$TargetDir, [string]$Kind, [System.Collections.ArrayList]$ManifestInputs, [System.Collections.ArrayList]$ScanPaths) {
    if ([string]::IsNullOrWhiteSpace($SourcePath) -or -not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        return
    }
    $targetName = Split-Path -Leaf $SourcePath
    $targetPath = Join-Path $TargetDir $targetName
    Copy-Item -LiteralPath $SourcePath -Destination $targetPath -Force
    [void]$ScanPaths.Add($SourcePath)
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $Kind
        source = $SourcePath
        packagedAs = $targetName
        bytes = (Get-Item -LiteralPath $SourcePath).Length
    })
}

$resolvedGovernanceDir = Resolve-RequiredPath $GovernanceDir "GovernanceDir"
if (-not (Test-Path -LiteralPath $resolvedGovernanceDir -PathType Container)) {
    throw "GovernanceDir is not a directory: $GovernanceDir"
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "large-file-governance-diagnostics.zip"
}
$resolvedPackagePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackagePath)
$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}

$stagingDir = Join-Path $resolvedOutputDir "large-file-governance-diagnostics"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

$knownFiles = @(
    @{ Name = "governance-alert-overview.json"; Kind = "alert-overview" },
    @{ Name = "last-health.json"; Kind = "health" },
    @{ Name = "large-file-route-summary.json"; Kind = "route-summary" },
    @{ Name = "large-file-route-alert-summary.json"; Kind = "route-alert" },
    @{ Name = "s3-request-results-summary.json"; Kind = "s3-summary" },
    @{ Name = "s3-request-results-alert-summary.json"; Kind = "s3-alert" },
    @{ Name = "s3-real-backend-evidence.json"; Kind = "s3-evidence" },
    @{ Name = "s3-real-backend-evidence.md"; Kind = "s3-evidence" },
    @{ Name = "s3-real-backend-evidence-alert-summary.json"; Kind = "s3-evidence-alert" },
    @{ Name = "s3-stability-runbook.json"; Kind = "s3-stability-runbook" },
    @{ Name = "s3-stability-runbook.md"; Kind = "s3-stability-runbook" },
    @{ Name = "s3-stability-runbook-alert-summary.json"; Kind = "s3-stability-runbook-alert" },
    @{ Name = "receipt-rotation-summary.json"; Kind = "rotation-summary" },
    @{ Name = "receipt-rotation-alert-summary.json"; Kind = "rotation-alert" },
    @{ Name = "large-file-governance-report.md"; Kind = "report" },
    @{ Name = "large-file-governance-report.html"; Kind = "report" },
    @{ Name = "aggregate-alerts.log"; Kind = "log" },
    @{ Name = "health-check.log"; Kind = "log" },
    @{ Name = "notify-unhealthy.log"; Kind = "log" },
    @{ Name = "governance-report.log"; Kind = "log" },
    @{ Name = "route-summary-alerts.log"; Kind = "log" },
    @{ Name = "s3-request-analysis.log"; Kind = "log" },
    @{ Name = "receipt-rotation-alerts.log"; Kind = "log" }
)

foreach ($entry in $knownFiles) {
    Copy-DiagnosticFile (Join-Path $resolvedGovernanceDir $entry.Name) $stagingDir $entry.Kind $manifestInputs $scanPaths
}

if (-not [string]::IsNullOrWhiteSpace($NotesPath)) {
    $resolvedNotesPath = Resolve-RequiredPath $NotesPath "NotesPath"
    Copy-DiagnosticFile $resolvedNotesPath $stagingDir "notes" $manifestInputs $scanPaths
}

if ($manifestInputs.Count -eq 0) {
    throw "No governance diagnostic artifacts were found."
}

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}

if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; diagnostics package was not created."
}

$manifestPath = Join-Path $stagingDir "manifest.json"
[pscustomobject]@{
    createdAt = (Get-Date).ToUniversalTime().ToString("o")
    packageFormat = "qtnetworkchat-large-file-governance-diagnostics-v1"
    readOnly = $true
    sensitiveHits = $sensitiveHits.Count
    inputs = @($manifestInputs)
    notes = "Package contains only governance diagnostic artifacts; it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}

Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

Write-Host "large file governance diagnostics package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)
Write-Host ""
Write-Host "This diagnostics packaging run is read-only: it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
