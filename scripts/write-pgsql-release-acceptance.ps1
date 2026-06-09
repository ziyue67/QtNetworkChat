[CmdletBinding()]
param(
    [string]$DatabaseHealthDashboardPath,
    [string]$SmokeJsonPath,
    [string]$MigrationJsonPath,
    [string]$RollbackPreviewPath,
    [string]$RollbackAuditPath,
    [string]$JsonPath,
    [string]$MarkdownPath,
    [switch]$FailOnUnhealthy
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'Authorization',
    'Credential',
    'Signature'
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Read-OptionalJson([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    $raw | ConvertFrom-Json
}

function Get-JsonValue([object]$ObjectValue, [string]$Name, [object]$DefaultValue = $null) {
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    if ($ObjectValue.PSObject.Properties.Name -contains $Name) {
        return $ObjectValue.$Name
    }
    $DefaultValue
}

function Normalize-Bool([object]$Value, [bool]$DefaultValue = $false) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    if ($text -in @("true", "1", "yes", "on")) { return $true }
    if ($text -in @("false", "0", "no", "off")) { return $false }
    $DefaultValue
}

function Format-Value([object]$Value) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return "n/a"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    [string]$Value
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

$resolvedDashboardPath = Resolve-OptionalPath $DatabaseHealthDashboardPath
$resolvedSmokePath = Resolve-OptionalPath $SmokeJsonPath
$resolvedMigrationPath = Resolve-OptionalPath $MigrationJsonPath
$resolvedRollbackPreviewPath = Resolve-OptionalPath $RollbackPreviewPath
$resolvedRollbackAuditPath = Resolve-OptionalPath $RollbackAuditPath
$resolvedJsonPath = Resolve-OptionalPath $JsonPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath

$dashboard = Read-OptionalJson $resolvedDashboardPath
$smoke = Read-OptionalJson $resolvedSmokePath
$migration = Read-OptionalJson $resolvedMigrationPath
$rollbackPreview = Read-OptionalJson $resolvedRollbackPreviewPath
$rollbackAudit = Read-OptionalJson $resolvedRollbackAuditPath

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in @($resolvedDashboardPath, $resolvedSmokePath, $resolvedMigrationPath, $resolvedRollbackPreviewPath, $resolvedRollbackAuditPath)) {
    Add-SensitiveHits $path $sensitiveHits
}

$warnings = New-Object System.Collections.ArrayList
if ($null -eq $dashboard) { [void]$warnings.Add("database-health-dashboard-missing") }
if ($null -eq $smoke) { [void]$warnings.Add("pgsql-smoke-evidence-missing") }
if ($null -eq $migration) { [void]$warnings.Add("migration-report-missing") }
if ($null -eq $rollbackPreview) { [void]$warnings.Add("rollback-preview-missing") }
if ($null -eq $rollbackAudit) { [void]$warnings.Add("rollback-audit-missing") }
if ($sensitiveHits.Count -gt 0) { [void]$warnings.Add("sensitive-fields-detected") }

$dashboardOk = Normalize-Bool (Get-JsonValue $dashboard "ok" $false)
$dashboardStatus = [string](Get-JsonValue $dashboard "status" "unknown")
$dashboardSummary = Get-JsonValue $dashboard "summary" $null
$dashboardAudit = Get-JsonValue $dashboard "auditSummary" $null
$dashboardQueryMetrics = Get-JsonValue $dashboard "queryMetrics" $null
$dashboardSlowQueryCount = [int](Get-JsonValue $dashboardQueryMetrics "slowQueryCount" 0)
$dashboardQueryFailureCount = [int](Get-JsonValue $dashboardQueryMetrics "queryFailureCount" 0)
$dashboardPlanOnly = Normalize-Bool (Get-JsonValue $dashboard "planOnly" $false)

$smokeOk = Normalize-Bool (Get-JsonValue $smoke "ok" $false)
$smokePlanOnly = Normalize-Bool (Get-JsonValue $smoke "planOnly" $false)
$smokeSummary = Get-JsonValue $smoke "summary" $null
$smokeAudit = Get-JsonValue $smoke "auditSummary" $null
$smokeRecovery = Get-JsonValue $smoke "recoverySummary" $null

$migrationOk = Normalize-Bool (Get-JsonValue $migration "ok" $false)
$migrationReport = Get-JsonValue $migration "reportSummary" $null
$migrationAudit = Get-JsonValue $migration "auditSummary" $null
$migrationDiffSummary = Get-JsonValue $migration "diffSummary" $null

