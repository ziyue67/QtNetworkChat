param(
    [string]$OutputDir = "build-qt6-mingw\automation-tasks",

    [int]$AckExpiryHours = 72,

    [int]$HistoryRetentionCount = 30,

    [switch]$PlanOnly,

    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'session[-_\s]?token',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Convert-ToRepoRelativePath([string]$PathValue) {
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
    if ($resolved.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $resolved.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    $resolved
}

function Assert-NoSensitiveText([string]$Label, [string[]]$Values) {
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($pattern in $sensitivePatterns) {
            if ($value -match $pattern -and $value -notmatch "<redacted>") {
                throw ("{0} contains sensitive-looking text rejected by automation bootstrap: {1}" -f $Label, $pattern)
            }
        }
    }
}

function Write-JsonFile([string]$PathValue, [object]$Payload, [int]$Depth = 8) {
    $parent = Split-Path -Parent $PathValue
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $Payload | ConvertTo-Json -Depth $Depth | Set-Content -LiteralPath $PathValue -Encoding UTF8
}

function Write-TextFile([string]$PathValue, [string]$Text) {
    $parent = Split-Path -Parent $PathValue
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $Text | Set-Content -LiteralPath $PathValue -Encoding UTF8
}

Assert-NoSensitiveText "OutputDir" @($OutputDir)
$resolvedOutputDir = Resolve-RepoPath $OutputDir
$generatedAt = (Get-Date).ToUniversalTime().ToString("o")
$registerDatabaseHealthScript = Join-Path $PSScriptRoot "register-database-health-task.ps1"
$registerLargeFileGovernanceScript = Join-Path $PSScriptRoot "register-large-file-governance-task.ps1"
$registerPgsqlReleaseScript = Join-Path $PSScriptRoot "register-pgsql-release-acceptance-task.ps1"
$historyScript = Join-Path $PSScriptRoot "write-automation-task-history.ps1"
$ackScript = Join-Path $PSScriptRoot "write-automation-task-ack.ps1"
foreach ($scriptPath in @($registerDatabaseHealthScript, $registerLargeFileGovernanceScript, $registerPgsqlReleaseScript, $historyScript, $ackScript)) {
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Required automation script not found: $scriptPath"
    }
}

if ($PlanOnly.IsPresent) {
    $summary = [ordered]@{
        format = "qtnetworkchat-automation-task-bootstrap-plan-v1"
        outputDir = Convert-ToRepoRelativePath $resolvedOutputDir
        tasks = @("database-health", "large-file-governance", "pgsql-release-acceptance")
        ackExpiryHours = $AckExpiryHours
        historyRetentionCount = $HistoryRetentionCount
        readOnly = $true
    }
    $summary | ConvertTo-Json -Depth 6
    return
}

New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$dbOutputDir = Join-Path $resolvedOutputDir "database-health"
$dbTaskDir = Join-Path $dbOutputDir "database-health-task"
$govOutputDir = Join-Path $resolvedOutputDir "large-file-governance"
$govTaskDir = Join-Path $govOutputDir "scheduled-task"
$pgsqlOutputDir = Join-Path $resolvedOutputDir "pgsql-release-acceptance"
$pgsqlTaskDir = Join-Path $pgsqlOutputDir "pgsql-release-acceptance-task"

& powershell -ExecutionPolicy Bypass -File $registerDatabaseHealthScript `
    -OutputDir $dbOutputDir `
    -TaskDir $dbTaskDir `
    -Driver postgres `
    -PlanOnly `
    -WriteMarkdown `
    -WriteDashboard
if ($LASTEXITCODE -ne 0) { throw "database health task preview bootstrap failed with exit code $LASTEXITCODE" }

