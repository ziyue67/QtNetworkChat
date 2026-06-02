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
    [string]$RollbackPreviewMarkdownPath
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
if ([string]::IsNullOrWhiteSpace($jsonTarget) -and (-not [string]::IsNullOrWhiteSpace($markdownTarget) -or -not [string]::IsNullOrWhiteSpace($htmlTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget))) {
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
        $lines.Add(("- Diff severity: {0}" -f $severity))
        $lines.Add(("- Recommended action: {0}" -f $recommendedAction))
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
            "<h2>Diff Summary</h2>",
            "<p>Severity: <code>$(Escape-Html $severity)</code></p>",
            "<p>Recommended action: <code>$(Escape-Html $recommendedAction)</code></p>",
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

    if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewMarkdownTarget)) {
        $previewTables = @()
        $previewOperations = @()
        foreach ($table in $tables) {
            $keyColumns = @(Get-RollbackKeyColumns -TableName $table.name -ReportedKeyColumns @($table.keyColumns))
            $wouldDeleteRows = if ($migration.mode -eq "rollback") { $table.rolledBackRows } else { $table.rows }
            $requiresReview = ([int64]$wouldDeleteRows) -gt 0
            $whereShape = if ($keyColumns.Count -gt 0) {
                (($keyColumns | ForEach-Object { "$_ = ?" }) -join " AND ")
            } else {
                "<no key columns>"
            }
            $previewTables += [pscustomobject]@{
                name = $table.name
                sourceRows = $table.rows
                keyColumns = $keyColumns
                wouldDeleteRows = $wouldDeleteRows
                dryRun = $true
            }
            $previewOperations += [pscustomobject]@{
                table = $table.name
                operation = "delete-by-source-keys"
                keyColumns = $keyColumns
                whereShape = $whereShape
                wouldDeleteRows = $wouldDeleteRows
                requiresReview = $requiresReview
                dryRun = $true
            }
        }
        $totalSourceRows = [int64](($previewTables | Measure-Object -Property sourceRows -Sum).Sum)
        $totalWouldDeleteRows = [int64](($previewTables | Measure-Object -Property wouldDeleteRows -Sum).Sum)
        $tablesWithDeletes = @($previewTables | Where-Object { ([int64]$_.wouldDeleteRows) -gt 0 }).Count
        $riskLevel = if ($totalWouldDeleteRows -eq 0) { "none" } elseif ($Mode -eq "rollback") { "high" } else { "review" }
        $summary = [ordered]@{
            sourceMode = $migration.mode
            dryRun = $true
            tableCount = @($previewTables).Count
            totalSourceRows = $totalSourceRows
            totalWouldDeleteRows = $totalWouldDeleteRows
            tablesWithDeletes = $tablesWithDeletes
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
            $lines.Add("")
            $lines.Add("| Table | Operation | Key columns | Delete predicate shape | Would delete | Requires review |")
            $lines.Add("|---|---|---|---|---:|---|")
            foreach ($operation in $previewOperations) {
                $keyColumnText = if (@($operation.keyColumns).Count -gt 0) { (@($operation.keyColumns) -join ", ") } else { "(none)" }
                $lines.Add(("| {0} | {1} | {2} | {3} | {4} | {5} |" -f
                    $operation.table, $operation.operation, $keyColumnText, $operation.whereShape, $operation.wouldDeleteRows, $operation.requiresReview))
            }
            Set-Content -LiteralPath $rollbackPreviewMarkdownTarget -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
        }
    }
}

exit $exitCode