$rollbackSummary = Get-JsonValue $rollbackPreview "summary" $null
$rollbackAuditSummary = Get-JsonValue $rollbackAudit "auditSummary" $null
$rollbackAuditBefore = Get-JsonValue $rollbackAudit "before" $null
$rollbackAuditAfter = Get-JsonValue $rollbackAudit "after" $null

$readinessSignals = @(
    [string](Get-JsonValue $dashboardSummary "readiness" "unknown"),
    [string](Get-JsonValue $smokeSummary "readiness" "unknown"),
    [string](Get-JsonValue $migrationReport "executionReadiness" "unknown")
)

$releaseGateSignals = @(
    [string](Get-JsonValue $dashboardAudit "releaseGate" "unknown"),
    [string](Get-JsonValue $smokeAudit "releaseGate" "unknown"),
    [string](Get-JsonValue $migrationAudit "releaseGate" "unknown")
)
$planOnlyEvidence = [bool]($dashboardPlanOnly -or $smokePlanOnly -or (($releaseGateSignals | Where-Object { $_ -match '^await-(live|real)' }).Count -gt 0))
$missingCoreEvidence = [bool]($null -eq $migration -or $null -eq $rollbackPreview -or $null -eq $rollbackAudit)

$acceptanceStatus = if ($sensitiveHits.Count -gt 0) {
    "blocked"
} elseif ($dashboardQueryFailureCount -gt 0) {
    "review"
} elseif ($dashboardSlowQueryCount -gt 0) {
    "review"
} elseif (-not $dashboardOk -or -not $smokeOk -or -not $migrationOk) {
    "review"
} elseif ((Get-JsonValue $rollbackPreview "riskLevel" "unknown") -ne "review") {
    "ready"
} else {
    "review"
}

$acceptanceOk = $acceptanceStatus -eq "ready"
$operatorAction = if ($sensitiveHits.Count -gt 0) {
    "Remove sensitive fields from PostgreSQL acceptance artifacts before using them for release review."
} elseif (-not $dashboardOk) {
    "Resolve database health warnings before promoting PostgreSQL release acceptance."
} elseif ($dashboardQueryFailureCount -gt 0) {
    "Investigate PostgreSQL query failures before promoting PostgreSQL release acceptance."
} elseif ($dashboardSlowQueryCount -gt 0) {
    "Review PostgreSQL slow queries and pool thresholds before promoting PostgreSQL release acceptance."
} elseif (-not $smokeOk) {
    "Run or repair real PostgreSQL smoke evidence before promoting PostgreSQL release acceptance."
} elseif ($planOnlyEvidence -or $missingCoreEvidence) {
    "Run with live PostgreSQL credentials, a valid SQLite source snapshot, and without PlanOnly to generate health, smoke, migration, rollback, and evidence artifacts."
} elseif (-not $migrationOk) {
    "Repair migration report generation before promoting PostgreSQL release acceptance."
} elseif ((Get-JsonValue $rollbackPreview "riskLevel" "unknown") -eq "review") {
    "Review rollback preview risk and fallback-key delete predicates before cutover."
} elseif ((Get-JsonValue $rollbackAuditSummary "releaseGate" "") -eq "rollback-count-mismatch-review") {
    "Review rollback audit mismatch before closing rollback evidence."
} else {
    "Archive PostgreSQL health, smoke, migration, and rollback evidence for cutover review."
}