& powershell -ExecutionPolicy Bypass -File $registerLargeFileGovernanceScript `
    -RouteLogPath (Join-Path $govOutputDir "route-log.ndjson") `
    -QueuePath (Join-Path $govOutputDir "offline-queue") `
    -SourceInstanceId "bootstrap-instance" `
    -OutputDir $govOutputDir `
    -TaskDir $govTaskDir `
    -WriteDashboard `
    -WriteReport `
    -PackageDiagnostics `
    -NoFailOnWarning
if ($LASTEXITCODE -ne 0) { throw "large-file governance task preview bootstrap failed with exit code $LASTEXITCODE" }

& powershell -ExecutionPolicy Bypass -File $registerPgsqlReleaseScript `
    -OutputDir $pgsqlOutputDir `
    -TaskDir $pgsqlTaskDir `
    -PlanOnly `
    -SkipEvidencePackage
if ($LASTEXITCODE -ne 0) { throw "PostgreSQL release acceptance task preview bootstrap failed with exit code $LASTEXITCODE" }

$dbStatusPath = Join-Path $dbOutputDir "database-health-status.json"
$dbLastRunPath = Join-Path $dbTaskDir "last-run.log"
$dbHistoryPath = Join-Path $dbTaskDir "automation-task-history.json"
$dbHistoryMarkdownPath = Join-Path $dbTaskDir "automation-task-history.md"
$dbAckPath = Join-Path $dbTaskDir "automation-task-ack.json"
Write-JsonFile $dbStatusPath ([ordered]@{
        format = "qtnetworkchat-database-health-status-v1"
        generatedAt = $generatedAt
        status = "configured"
        ok = $true
        driver = "QPSQL"
        checkCount = 0
        failedChecks = @()
        queryMetrics = [ordered]@{
            slowQueryCount = 0
            queryFailureCount = 0
        }
        summary = [ordered]@{
            readiness = "preview-registered"
            operatorAction = "Run the generated database health launcher to refresh live health evidence."
        }
        auditSummary = [ordered]@{
            releaseGate = "database-health-preview-registered"
            auditFocus = @("preview", "last-run", "history", "ack")
        }
    })
Write-TextFile $dbLastRunPath ("{0} exitCode=0 bootstrapExitCode=0 taskKind=database-health statusPath={1} historyPath={2} ackPath={3}" -f $generatedAt, $dbStatusPath, $dbHistoryPath, $dbAckPath)

$govStatusPath = Join-Path $govOutputDir "large-file-governance-dashboard.json"
$govLastRunPath = Join-Path $govTaskDir "last-run.log"
$govHistoryPath = Join-Path $govTaskDir "automation-task-history.json"
$govHistoryMarkdownPath = Join-Path $govTaskDir "automation-task-history.md"
$govAckPath = Join-Path $govTaskDir "automation-task-ack.json"
Write-JsonFile $govStatusPath ([ordered]@{
        format = "qtnetworkchat-large-file-governance-status-v1"
        generatedAt = $generatedAt
        status = "configured"
        ok = $true
        totalWarnings = 0
        alertCount = 0
        s3CoverageActionableGapAreas = @()
        summary = [ordered]@{
            readiness = "preview-registered"
            operatorAction = "Run the generated large-file governance launcher to refresh dashboard and diagnostics evidence."
        }
    })
Write-TextFile $govLastRunPath ("{0} exitCode=0 bootstrapExitCode=0 taskKind=large-file-governance statusPath={1} historyPath={2} ackPath={3}" -f $generatedAt, $govStatusPath, $govHistoryPath, $govAckPath)

$pgsqlStatusPath = Join-Path $pgsqlOutputDir "pgsql-release-acceptance.json"
$pgsqlLastRunPath = Join-Path $pgsqlTaskDir "last-run.log"
$pgsqlHistoryPath = Join-Path $pgsqlTaskDir "automation-task-history.json"
$pgsqlHistoryMarkdownPath = Join-Path $pgsqlTaskDir "automation-task-history.md"
$pgsqlAckPath = Join-Path $pgsqlTaskDir "automation-task-ack.json"
$pgsqlEvidenceDir = Join-Path $pgsqlOutputDir "evidence"
$pgsqlEvidencePackagePath = Join-Path $pgsqlEvidenceDir "pgsql-release-evidence.zip"
$pgsqlEvidenceManifestPath = Join-Path $pgsqlEvidenceDir "pgsql-release-evidence-manifest.json"
Write-JsonFile $pgsqlStatusPath ([ordered]@{
        format = "qtnetworkchat-pgsql-release-acceptance-v1"
        generatedAt = $generatedAt
        status = "configured"
        ok = $true
        summary = [ordered]@{
            readiness = "preview-registered"
            operatorAction = "Run the generated PostgreSQL release acceptance launcher to refresh health, smoke, migration, rollback, and evidence artifacts."
        }
        reportSummary = [ordered]@{
            executionReadiness = "preview-registered"
            operatorAction = "Run the generated PostgreSQL release acceptance launcher before cutover review."
        }
        auditSummary = [ordered]@{
            releaseGate = "pgsql-release-preview-registered"
            auditFocus = @("health", "smoke", "migration", "rollback", "evidence")
            evidenceBundle = @("pgsql-release-acceptance.json", "pgsql-release-evidence.zip")
        }
    })
Write-TextFile $pgsqlLastRunPath ("{0} exitCode=0 bootstrapExitCode=0 taskKind=pgsql-release-acceptance statusPath={1} evidencePackagePath={2} historyPath={3} ackPath={4}" -f $generatedAt, $pgsqlStatusPath, $pgsqlEvidencePackagePath, $pgsqlHistoryPath, $pgsqlAckPath)
Write-JsonFile $pgsqlEvidenceManifestPath ([ordered]@{
        format = "qtnetworkchat-pgsql-release-evidence-package-v1"
        generatedAt = $generatedAt
        packageMode = "bootstrap-preview"
        redacted = $true
        artifacts = @("pgsql-release-acceptance.json", "last-run.log", "automation-task-history.json", "automation-task-ack.json")
    })
New-Item -ItemType Directory -Force -Path $pgsqlEvidenceDir | Out-Null
$evidenceReadmePath = Join-Path $pgsqlEvidenceDir "README.txt"
Write-TextFile $evidenceReadmePath "Bootstrap evidence package placeholder. Run the generated PostgreSQL release acceptance launcher for live redacted evidence."
if (Test-Path -LiteralPath $pgsqlEvidencePackagePath -PathType Leaf) {
    Remove-Item -LiteralPath $pgsqlEvidencePackagePath -Force
}
Compress-Archive -LiteralPath $pgsqlEvidenceManifestPath, $evidenceReadmePath -DestinationPath $pgsqlEvidencePackagePath -Force

foreach ($ackPath in @($dbAckPath, $govAckPath, $pgsqlAckPath)) {
    & powershell -ExecutionPolicy Bypass -File $ackScript `
        -AckPath $ackPath `
        -Clear `
        -Reason "bootstrap-default"
    if ($LASTEXITCODE -ne 0) { throw "automation ack bootstrap failed with exit code $LASTEXITCODE" }
}

