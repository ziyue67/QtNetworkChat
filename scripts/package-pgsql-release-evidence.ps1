param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$NotesPath,

    [string]$DatabaseHealthPath,
    [string]$DatabaseHealthStatusPath,
    [string]$DatabaseHealthDashboardPath,
    [string]$SmokeJsonPath,
    [string]$SmokeMarkdownPath,
    [string]$SmokeBootstrapJsonPath,
    [string]$MigrationJsonPath,
    [string]$MigrationMarkdownPath,
    [string]$MigrationHtmlPath,
    [string]$RollbackPreviewPath,
    [string]$RollbackPreviewMarkdownPath,
    [string]$RollbackAuditPath,
    [string]$RollbackAuditMarkdownPath,
    [string]$AcceptanceJsonPath,
    [string]$AcceptanceMarkdownPath,
    [string]$LastRunPath,
    [string]$HistoryPath,
    [string]$HistoryMarkdownPath,
    [string]$AckPath,

    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'session[-_\s]?token',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
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

function Copy-EvidenceFile(
    [string]$SourcePath,
    [string]$TargetDir,
    [string]$Kind,
    [System.Collections.ArrayList]$ManifestInputs,
    [System.Collections.ArrayList]$ScanPaths
) {
    $resolvedSource = Resolve-OptionalPath $SourcePath
    if ([string]::IsNullOrWhiteSpace($resolvedSource) -or -not (Test-Path -LiteralPath $resolvedSource -PathType Leaf)) {
        return
    }
    $targetName = Split-Path -Leaf $resolvedSource
    $targetPath = Join-Path $TargetDir $targetName
    Copy-Item -LiteralPath $resolvedSource -Destination $targetPath -Force
    [void]$ScanPaths.Add($resolvedSource)
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $Kind
        source = $resolvedSource
        packagedAs = $targetName
        bytes = (Get-Item -LiteralPath $resolvedSource).Length
    })
}

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "pgsql-release-evidence.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "pgsql-release-evidence-manifest.json"
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}
$manifestParent = Split-Path -Parent $resolvedManifestPath
if (-not [string]::IsNullOrWhiteSpace($manifestParent)) {
    New-Item -ItemType Directory -Path $manifestParent -Force | Out-Null
}

$stagingDir = Join-Path $resolvedOutputDir "pgsql-release-evidence"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList
Copy-EvidenceFile $DatabaseHealthPath $stagingDir "database-health" $manifestInputs $scanPaths
Copy-EvidenceFile $DatabaseHealthStatusPath $stagingDir "database-health-status" $manifestInputs $scanPaths
Copy-EvidenceFile $DatabaseHealthDashboardPath $stagingDir "database-health-dashboard" $manifestInputs $scanPaths
Copy-EvidenceFile $SmokeJsonPath $stagingDir "pgsql-smoke-json" $manifestInputs $scanPaths
Copy-EvidenceFile $SmokeMarkdownPath $stagingDir "pgsql-smoke-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $SmokeBootstrapJsonPath $stagingDir "pgsql-smoke-bootstrap" $manifestInputs $scanPaths
Copy-EvidenceFile $MigrationJsonPath $stagingDir "migration-json" $manifestInputs $scanPaths
Copy-EvidenceFile $MigrationMarkdownPath $stagingDir "migration-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $MigrationHtmlPath $stagingDir "migration-html" $manifestInputs $scanPaths
Copy-EvidenceFile $RollbackPreviewPath $stagingDir "rollback-preview-json" $manifestInputs $scanPaths
Copy-EvidenceFile $RollbackPreviewMarkdownPath $stagingDir "rollback-preview-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $RollbackAuditPath $stagingDir "rollback-audit-json" $manifestInputs $scanPaths
Copy-EvidenceFile $RollbackAuditMarkdownPath $stagingDir "rollback-audit-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $AcceptanceJsonPath $stagingDir "acceptance-json" $manifestInputs $scanPaths
Copy-EvidenceFile $AcceptanceMarkdownPath $stagingDir "acceptance-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $LastRunPath $stagingDir "last-run" $manifestInputs $scanPaths
Copy-EvidenceFile $HistoryPath $stagingDir "history-json" $manifestInputs $scanPaths
Copy-EvidenceFile $HistoryMarkdownPath $stagingDir "history-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $AckPath $stagingDir "ack" $manifestInputs $scanPaths

if (-not [string]::IsNullOrWhiteSpace($NotesPath)) {
    $resolvedNotesPath = Resolve-OptionalPath $NotesPath
    if (Test-Path -LiteralPath $resolvedNotesPath -PathType Leaf) {
        Copy-EvidenceFile $resolvedNotesPath $stagingDir "notes" $manifestInputs $scanPaths
    }
}

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}

if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("PostgreSQL release evidence contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}
Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

$manifest = [ordered]@{
    format = "qtnetworkchat-pgsql-release-evidence-package-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = ($sensitiveHits.Count -eq 0)
    packagePath = $resolvedPackagePath
    stagingDir = $stagingDir
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    sensitiveHits = @($sensitiveHits.ToArray())
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedManifestPath -Encoding UTF8

Write-Host "pgsql release evidence package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