$releaseDetails = New-Object System.Collections.Generic.List[string]
$bootstrapRequired = Get-JsonValue $smokeAudit "bootstrapRequired" $null
if ($null -ne $bootstrapRequired) {
    $releaseDetails.Add('bootstrapRequired=' + (Format-Value $bootstrapRequired))
}
$releaseHint = [string](Get-JsonValue $smokeRecovery "releaseHint" "")
if (-not [string]::IsNullOrWhiteSpace($releaseHint)) {
    $releaseDetails.Add('smokeReleaseHint=' + $releaseHint)
}
$writeIntent = [string](Get-JsonValue $migrationAudit "writeIntent" "")
if (-not [string]::IsNullOrWhiteSpace($writeIntent)) {
    $releaseDetails.Add('writeIntent=' + $writeIntent)
}
$backupRequired = Get-JsonValue $migrationAudit "backupRequired" $null
if ($null -ne $backupRequired) {
    $releaseDetails.Add('backupRequired=' + (Format-Value $backupRequired))
}
$rollbackPreviewAvailable = Get-JsonValue $migrationAudit "rollbackPreviewAvailable" $null
if ($null -ne $rollbackPreviewAvailable) {
    $releaseDetails.Add('rollbackPreview=' + (Format-Value $rollbackPreviewAvailable))
}
$rollbackRisk = [string](Get-JsonValue $rollbackPreview "riskLevel" "")
if (-not [string]::IsNullOrWhiteSpace($rollbackRisk)) {
    $releaseDetails.Add('rollbackRisk=' + $rollbackRisk)
}
$rollbackAuditGate = [string](Get-JsonValue $rollbackAuditSummary "releaseGate" "")
if (-not [string]::IsNullOrWhiteSpace($rollbackAuditGate)) {
    $releaseDetails.Add('rollbackAuditGate=' + $rollbackAuditGate)
}
$rollbackAuditApplied = Get-JsonValue $rollbackAudit "executionApplied" $null
if ($null -ne $rollbackAuditApplied) {
    $releaseDetails.Add('rollbackExecuted=' + (Format-Value $rollbackAuditApplied))
}
if ($dashboardSlowQueryCount -gt 0) {
    $releaseDetails.Add('slowQueries=' + $dashboardSlowQueryCount)
}
if ($dashboardQueryFailureCount -gt 0) {
    $releaseDetails.Add('queryFailures=' + $dashboardQueryFailureCount)
}

