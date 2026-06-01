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
    exit $LASTEXITCODE
} finally {
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
}