foreach ($task in @(
        @{ LastRun = $dbLastRunPath; Ack = $dbAckPath; Json = $dbHistoryPath; Markdown = $dbHistoryMarkdownPath },
        @{ LastRun = $govLastRunPath; Ack = $govAckPath; Json = $govHistoryPath; Markdown = $govHistoryMarkdownPath },
        @{ LastRun = $pgsqlLastRunPath; Ack = $pgsqlAckPath; Json = $pgsqlHistoryPath; Markdown = $pgsqlHistoryMarkdownPath }
    )) {
    & powershell -ExecutionPolicy Bypass -File $historyScript `
        -LastRunPath $task.LastRun `
        -AckPath $task.Ack `
        -AckExpiryHours $AckExpiryHours `
        -RetentionCount $HistoryRetentionCount `
        -JsonPath $task.Json `
        -MarkdownPath $task.Markdown `
        -FailOnSensitive
    if ($LASTEXITCODE -ne 0) { throw "automation history bootstrap failed with exit code $LASTEXITCODE" }
}

$summaryPath = Join-Path $resolvedOutputDir "automation-task-bootstrap.json"
$summary = [ordered]@{
    format = "qtnetworkchat-automation-task-bootstrap-v1"
    generatedAt = $generatedAt
    outputDir = Convert-ToRepoRelativePath $resolvedOutputDir
    ackExpiryHours = $AckExpiryHours
    historyRetentionCount = $HistoryRetentionCount
    databaseHealth = [ordered]@{
        previewPath = Convert-ToRepoRelativePath (Join-Path $dbTaskDir "database-health-task-preview.json")
        statusPath = Convert-ToRepoRelativePath $dbStatusPath
        lastRunPath = Convert-ToRepoRelativePath $dbLastRunPath
        historyPath = Convert-ToRepoRelativePath $dbHistoryPath
        ackPath = Convert-ToRepoRelativePath $dbAckPath
    }
    largeFileGovernance = [ordered]@{
        previewPath = Convert-ToRepoRelativePath (Join-Path $govTaskDir "scheduled-task-preview.json")
        statusPath = Convert-ToRepoRelativePath $govStatusPath
        lastRunPath = Convert-ToRepoRelativePath $govLastRunPath
        historyPath = Convert-ToRepoRelativePath $govHistoryPath
        ackPath = Convert-ToRepoRelativePath $govAckPath
    }
    pgsqlReleaseAcceptance = [ordered]@{
        previewPath = Convert-ToRepoRelativePath (Join-Path $pgsqlTaskDir "pgsql-release-acceptance-task-preview.json")
        statusPath = Convert-ToRepoRelativePath $pgsqlStatusPath
        lastRunPath = Convert-ToRepoRelativePath $pgsqlLastRunPath
        historyPath = Convert-ToRepoRelativePath $pgsqlHistoryPath
        ackPath = Convert-ToRepoRelativePath $pgsqlAckPath
        evidencePackagePath = Convert-ToRepoRelativePath $pgsqlEvidencePackagePath
    }
}
Write-JsonFile $summaryPath $summary
$summary | ConvertTo-Json -Depth 8

if ($FailOnSensitive) {
    $raw = Get-Content -LiteralPath $summaryPath -Raw -Encoding UTF8
    Assert-NoSensitiveText "bootstrap summary" @($raw)
}
