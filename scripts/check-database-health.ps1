param(
    [ValidateSet("sqlite", "postgres")]
    [string]$Driver = "sqlite",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$SQLitePath = "accounts.sqlite3",
    [switch]$PlanOnly,
    [switch]$FailOnUnhealthy,
    [string]$JsonPath
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $Path
}

function New-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Detail,
        [string]$Reason = ""
    )

    [pscustomobject]@{
        name = $Name
        ok = $Ok
        detail = $Detail
        reason = $Reason
    }
}

$normalizedDriver = $Driver.ToLowerInvariant()
$qtPluginDir = Join-Path $QtRoot "plugins"
$qpsqlPluginPath = Join-Path $qtPluginDir "sqldrivers\qsqlpsql.dll"
$psqlPath = Join-Path $PostgresBinDir "psql.exe"
$libpqPath = Join-Path $PostgresBinDir "libpq.dll"
$resolvedSqlitePath = Resolve-RepoPath $SQLitePath

$checks = [System.Collections.Generic.List[object]]::new()
$environment = [ordered]@{}

if ($normalizedDriver -eq "postgres") {
    $checks.Add((New-Check "qt-qpsql-plugin" (Test-Path -LiteralPath $qpsqlPluginPath -PathType Leaf) $qpsqlPluginPath))
    $checks.Add((New-Check "postgres-psql" (Test-Path -LiteralPath $psqlPath -PathType Leaf) $psqlPath))
    $checks.Add((New-Check "postgres-libpq-runtime" (Test-Path -LiteralPath $libpqPath -PathType Leaf) $libpqPath))
    $environment = [ordered]@{
        QT_PLUGIN_PATH = $qtPluginDir
        QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        QTNETWORKCHAT_PGHOST = $PostgresHost
        QTNETWORKCHAT_PGPORT = "$PostgresPort"
        QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        QTNETWORKCHAT_PGUSER = $PostgresUser
        QTNETWORKCHAT_PGPASSWORD = "<redacted>"
    }
} else {
    $checks.Add((New-Check "sqlite-parent" (Test-Path -LiteralPath (Split-Path -Parent $resolvedSqlitePath) -PathType Container) (Split-Path -Parent $resolvedSqlitePath)))
    $environment = [ordered]@{
        QTNETWORKCHAT_DB_DRIVER = "QSQLITE"
        QTNETWORKCHAT_DB_PATH = $resolvedSqlitePath
    }
}

$ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
$status = if ($ok) { "healthy" } else { "unhealthy" }

if (-not $PlanOnly -and $ok) {
    if ($normalizedDriver -eq "postgres") {
        if ([string]::IsNullOrWhiteSpace($PostgresPassword)) {
            $checks.Add((New-Check "postgres-password" $false "QTNETWORKCHAT_PGPASSWORD" "PostgresPassword is required outside PlanOnly"))
            $ok = $false
        } else {
            $oldPassword = $env:PGPASSWORD
            try {
                $env:PGPASSWORD = $PostgresPassword
                $query = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='public' AND table_name IN ('accounts','user_sessions','messages','offline_messages','friend_events','server_groups','server_group_members','server_group_removed_members','server_group_announcements','server_group_audit_events');"
                $psqlOutput = & $psqlPath @(
                    "-h", $PostgresHost,
                    "-p", "$PostgresPort",
                    "-U", $PostgresUser,
                    "-d", $PostgresDatabase,
                    "-t",
                    "-A",
                    "-c", $query
                ) 2>&1
                $psqlExitCode = $LASTEXITCODE
                $tableCount = 0
                [void][int]::TryParse((([string]$psqlOutput).Trim()), [ref]$tableCount)
                $reason = if ($psqlExitCode -eq 0) { "" } else { ([string]$psqlOutput).Trim() }
                $checks.Add((New-Check "postgres-required-tables" ($psqlExitCode -eq 0 -and $tableCount -eq 10) "requiredTables=$tableCount/10" $reason))
                $ok = $ok -and $psqlExitCode -eq 0 -and $tableCount -eq 10
            } finally {
                $env:PGPASSWORD = $oldPassword
            }
        }
    } else {
        $checks.Add((New-Check "sqlite-file" (Test-Path -LiteralPath $resolvedSqlitePath -PathType Leaf) $resolvedSqlitePath "SQLite file is optional before first server start"))
    }
    $status = if ($ok) { "healthy" } else { "unhealthy" }
}

$resultObject = [ordered]@{
    format = "qtnetworkchat-database-health-check-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = [bool]$PlanOnly
    driver = $normalizedDriver
    ok = [bool]$ok
    status = $status
    checks = $checks
    environment = $environment
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $jsonTarget = Resolve-RepoPath $JsonPath
    $parent = Split-Path -Parent $jsonTarget
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $resultObject | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonTarget -Encoding UTF8
}

$resultObject | ConvertTo-Json -Depth 8
if ($FailOnUnhealthy -and -not $ok) {
    exit 2
}
