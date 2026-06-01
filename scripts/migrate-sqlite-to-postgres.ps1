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
    [string]$RollbackPreviewPath
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $Path
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
if ([string]::IsNullOrWhiteSpace($jsonTarget) -and (-not [string]::IsNullOrWhiteSpace($markdownTarget) -or -not [string]::IsNullOrWhiteSpace($htmlTarget) -or -not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget))) {
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
            "<tr><td>$($table.name)</td><td>$($table.rows)</td><td>$($table.postgresRows)</td><td>$($table.copiedRows)</td><td>$($table.validatedRows)</td><td>$($table.missingRows)</td><td>$($table.rolledBackRows)</td><td>$($table.diffStatus)</td></tr>"
        }
        $html = @(
            "<!doctype html>",
            "<html lang=""en""><head><meta charset=""utf-8""><title>SQLite to PostgreSQL Migration Report</title></head>",
            "<body>",
            "<h1>SQLite to PostgreSQL Migration Report</h1>",
            "<p>Mode: <code>$($migration.mode)</code> Status: <code>$($migration.status)</code> OK: <code>$($migration.ok)</code></p>",
            "<p>PostgreSQL password: <code>$($migration.postgresPassword)</code></p>",
            "<p>Total source rows: <code>$totalRows</code> Missing rows: <code>$totalMissing</code> Rolled back rows: <code>$totalRolledBack</code></p>",
            "<table><thead><tr><th>Table</th><th>Source rows</th><th>PostgreSQL rows</th><th>Copied</th><th>Validated</th><th>Missing</th><th>Rolled back</th><th>Diff</th></tr></thead><tbody>",
            ($rows -join [Environment]::NewLine),
            "</tbody></table>",
            "</body></html>"
        ) -join [Environment]::NewLine
        Set-Content -LiteralPath $htmlTarget -Value $html -Encoding UTF8
    }

    if (-not [string]::IsNullOrWhiteSpace($rollbackPreviewTarget)) {
        $parent = Split-Path -Parent $rollbackPreviewTarget
        if (-not [string]::IsNullOrWhiteSpace($parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
        $previewTables = foreach ($table in $tables) {
            [pscustomobject]@{
                name = $table.name
                sourceRows = $table.rows
                keyColumns = @($table.keyColumns)
                wouldDeleteRows = if ($migration.mode -eq "rollback") { $table.rolledBackRows } else { $table.rows }
                dryRun = $true
            }
        }
        [ordered]@{
            format = "qtnetworkchat-sqlite-pg-rollback-preview-v1"
            generatedAt = (Get-Date).ToUniversalTime().ToString("o")
            sourceMode = $migration.mode
            dryRun = $true
            ok = $true
            postgresPassword = "<redacted>"
            tables = @($previewTables)
        } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $rollbackPreviewTarget -Encoding UTF8
    }
}

exit $exitCode