$evidenceBundle = New-Object System.Collections.Generic.List[string]
foreach ($item in @((Get-JsonValue $dashboardAudit "evidenceBundle" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $evidenceBundle -notcontains [string]$item) { [void]$evidenceBundle.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $smokeAudit "evidenceBundle" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $evidenceBundle -notcontains [string]$item) { [void]$evidenceBundle.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $migrationAudit "evidenceBundle" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $evidenceBundle -notcontains [string]$item) { [void]$evidenceBundle.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $rollbackAuditSummary "evidenceBundle" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $evidenceBundle -notcontains [string]$item) { [void]$evidenceBundle.Add([string]$item) }
}
if ($evidenceBundle.Count -gt 0) {
    $releaseDetails.Add('evidence=' + ($evidenceBundle -join ", "))
}

$auditFocus = New-Object System.Collections.Generic.List[string]
foreach ($item in @((Get-JsonValue $dashboardAudit "auditFocus" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $auditFocus -notcontains [string]$item) { [void]$auditFocus.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $smokeAudit "auditFocus" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $auditFocus -notcontains [string]$item) { [void]$auditFocus.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $migrationAudit "auditFocus" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $auditFocus -notcontains [string]$item) { [void]$auditFocus.Add([string]$item) }
}
foreach ($item in @((Get-JsonValue $rollbackAuditSummary "auditFocus" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$item) -and $auditFocus -notcontains [string]$item) { [void]$auditFocus.Add([string]$item) }
}

$summary = [ordered]@{
    format = "qtnetworkchat-pgsql-release-acceptance-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = $planOnlyEvidence
    status = $acceptanceStatus
    ok = $acceptanceOk
    summary = [ordered]@{
        readiness = $acceptanceStatus
        operatorAction = $operatorAction
        readinessSignals = $readinessSignals
        releaseGateSignals = $releaseGateSignals
    }
    auditSummary = [ordered]@{
        releaseGate = if ($acceptanceOk) { "can-review-cutover" } elseif ($dashboardQueryFailureCount -gt 0) { "review-query-failures" } elseif ($dashboardSlowQueryCount -gt 0) { "review-slow-queries" } elseif ($planOnlyEvidence -or $missingCoreEvidence) { "await-live-pgsql-release-acceptance" } elseif ($acceptanceStatus -eq "review") { "review-pgsql-evidence" } else { "blocked" }
        auditFocus = @($auditFocus)
        evidenceBundle = @($evidenceBundle)
    }
    releaseDetails = @($releaseDetails)
    metrics = [ordered]@{
        slowQueryCount = $dashboardSlowQueryCount
        queryFailureCount = $dashboardQueryFailureCount
        smokeRetryOrResumeCount = [int](Get-JsonValue $smokeRecovery "retryOrResumeCount" 0)
        smokeCleanupProofCount = [int](Get-JsonValue $smokeRecovery "cleanupProofCount" 0)
        migrationDriftTableCount = [int](Get-JsonValue $migrationDiffSummary "driftTableCount" 0)
        rollbackPreviewDeleteTables = [int](Get-JsonValue $rollbackSummary "tablesWithDeletes" 0)
        rollbackPreviewFallbackReviewTables = [int](Get-JsonValue $rollbackSummary "fallbackKeyReviewTableCount" 0)
        rollbackAuditBeforeRows = [int](Get-JsonValue $rollbackAuditBefore "totalWouldDeleteRows" 0)
        rollbackAuditAfterRows = [int](Get-JsonValue $rollbackAuditAfter "actualRolledBackRows" 0)
        rollbackAuditCountMatchesPreview = Normalize-Bool (Get-JsonValue $rollbackAuditSummary "countMatchesPreview" $false)
    }
    inputs = [ordered]@{
        databaseHealthDashboardPath = $resolvedDashboardPath
        smokeJsonPath = $resolvedSmokePath
        migrationJsonPath = $resolvedMigrationPath
        rollbackPreviewPath = $resolvedRollbackPreviewPath
        rollbackAuditPath = $resolvedRollbackAuditPath
    }
    warnings = @($warnings)
    sensitiveHits = @($sensitiveHits.ToArray())
}

if (-not [string]::IsNullOrWhiteSpace($resolvedJsonPath)) {
    $parent = Split-Path -Parent $resolvedJsonPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedJsonPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    $parent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat PostgreSQL Release Acceptance")
    $lines.Add("")
    $lines.Add(('- Status: `{0}`' -f (Format-Value $summary.status)))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $summary.ok)))
    $lines.Add(('- Plan only: `{0}`' -f (Format-Value $summary.planOnly)))
    $lines.Add(('- Readiness: `{0}`' -f (Format-Value $summary.summary.readiness)))
    $lines.Add(('- Operator action: `{0}`' -f (Format-Value $summary.summary.operatorAction)))
    $lines.Add(('- Release gate: `{0}`' -f (Format-Value $summary.auditSummary.releaseGate)))
    $lines.Add(('- Readiness signals: `{0}`' -f (@($summary.summary.readinessSignals) -join ", ")))
    $lines.Add(('- Release-gate signals: `{0}`' -f (@($summary.summary.releaseGateSignals) -join ", ")))
    $lines.Add(('- Release details: `{0}`' -f (@($summary.releaseDetails) -join "; ")))
    $lines.Add(('- Audit focus: `{0}`' -f (@($summary.auditSummary.auditFocus) -join ", ")))
    $lines.Add(('- Evidence bundle: `{0}`' -f (@($summary.auditSummary.evidenceBundle) -join ", ")))
    $lines.Add("")
    $lines.Add("## Metrics")
    $lines.Add("")
    $lines.Add(("| Metric | Value |"))
    $lines.Add(("| --- | --- |"))
    $lines.Add(("| slowQueryCount | {0} |" -f $summary.metrics.slowQueryCount))
    $lines.Add(("| queryFailureCount | {0} |" -f $summary.metrics.queryFailureCount))
    $lines.Add(("| smokeRetryOrResumeCount | {0} |" -f $summary.metrics.smokeRetryOrResumeCount))
    $lines.Add(("| smokeCleanupProofCount | {0} |" -f $summary.metrics.smokeCleanupProofCount))
    $lines.Add(("| migrationDriftTableCount | {0} |" -f $summary.metrics.migrationDriftTableCount))
    $lines.Add(("| rollbackPreviewDeleteTables | {0} |" -f $summary.metrics.rollbackPreviewDeleteTables))
    $lines.Add(("| rollbackPreviewFallbackReviewTables | {0} |" -f $summary.metrics.rollbackPreviewFallbackReviewTables))
    $lines.Add(("| rollbackAuditBeforeRows | {0} |" -f $summary.metrics.rollbackAuditBeforeRows))
    $lines.Add(("| rollbackAuditAfterRows | {0} |" -f $summary.metrics.rollbackAuditAfterRows))
    $lines.Add(("| rollbackAuditCountMatchesPreview | {0} |" -f (Format-Value $summary.metrics.rollbackAuditCountMatchesPreview)))
    $lines.Add("")
    $lines.Add("This release acceptance summary is generated from local redacted PostgreSQL artifacts only. It does not connect to PostgreSQL, Redis, S3, or MinIO, and it does not modify application data.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "pgsql release acceptance"
Write-Host ("  status: {0}" -f $summary.status)
Write-Host ("  ok: {0}" -f (Format-Value $summary.ok))
Write-Host ("  release gate: {0}" -f $summary.auditSummary.releaseGate)
Write-Host ("  evidence bundle: {0}" -f (@($summary.auditSummary.evidenceBundle) -join ", "))

if ($FailOnUnhealthy -and -not $summary.ok) {
    exit 2
}
