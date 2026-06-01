param(
    [string]$TestExe = "build-qt6-mingw\postgres_qpsql_protocol_smoke_test.exe",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$AppDataDir,
    [switch]$PlanOnly,
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
        [string]$Detail
    )

    [pscustomobject]@{
        name = $Name
        ok = $Ok
        detail = $Detail
    }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$testExePath = Resolve-RepoPath $TestExe
$qtBinDir = Join-Path $QtRoot "bin"
$qtPluginDir = Join-Path $QtRoot "plugins"
$qpsqlPluginPath = Join-Path $qtPluginDir "sqldrivers\qsqlpsql.dll"
$libpqPath = Join-Path $PostgresBinDir "libpq.dll"
$runtimeAppData = if ([string]::IsNullOrWhiteSpace($AppDataDir)) {
    Join-Path $repoRoot "build-qt6-mingw\pgsql-protocol-smoke-runtime"
} else {
    Resolve-RepoPath $AppDataDir
}

$checks = @(
    (New-Check "test-executable" (Test-Path -LiteralPath $testExePath -PathType Leaf) $testExePath),
    (New-Check "qt-bin" (Test-Path -LiteralPath $qtBinDir -PathType Container) $qtBinDir),
    (New-Check "qt-qpsql-plugin" (Test-Path -LiteralPath $qpsqlPluginPath -PathType Leaf) $qpsqlPluginPath),
    (New-Check "postgres-bin" (Test-Path -LiteralPath $PostgresBinDir -PathType Container) $PostgresBinDir),
    (New-Check "postgres-libpq-runtime" (Test-Path -LiteralPath $libpqPath -PathType Leaf) $libpqPath)
)

$coverageSurfaces = @(
    "register-login",
    "private-message-persistence",
    "friend-search-request-accept",
    "public-group-announcement-audit",
    "public-group-member-role-audit",
    "file-metadata-persistence",
    "offline-private-queue-replay",
    "restart-login-kdf-session"
)

$result = [ordered]@{
    format = "qtnetworkchat-pgsql-protocol-smoke-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = [bool]$PlanOnly
    ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
    checks = $checks
    coverageSurfaces = $coverageSurfaces
    testExecutable = $testExePath
    qtRoot = $QtRoot
    postgresBinDir = $PostgresBinDir
    appDataDir = $runtimeAppData
    environment = [ordered]@{
        QT_PLUGIN_PATH = $qtPluginDir
        QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = "1"
        QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        QTNETWORKCHAT_PGHOST = $PostgresHost
        QTNETWORKCHAT_PGPORT = "$PostgresPort"
        QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        QTNETWORKCHAT_PGUSER = $PostgresUser
        QTNETWORKCHAT_PGPASSWORD = "<redacted>"
    }
    exitCode = $null
}

if (-not $PlanOnly) {
    if (-not $result.ok) {
        $missing = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
        throw ("PostgreSQL protocol smoke prerequisites missing: {0}" -f ($missing -join ", "))
    }
    if ([string]::IsNullOrWhiteSpace($PostgresPassword)) {
        throw "PostgresPassword is required for real PostgreSQL protocol smoke"
    }

    New-Item -ItemType Directory -Force -Path $runtimeAppData | Out-Null

    $oldPath = $env:PATH
    $oldPluginPath = $env:QT_PLUGIN_PATH
    $oldRun = $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST
    $oldDriver = $env:QTNETWORKCHAT_DB_DRIVER
    $oldHost = $env:QTNETWORKCHAT_PGHOST
    $oldPort = $env:QTNETWORKCHAT_PGPORT
    $oldDatabase = $env:QTNETWORKCHAT_PGDATABASE
    $oldUser = $env:QTNETWORKCHAT_PGUSER
    $oldPassword = $env:QTNETWORKCHAT_PGPASSWORD
    $oldAppData = $env:QTNETWORKCHAT_APPDATA_DIR
    try {
        $env:PATH = "$qtBinDir;$PostgresBinDir;$oldPath"
        $env:QT_PLUGIN_PATH = $qtPluginDir
        $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = "1"
        $env:QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        $env:QTNETWORKCHAT_PGHOST = $PostgresHost
        $env:QTNETWORKCHAT_PGPORT = "$PostgresPort"
        $env:QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        $env:QTNETWORKCHAT_PGUSER = $PostgresUser
        $env:QTNETWORKCHAT_PGPASSWORD = $PostgresPassword
        $env:QTNETWORKCHAT_APPDATA_DIR = $runtimeAppData

        & $testExePath
        $result.exitCode = $LASTEXITCODE
        $result.ok = $result.ok -and ($LASTEXITCODE -eq 0)
    } finally {
        $env:PATH = $oldPath
        $env:QT_PLUGIN_PATH = $oldPluginPath
        $env:QTNETWORKCHAT_RUN_REAL_QPSQL_TEST = $oldRun
        $env:QTNETWORKCHAT_DB_DRIVER = $oldDriver
        $env:QTNETWORKCHAT_PGHOST = $oldHost
        $env:QTNETWORKCHAT_PGPORT = $oldPort
        $env:QTNETWORKCHAT_PGDATABASE = $oldDatabase
        $env:QTNETWORKCHAT_PGUSER = $oldUser
        $env:QTNETWORKCHAT_PGPASSWORD = $oldPassword
        $env:QTNETWORKCHAT_APPDATA_DIR = $oldAppData
    }
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $jsonTarget = Resolve-RepoPath $JsonPath
    $parent = Split-Path -Parent $jsonTarget
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonTarget -Encoding UTF8
}

$result | ConvertTo-Json -Depth 8
if (-not $result.ok) {
    exit 2
}
