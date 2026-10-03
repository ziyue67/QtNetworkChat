param(
    [string]$MigratorExe = "build-qt6-mingw\sqlite_to_postgres_migrator.exe",
    [string]$SQLitePath = "accounts.sqlite3",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [ValidateSet("plan", "execute", "validate", "diff", "rollback")]
    [string]$Mode = "plan",
    [switch]$CreateSample,
    [string]$SampleOwnerId = "910001",
    [string]$SamplePeerId = "910002",
    [string]$JsonPath,
    [string]$MarkdownPath,
    [string]$HtmlPath,
    [string]$RollbackPreviewPath,
    [string]$RollbackPreviewMarkdownPath,
    [string]$RollbackAuditPath,
    [string]$RollbackAuditMarkdownPath
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $Path
}

function Get-RollbackKeyColumns {
    param(
        [string]$TableName,
        [object[]]$ReportedKeyColumns
    )
    $reported = @($ReportedKeyColumns | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
    if ($reported.Count -gt 0) {
        return $reported
    }
    switch ($TableName) {
        "accounts" { return @("account") }
        "server_groups" { return @("group_id") }
        "server_group_members" { return @("group_id", "user_id") }
        "server_group_removed_members" { return @("group_id", "user_id") }
        default { return @("id") }
    }
}

function Format-ReportValue {
    param([object]$Value)
    if ($null -eq $Value) {
        return ""
    }
    return [string]$Value
}

function Escape-Html {
    param([object]$Value)
    return [System.Net.WebUtility]::HtmlEncode((Format-ReportValue $Value))
}

function Get-ExecutionReadiness {
    param(
        [string]$Mode,
        [string]$Severity,
        [int64]$TotalMissingRows,
        [int64]$TotalRolledBackRows,
        [int]$DriftTableCount
    )
    if ($Mode -eq "rollback") {
        return "review"
    }
    if ($Severity -eq "ok" -and $TotalMissingRows -eq 0 -and $TotalRolledBackRows -eq 0 -and $DriftTableCount -eq 0) {
        return "ready"
    }
    if ($Severity -eq "warning") {
        return "review"
    }
    "blocked"
}

function Get-ExecutionAction {
    param(
        [string]$Readiness,
        [string]$Mode
    )
    switch ($Readiness) {
        "ready" { return "Can proceed after a routine backup and smoke verification." }
        "review" {
            if ($Mode -eq "rollback") {
                return "Review delete predicates and affected tables before executing rollback against PostgreSQL."
            }
            return "Review drift, missing rows, and rollback preview before execute or cutover."
        }
        default { return "Do not execute until drift or missing-row issues are resolved and rerun validation." }
    }
}

function Get-AuditWriteIntent {
    param([string]$Mode)
    switch ($Mode) {
        "plan" { return "dry-run-plan" }
        "execute" { return "upsert-postgres" }
        "validate" { return "validate-postgres" }
        "diff" { return "diff-postgres" }
        "rollback" { return "delete-postgres" }
        default { return "unknown" }
    }
}

function Get-AuditReleaseGate {
    param(
        [string]$Readiness,
        [string]$Mode
    )
    if ($Mode -eq "rollback") {
        return "manual-rollback-review"
    }
    switch ($Readiness) {
        "ready" { return "can-cutover-after-smoke" }
        "review" { return "manual-review-required" }
        default { return "blocked" }
    }
}

$migratorPath = Resolve-RepoPath $MigratorExe
$sqliteFullPath = Resolve-RepoPath $SQLitePath
$qtBinDir = Join-Path $QtRoot "bin"
$qtPluginDir = Join-Path $QtRoot "plugins"
$qpsqlPlugin = Join-Path $qtPluginDir "sqldrivers\qsqlpsql.dll"
$libpq = Join-Path $PostgresBinDir "libpq.dll"

$checks = @(
    [pscustomobject]@{ name = "migrator"; ok = (Test-Path -LiteralPath $migratorPath -PathType Leaf); detail = $migratorPath },
    [pscustomobject]@{ name = "qt-bin"; ok = (Test-Path -LiteralPath $qtBinDir -PathType Container); detail = $qtBinDir },
    [pscustomobject]@{ name = "qt-sqlite-plugin"; ok = (Test-Path -LiteralPath (Join-Path $qtPluginDir "sqldrivers\qsqlite.dll") -PathType Leaf); detail = (Join-Path $qtPluginDir "sqldrivers\qsqlite.dll") }
)
if ($Mode -ne "plan") {
    $checks += @(
        [pscustomobject]@{ name = "qt-qpsql-plugin"; ok = (Test-Path -LiteralPath $qpsqlPlugin -PathType Leaf); detail = $qpsqlPlugin },
        [pscustomobject]@{ name = "postgres-libpq"; ok = (Test-Path -LiteralPath $libpq -PathType Leaf); detail = $libpq }
    )
}
$missing = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
if ($missing.Count -gt 0) {
    throw ("SQLite to PostgreSQL migration prerequisites missing: {0}" -f ($missing -join ", "))
}
if ($Mode -ne "plan" -and [string]::IsNullOrWhiteSpace($PostgresPassword)) {
    throw "PostgresPassword is required when -Mode $Mode"
}

$jsonTarget = if ([string]::IsNullOrWhiteSpace($JsonPath)) { "" } else { Resolve-RepoPath $JsonPath }
$markdownTarget = if ([string]::IsNullOrWhiteSpace($MarkdownPath)) { "" } else { Resolve-RepoPath $MarkdownPath }
$htmlTarget = if ([string]::IsNullOrWhiteSpace($HtmlPath)) { "" } else { Resolve-RepoPath $HtmlPath }
$rollbackPreviewTarget = if ([string]::IsNullOrWhiteSpace($RollbackPreviewPath)) { "" } else { Resolve-RepoPath $RollbackPreviewPath }
$rollbackPreviewMarkdownTarget = if ([string]::IsNullOrWhiteSpace($RollbackPreviewMarkdownPath)) { "" } else { Resolve-RepoPath $RollbackPreviewMarkdownPath }
$rollbackAuditTarget = if ([string]::IsNullOrWhiteSpace($RollbackAuditPath)) { "" } else { Resolve-RepoPath $RollbackAuditPath }
$rollbackAuditMarkdownTarget = if ([string]::IsNullOrWhiteSpace($RollbackAuditMarkdownPath)) { "" } else { Resolve-RepoPath $RollbackAuditMarkdownPath }
if ([string]::IsNullOrWhiteSpace($jsonTarget) -and (-not [string]::IsNullOrWhiteSpace($markdownTarget) -or -not [string]::IsNullOrWhiteSpace($htmlTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackAuditTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackAuditMarkdownTarget))) {
    $jsonTarget = Resolve-RepoPath "build-qt6-mingw\sqlite-pg-migration.json"
}
$args = @(
    "--mode", $Mode,
    "--sqlite", $sqliteFullPath,
    "--pg-host", $PostgresHost,
    "--pg-port", "$PostgresPort",
    "--pg-database", $PostgresDatabase,
    "--pg-user", $PostgresUser,
    "--pg-password", $PostgresPassword
)
if ($CreateSample) {
    $args += @("--create-sample", "--sample-owner-id", $SampleOwnerId, "--sample-peer-id", $SamplePeerId)
}
if (-not [string]::IsNullOrWhiteSpace($jsonTarget)) {
    $args += @("--json", $jsonTarget)
}

$oldPath = $env:PATH
$oldPluginPath = $env:QT_PLUGIN_PATH
try {
    $pathPrefix = $qtBinDir
    if ($Mode -ne "plan") {
        $pathPrefix = "$qtBinDir;$PostgresBinDir"
    }
    $env:PATH = "$pathPrefix;$oldPath"
    $env:QT_PLUGIN_PATH = $qtPluginDir
    & $migratorPath @args
    $exitCode = $LASTEXITCODE
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
}

if ($exitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($jsonTarget) -and (Test-Path -LiteralPath $jsonTarget -PathType Leaf)) {
    $migration = Get-Content -LiteralPath $jsonTarget -Raw | ConvertFrom-Json
    $tables = @($migration.tables)
    $totalRows = [int64](($tables | Measure-Object -Property rows -Sum).Sum)
    $totalMissing = [int64](($tables | Measure-Object -Property missingRows -Sum).Sum)
    $totalRolledBack = [int64](($tables | Measure-Object -Property rolledBackRows -Sum).Sum)
    $diffSummary = $migration.diffSummary
    $severity = Format-ReportValue $diffSummary.severity
    $recommendedAction = Format-ReportValue $diffSummary.recommendedAction
    $tablesRequiringReview = @($tables | Where-Object {
            ([string]$_.diffStatus) -ne "clean" -or
            ([int64]$_.missingRows) -gt 0 -or
            ([int64]$_.rolledBackRows) -gt 0
        })
    $executionReadiness = Get-ExecutionReadiness -Mode $migration.mode -Severity $severity -TotalMissingRows $totalMissing -TotalRolledBackRows $totalRolledBack -DriftTableCount ([int]$diffSummary.driftTableCount)
    $executionAction = Get-ExecutionAction -Readiness $executionReadiness -Mode $migration.mode
    $auditFocus = New-Object System.Collections.Generic.List[string]
    if ($tablesRequiringReview.Count -gt 0) { [void]$auditFocus.Add("review-drifted-tables") }
    if ($totalMissing -gt 0) { [void]$auditFocus.Add("resolve-missing-rows") }
    if ($totalRolledBack -gt 0 -or $migration.mode -eq "rollback") { [void]$auditFocus.Add("verify-rollback-scope") }
    if ($auditFocus.Count -eq 0) { [void]$auditFocus.Add("routine-backup-and-smoke") }
    $evidenceBundle = New-Object System.Collections.Generic.List[string]
    [void]$evidenceBundle.Add("json")
    if (-not [string]::IsNullOrWhiteSpace($markdownTarget)) { [void]$evidenceBundle.Add("markdown") }
    if (-not [string]::IsNullOrWhiteSpace($htmlTarget)) { [void]$evidenceBundle.Add("html") }
    if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget)) { [void]$evidenceBundle.Add("rollback-preview-json") }
    if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget)) { [void]$evidenceBundle.Add("rollback-preview-markdown") }
    if (-not [string]::IsNullOrWhiteSpace($rollbackAuditTarget)) { [void]$evidenceBundle.Add("rollback-audit-json") }
    if (-not [string]::IsNullOrWhiteSpace($rollbackAuditMarkdownTarget)) { [void]$evidenceBundle.Add("rollback-audit-markdown") }
    $tablesInScope = @($tables | Where-Object { ([int64]$_.rows) -gt 0 }).Count
    $auditSummary = [ordered]@{
        mode = $migration.mode
        writeIntent = Get-AuditWriteIntent -Mode $migration.mode
        tablesInScope = $tablesInScope
        rowsInScope = $totalRows
        reviewTableCount = $tablesRequiringReview.Count
        backupRequired = ($migration.mode -eq "execute" -or $migration.mode -eq "rollback")
        rollbackPreviewAvailable = (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget))
        releaseGate = Get-AuditReleaseGate -Readiness $executionReadiness -Mode $migration.mode
        evidenceBundle = @($evidenceBundle)
    }
    $migration | Add-Member -NotePropertyName reportSummary -NotePropertyValue ([ordered]@{
            executionReadiness = $executionReadiness
            operatorAction = $executionAction
            tablesRequiringReview = $tablesRequiringReview.Count
            auditFocus = @($auditFocus)
        }) -Force
    $migration | Add-Member -NotePropertyName auditSummary -NotePropertyValue $auditSummary -Force
    $migration | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonTarget -Encoding UTF8

    if (-not [string]::IsNullOrWhiteSpace($markdownTarget)) {
        $parent = Split-Path -Parent $markdownTarget
        if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add("# SQLite to PostgreSQL Migration Report")
        $lines.Add("")
        $lines.Add(("- Mode: {0}" -f $migration.mode))
        $lines.Add(("- Status: {0}" -f $migration.status))
        $lines.Add(("- OK: {0}" -f $migration.ok))
        $lines.Add(("- PostgreSQL password: {0}" -f $migration.postgresPassword))
        $lines.Add(("- Total source rows: {0}" -f $totalRows))
        $lines.Add(("- Missing rows: {0}" -f $totalMissing))
        $lines.Add(("- Rolled back rows: {0}" -f $totalRolledBack))
        $lines.Add(("- Execution readiness: {0}" -f $executionReadiness))
        $lines.Add(("- Tables requiring review: {0}" -f $tablesRequiringReview.Count))
        $lines.Add(("- Diff severity: {0}" -f $severity))
        $lines.Add(("- Recommended action: {0}" -f $recommendedAction))
        $lines.Add(("- Operator action: {0}" -f $executionAction))
        $lines.Add(("- Audit write intent: {0}" -f $auditSummary.writeIntent))
        $lines.Add(("- Audit release gate: {0}" -f $auditSummary.releaseGate))
        $lines.Add(("- Audit backup required: {0}" -f $auditSummary.backupRequired))
        $lines.Add(("- Audit rollback preview available: {0}" -f $auditSummary.rollbackPreviewAvailable))
        $lines.Add(("- Audit evidence bundle: {0}" -f (@($auditSummary.evidenceBundle) -join ", ")))
        $lines.Add("")
        $lines.Add("| Audit summary | Value |")
        $lines.Add("|---|---|")
        $lines.Add(("| Mode | {0} |" -f $auditSummary.mode))
        $lines.Add(("| Write intent | {0} |" -f $auditSummary.writeIntent))
        $lines.Add(("| Tables in scope | {0} |" -f $auditSummary.tablesInScope))
        $lines.Add(("| Rows in scope | {0} |" -f $auditSummary.rowsInScope))
        $lines.Add(("| Review tables | {0} |" -f $auditSummary.reviewTableCount))
        $lines.Add(("| Backup required | {0} |" -f $auditSummary.backupRequired))
        $lines.Add(("| Rollback preview available | {0} |" -f $auditSummary.rollbackPreviewAvailable))
        $lines.Add(("| Release gate | {0} |" -f $auditSummary.releaseGate))
        $lines.Add("")
        $lines.Add("| Diff summary | Value |")
        $lines.Add("|---|---:|")
        $lines.Add(("| Tables | {0} |" -f $diffSummary.tableCount))
        $lines.Add(("| Drift tables | {0} |" -f $diffSummary.driftTableCount))
        $lines.Add(("| Missing tables | {0} |" -f $diffSummary.missingTableCount))
        $lines.Add(("| Rollback tables | {0} |" -f $diffSummary.rollbackTableCount))
        $lines.Add(("| Total PostgreSQL rows | {0} |" -f $diffSummary.totalPostgresRows))
        $lines.Add(("| Total copied rows | {0} |" -f $diffSummary.totalCopiedRows))
        $lines.Add(("| Total validated rows | {0} |" -f $diffSummary.totalValidatedRows))
        $lines.Add("")
        $lines.Add("| Table | Source rows | PostgreSQL rows | Copied | Validated | Missing | Rolled back | Diff |")
        $lines.Add("|---|---:|---:|---:|---:|---:|---:|---|")
        foreach ($table in $tables) {
            $lines.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f
                $table.name, $table.rows, $table.postgresRows, $table.copiedRows, $table.validatedRows, $table.missingRows, $table.rolledBackRows, $table.diffStatus))
        }
        Set-Content -LiteralPath $markdownTarget -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
    }

    if (-not [string]::IsNullOrWhiteSpace($htmlTarget)) {
        $parent = Split-Path -Parent $htmlTarget
        if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
        $rows = foreach ($table in $tables) {
            "<tr><td>$(Escape-Html $table.name)</td><td>$(Escape-Html $table.rows)</td><td>$(Escape-Html $table.postgresRows)</td><td>$(Escape-Html $table.copiedRows)</td><td>$(Escape-Html $table.validatedRows)</td><td>$(Escape-Html $table.missingRows)</td><td>$(Escape-Html $table.rolledBackRows)</td><td>$(Escape-Html $table.diffStatus)</td></tr>"
        }
        $html = @(
            "<!doctype html>",
            "<html lang=""en""><head><meta charset=""utf-8""><title>SQLite to PostgreSQL Migration Report</title></head>",
            "<body>",
            "<h1>SQLite to PostgreSQL Migration Report</h1>",
            "<p>Mode: <code>$(Escape-Html $migration.mode)</code> Status: <code>$(Escape-Html $migration.status)</code> OK: <code>$(Escape-Html $migration.ok)</code></p>",
            "<p>PostgreSQL password: <code>$(Escape-Html $migration.postgresPassword)</code></p>",
            "<p>Total source rows: <code>$(Escape-Html $totalRows)</code> Missing rows: <code>$(Escape-Html $totalMissing)</code> Rolled back rows: <code>$(Escape-Html $totalRolledBack)</code></p>",
            "<p>Execution readiness: <code>$(Escape-Html $executionReadiness)</code> Tables requiring review: <code>$(Escape-Html $tablesRequiringReview.Count)</code></p>",
            "<h2>Diff Summary</h2>",
            "<p>Severity: <code>$(Escape-Html $severity)</code></p>",
            "<p>Recommended action: <code>$(Escape-Html $recommendedAction)</code></p>",
            "<p>Operator action: <code>$(Escape-Html $executionAction)</code></p>",
            "<h2>Audit Summary</h2>",
            "<p>Write intent: <code>$(Escape-Html $auditSummary.writeIntent)</code> Release gate: <code>$(Escape-Html $auditSummary.releaseGate)</code></p>",
            "<p>Backup required: <code>$(Escape-Html $auditSummary.backupRequired)</code> Rollback preview available: <code>$(Escape-Html $auditSummary.rollbackPreviewAvailable)</code></p>",
            "<p>Evidence bundle: <code>$(Escape-Html (@($auditSummary.evidenceBundle) -join ", "))</code></p>",
            "<table><thead><tr><th>Audit metric</th><th>Value</th></tr></thead><tbody>",
            "<tr><td>Mode</td><td>$(Escape-Html $auditSummary.mode)</td></tr>",
            "<tr><td>Write intent</td><td>$(Escape-Html $auditSummary.writeIntent)</td></tr>",
            "<tr><td>Tables in scope</td><td>$(Escape-Html $auditSummary.tablesInScope)</td></tr>",
            "<tr><td>Rows in scope</td><td>$(Escape-Html $auditSummary.rowsInScope)</td></tr>",
            "<tr><td>Review tables</td><td>$(Escape-Html $auditSummary.reviewTableCount)</td></tr>",
            "<tr><td>Backup required</td><td>$(Escape-Html $auditSummary.backupRequired)</td></tr>",
            "<tr><td>Rollback preview available</td><td>$(Escape-Html $auditSummary.rollbackPreviewAvailable)</td></tr>",
            "<tr><td>Release gate</td><td>$(Escape-Html $auditSummary.releaseGate)</td></tr>",
            "</tbody></table>",
            "<table><thead><tr><th>Metric</th><th>Value</th></tr></thead><tbody>",
            "<tr><td>Tables</td><td>$(Escape-Html $diffSummary.tableCount)</td></tr>",
            "<tr><td>Drift tables</td><td>$(Escape-Html $diffSummary.driftTableCount)</td></tr>",
            "<tr><td>Missing tables</td><td>$(Escape-Html $diffSummary.missingTableCount)</td></tr>",
            "<tr><td>Rollback tables</td><td>$(Escape-Html $diffSummary.rollbackTableCount)</td></tr>",
            "<tr><td>Total PostgreSQL rows</td><td>$(Escape-Html $diffSummary.totalPostgresRows)</td></tr>",
            "<tr><td>Total copied rows</td><td>$(Escape-Html $diffSummary.totalCopiedRows)</td></tr>",
            "<tr><td>Total validated rows</td><td>$(Escape-Html $diffSummary.totalValidatedRows)</td></tr>",
            "</tbody></table>",
            "<table><thead><tr><th>Table</th><th>Source rows</th><th>PostgreSQL rows</th><th>Copied</th><th>Validated</th><th>Missing</th><th>Rolled back</th><th>Diff</th></tr></thead><tbody>",
            ($rows -join [Environment]::NewLine),
            "</tbody></table>",
            "</body></html>"
        ) -join [Environment]::NewLine
        Set-Content -LiteralPath $htmlTarget -Value $html -Encoding UTF8
    }

    $rollbackPreviewForAudit = $null
    if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackAuditTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackAuditMarkdownTarget)) {
        $previewTables = @()
        $previewOperations = @()
        $tablesUsingFallbackKeys = 0
        foreach ($table in $tables) {
            $reportedKeyColumns = @($table.keyColumns)
            $keyColumns = @(Get-RollbackKeyColumns -TableName $table.name -ReportedKeyColumns $reportedKeyColumns)
            $keySource = if (@($reportedKeyColumns | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }).Count -gt 0) { "reported" } else { "fallback" }
            $wouldDeleteRows = if ($migration.mode -eq "rollback") { $table.rolledBackRows } else { $table.rows }
            $requiresReview = ([int64]$wouldDeleteRows) -gt 0
            if ($keySource -eq "fallback") {
                $tablesUsingFallbackKeys += 1
            }
            $whereShape = if ($keyColumns.Count -gt 0) {
                (($keyColumns | ForEach-Object { "$_ = ?" }) -join " AND ")
            } else {
                "<no key columns>"
            }
            $reviewReason = if (-not $requiresReview) {
                "none"
            } elseif ($keySource -eq "fallback") {
                "fallback-key-columns"
            } else {
                "delete-preview"
            }
            $previewTables += [pscustomobject]@{
                name = $table.name
                sourceRows = $table.rows
                keyColumns = $keyColumns
                keySource = $keySource
                wouldDeleteRows = $wouldDeleteRows
                dryRun = $true
            }
            $previewOperations += [pscustomobject]@{
                table = $table.name
                operation = "delete-by-source-keys"
                keyColumns = $keyColumns
                keySource = $keySource
                whereShape = $whereShape
                wouldDeleteRows = $wouldDeleteRows
                requiresReview = $requiresReview
                reviewReason = $reviewReason
                dryRun = $true
            }
        }
        $totalSourceRows = [int64](($previewTables | Measure-Object -Property sourceRows -Sum).Sum)
        $totalWouldDeleteRows = [int64](($previewTables | Measure-Object -Property wouldDeleteRows -Sum).Sum)
        $tablesWithDeletes = @($previewTables | Where-Object { ([int64]$_.wouldDeleteRows) -gt 0 }).Count
        $fallbackReviewTables = @($previewOperations | Where-Object { $_.reviewReason -eq "fallback-key-columns" }).Count
        $deletePreviewTables = @($previewOperations | Where-Object { $_.reviewReason -eq "delete-preview" }).Count
        $reviewReasons = New-Object System.Collections.Generic.List[string]
        if ($fallbackReviewTables -gt 0) { [void]$reviewReasons.Add("fallback-key-columns") }
        if ($deletePreviewTables -gt 0) { [void]$reviewReasons.Add("delete-preview") }
        if ($reviewReasons.Count -eq 0) { [void]$reviewReasons.Add("none") }
        $riskLevel = if ($totalWouldDeleteRows -eq 0) { "none" } elseif ($Mode -eq "rollback") { "high" } else { "review" }
        $previewOperatorAction = if ($riskLevel -eq "none") { "No rollback rows would be touched." } elseif ($tablesUsingFallbackKeys -gt 0) { "Review fallback key columns before executing rollback." } else { "Review affected tables and delete predicates before executing rollback." }
        $summary = [ordered]@{
            sourceMode = $migration.mode
            dryRun = $true
            tableCount = @($previewTables).Count
            totalSourceRows = $totalSourceRows
            totalWouldDeleteRows = $totalWouldDeleteRows
            tablesWithDeletes = $tablesWithDeletes
            reviewRequiredTableCount = $tablesWithDeletes
            fallbackKeyTableCount = $tablesUsingFallbackKeys
            fallbackKeyReviewTableCount = $fallbackReviewTables
            deletePreviewTableCount = $deletePreviewTables
            operatorAction = $previewOperatorAction
            reviewReasons = @($reviewReasons)
        }
        $preview = [ordered]@{
            format = "qtnetworkchat-sqlite-pg-rollback-preview-v1"
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            sourceMode = $migration.mode
            dryRun = $true
            ok = $true
            riskLevel = $riskLevel
            summary = $summary
            postgresPassword = "<redacted>"
            operations = @($previewOperations)
            tables = @($previewTables)
        }
        $rollbackPreviewForAudit = $preview

        if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget)) {
            $parent = Split-Path -Parent $rollbackPreviewTarget
            if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            $preview | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $rollbackPreviewTarget -Encoding UTF8
        }

        if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget)) {
            $parent = Split-Path -Parent $rollbackPreviewMarkdownTarget
            if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            $lines = [System.Collections.Generic.List[string]]::new()
            $lines.Add("# SQLite to PostgreSQL Rollback Dry-run Preview")
            $lines.Add("")
            $lines.Add(("- Source mode: {0}" -f $migration.mode))
            $lines.Add(("- Dry-run: {0}" -f $true))
            $lines.Add(("- Risk level: {0}" -f $riskLevel))
            $lines.Add(("- PostgreSQL password: {0}" -f $preview.postgresPassword))
            $lines.Add(("- Tables: {0}" -f $summary.tableCount))
            $lines.Add(("- Total source rows: {0}" -f $summary.totalSourceRows))
            $lines.Add(("- Total rows that would be deleted: {0}" -f $summary.totalWouldDeleteRows))
            $lines.Add(("- Tables with deletes: {0}" -f $summary.tablesWithDeletes))
            $lines.Add(("- Fallback-key tables: {0}" -f $summary.fallbackKeyTableCount))
            $lines.Add(("- Fallback-key review tables: {0}" -f $summary.fallbackKeyReviewTableCount))
            $lines.Add(("- Delete-preview tables: {0}" -f $summary.deletePreviewTableCount))
            $lines.Add(("- Review reasons: {0}" -f (@($summary.reviewReasons) -join ", ")))
            $lines.Add(("- Operator action: {0}" -f $summary.operatorAction))
            $lines.Add("")
            $lines.Add("| Table | Operation | Key columns | Key source | Delete predicate shape | Would delete | Requires review | Review reason |")
            $lines.Add("|---|---|---|---|---|---:|---|---|")
            foreach ($operation in $previewOperations) {
                $keyColumnText = if (@($operation.keyColumns).Count -gt 0) { (@($operation.keyColumns) -join ", ") } else { "(none)" }
                $lines.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f
                    $operation.table, $operation.operation, $keyColumnText, $operation.keySource, $operation.whereShape, $operation.wouldDeleteRows, $operation.requiresReview, $operation.reviewReason))
            }
            Set-Content -LiteralPath $rollbackPreviewMarkdownTarget -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($rollbackAuditTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackAuditMarkdownTarget)) {
        if ($null -eq $rollbackPreviewForAudit) {
            throw "Rollback audit requires rollback preview data."
        }
        $rollbackTables = @($tables | Where-Object { ([int64]$_.rolledBackRows) -gt 0 })
        $actualRolledBackRows = [int64](($rollbackTables | Measure-Object -Property rolledBackRows -Sum).Sum)
        $executionApplied = $migration.mode -eq "rollback"
        $releaseGate = if (-not $executionApplied) {
            "await-rollback-execute"
        } elseif ($actualRolledBackRows -eq [int64]($rollbackPreviewForAudit.summary.totalWouldDeleteRows)) {
            "rollback-executed-review"
        } else {
            "rollback-count-mismatch-review"
        }
        $auditFocusValues = New-Object System.Collections.Generic.List[string]
        [void]$auditFocusValues.Add("rollback-before-preview")
        if ($executionApplied) {
            [void]$auditFocusValues.Add("rollback-after-execute")
        } else {
            [void]$auditFocusValues.Add("rollback-not-executed")
        }
        if ([int64]($rollbackPreviewForAudit.summary.fallbackKeyReviewTableCount) -gt 0) {
            [void]$auditFocusValues.Add("fallback-key-review")
        }
        $rollbackAudit = [ordered]@{
            format = "qtnetworkchat-sqlite-pg-rollback-audit-v1"
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            ok = $true
            executionApplied = $executionApplied
            sourceMode = $migration.mode
            postgresPassword = "<redacted>"
            before = [ordered]@{
                dryRun = [bool]$rollbackPreviewForAudit.dryRun
                riskLevel = [string]$rollbackPreviewForAudit.riskLevel
                totalWouldDeleteRows = [int64]$rollbackPreviewForAudit.summary.totalWouldDeleteRows
                tablesWithDeletes = [int]$rollbackPreviewForAudit.summary.tablesWithDeletes
                fallbackKeyReviewTableCount = [int]$rollbackPreviewForAudit.summary.fallbackKeyReviewTableCount
                reviewReasons = @($rollbackPreviewForAudit.summary.reviewReasons)
                operatorAction = [string]$rollbackPreviewForAudit.summary.operatorAction
            }
            after = [ordered]@{
                mode = $migration.mode
                actualRolledBackRows = $actualRolledBackRows
                affectedTableCount = @($rollbackTables).Count
                diffSeverity = $severity
                executionReadiness = $executionReadiness
                operatorAction = if ($executionApplied) { "Archive rollback audit and verify PostgreSQL smoke before closing rollback." } else { "Run rollback mode only after reviewing preview predicates and taking a backup." }
            }
            auditSummary = [ordered]@{
                releaseGate = $releaseGate
                backupRequired = $true
                beforeAfterComplete = $executionApplied
                countMatchesPreview = ($actualRolledBackRows -eq [int64]$rollbackPreviewForAudit.summary.totalWouldDeleteRows)
                auditFocus = @($auditFocusValues)
                evidenceBundle = @("rollback-audit-json", "rollback-preview-json")
            }
        }

        if (-not [string]::IsNullOrWhiteSpace($rollbackAuditTarget)) {
            $parent = Split-Path -Parent $rollbackAuditTarget
            if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            $rollbackAudit | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $rollbackAuditTarget -Encoding UTF8
        }

        if (-not [string]::IsNullOrWhiteSpace($rollbackAuditMarkdownTarget)) {
            $parent = Split-Path -Parent $rollbackAuditMarkdownTarget
            if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            $lines = [System.Collections.Generic.List[string]]::new()
            $lines.Add("# SQLite to PostgreSQL Rollback Audit")
            $lines.Add("")
            $lines.Add(("- Source mode: {0}" -f $rollbackAudit.sourceMode))
            $lines.Add(("- Execution applied: {0}" -f $rollbackAudit.executionApplied))
            $lines.Add(("- PostgreSQL password: {0}" -f $rollbackAudit.postgresPassword))
            $lines.Add(("- Before risk level: {0}" -f $rollbackAudit.before.riskLevel))
            $lines.Add(("- Before would delete rows: {0}" -f $rollbackAudit.before.totalWouldDeleteRows))
            $lines.Add(("- Before fallback-key review tables: {0}" -f $rollbackAudit.before.fallbackKeyReviewTableCount))
            $lines.Add(("- After actual rolled back rows: {0}" -f $rollbackAudit.after.actualRolledBackRows))
            $lines.Add(("- After affected tables: {0}" -f $rollbackAudit.after.affectedTableCount))
            $lines.Add(("- Release gate: {0}" -f $rollbackAudit.auditSummary.releaseGate))
            $lines.Add(("- Count matches preview: {0}" -f $rollbackAudit.auditSummary.countMatchesPreview))
            $lines.Add(("- Audit focus: {0}" -f (@($rollbackAudit.auditSummary.auditFocus) -join ", ")))
            $lines.Add("")
            $lines.Add("| Phase | Metric | Value |")
            $lines.Add("|---|---|---|")
            $lines.Add(("| before | riskLevel | {0} |" -f $rollbackAudit.before.riskLevel))
            $lines.Add(("| before | totalWouldDeleteRows | {0} |" -f $rollbackAudit.before.totalWouldDeleteRows))
            $lines.Add(("| before | fallbackKeyReviewTableCount | {0} |" -f $rollbackAudit.before.fallbackKeyReviewTableCount))
            $lines.Add(("| after | actualRolledBackRows | {0} |" -f $rollbackAudit.after.actualRolledBackRows))
            $lines.Add(("| after | affectedTableCount | {0} |" -f $rollbackAudit.after.affectedTableCount))
            $lines.Add(("| audit | releaseGate | {0} |" -f $rollbackAudit.auditSummary.releaseGate))
            Set-Content -LiteralPath $rollbackAuditMarkdownTarget -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
        }
    }
}

exit $exitCode
