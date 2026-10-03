param(
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$RedisBinDir = "D:\Program Files\Redis-8.6.2",
    [string]$RedisHost = "127.0.0.1",
    [int]$RedisPort = 6379,
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$MinioServerPath = "D:\Dminio-server\minio.windows-amd64.RELEASE.2025-09-07T16-13-09Z.exe",
    [string]$MinioClientPath = "D:\Dminio-server\mc.windows-amd64.RELEASE.2025-08-13T08-35-41Z.exe",
    [string]$MinioEndpoint = "http://127.0.0.1:19000",
    [switch]$PlanOnly,
    [string]$JsonPath
)

$ErrorActionPreference = "Stop"

function Resolve-ToolPath {
    param(
        [string]$Directory,
        [string[]]$Names
    )

    foreach ($name in $Names) {
        $candidate = Join-Path $Directory $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    foreach ($name in $Names) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }
    return ""
}

function New-CheckResult {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Mode,
        [string]$Detail
    )

    [pscustomobject]@{
        name = $Name
        ok = $Ok
        mode = $Mode
        detail = $Detail
    }
}

$psqlPath = Resolve-ToolPath -Directory $PostgresBinDir -Names @("psql.exe", "psql")
$redisCliPath = Resolve-ToolPath -Directory $RedisBinDir -Names @("redis-cli.exe", "redis-cli")
$qpsqlPluginPath = if ([string]::IsNullOrWhiteSpace($QtRoot)) { "" } else { Join-Path $QtRoot "plugins\sqldrivers\qsqlpsql.dll" }
$qpsqlPluginExists = -not [string]::IsNullOrWhiteSpace($qpsqlPluginPath) -and (Test-Path -LiteralPath $qpsqlPluginPath -PathType Leaf)
$libpqPath = if ([string]::IsNullOrWhiteSpace($PostgresBinDir)) { "" } else { Join-Path $PostgresBinDir "libpq.dll" }
$libpqExists = -not [string]::IsNullOrWhiteSpace($libpqPath) -and (Test-Path -LiteralPath $libpqPath -PathType Leaf)
$minioServerExists = -not [string]::IsNullOrWhiteSpace($MinioServerPath) -and (Test-Path -LiteralPath $MinioServerPath -PathType Leaf)
$minioClientExists = -not [string]::IsNullOrWhiteSpace($MinioClientPath) -and (Test-Path -LiteralPath $MinioClientPath -PathType Leaf)

$checks = [System.Collections.Generic.List[object]]::new()
$checks.Add((New-CheckResult "postgres-client" (-not [string]::IsNullOrWhiteSpace($psqlPath)) "path" $psqlPath))
$checks.Add((New-CheckResult "qt-qpsql-plugin" $qpsqlPluginExists "path" $qpsqlPluginPath))
$checks.Add((New-CheckResult "postgres-libpq-runtime" $libpqExists "path" $libpqPath))
$checks.Add((New-CheckResult "redis-client" (-not [string]::IsNullOrWhiteSpace($redisCliPath)) "path" $redisCliPath))
$checks.Add((New-CheckResult "minio-server" $minioServerExists "path" $MinioServerPath))
$checks.Add((New-CheckResult "minio-client" $minioClientExists "path" $MinioClientPath))

if (-not $PlanOnly) {
    if (-not [string]::IsNullOrWhiteSpace($psqlPath)) {
        $oldPassword = $env:PGPASSWORD
        $oldNativePreference = $PSNativeCommandUseErrorActionPreference
        $oldErrorActionPreference = $ErrorActionPreference
        $env:PGPASSWORD = $PostgresPassword
        $PSNativeCommandUseErrorActionPreference = $false
        $ErrorActionPreference = "Continue"
        try {
            $output = & $psqlPath -h $PostgresHost -p $PostgresPort -U $PostgresUser -d $PostgresDatabase -tAc "select 1" 2>&1
            $checks.Add((New-CheckResult "postgres-connect" ($LASTEXITCODE -eq 0 -and ([string]$output).Trim() -eq "1") "connect" "host=$PostgresHost port=$PostgresPort database=$PostgresDatabase user=$PostgresUser"))
        } finally {
            $env:PGPASSWORD = $oldPassword
            $PSNativeCommandUseErrorActionPreference = $oldNativePreference
            $ErrorActionPreference = $oldErrorActionPreference
        }
    } else {
        $checks.Add((New-CheckResult "postgres-connect" $false "connect" "psql not found"))
    }

    if (-not [string]::IsNullOrWhiteSpace($redisCliPath)) {
        $oldNativePreference = $PSNativeCommandUseErrorActionPreference
        $oldErrorActionPreference = $ErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
        $ErrorActionPreference = "Continue"
        try {
        $output = & $redisCliPath -h $RedisHost -p $RedisPort PING 2>&1
        $checks.Add((New-CheckResult "redis-ping" ($LASTEXITCODE -eq 0 -and ([string]$output).Trim() -eq "PONG") "connect" "host=$RedisHost port=$RedisPort"))
        } finally {
            $PSNativeCommandUseErrorActionPreference = $oldNativePreference
            $ErrorActionPreference = $oldErrorActionPreference
        }
    } else {
        $checks.Add((New-CheckResult "redis-ping" $false "connect" "redis-cli not found"))
    }
}

$windowsEnv = [ordered]@{
    QTNETWORKCHAT_DB_DRIVER = "QPSQL"
    QTNETWORKCHAT_PGHOST = $PostgresHost
    QTNETWORKCHAT_PGPORT = "$PostgresPort"
    QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
    QTNETWORKCHAT_PGUSER = $PostgresUser
    QTNETWORKCHAT_PGPASSWORD = "<redacted>"
    QTNETWORKCHAT_REDIS_HOST = $RedisHost
    QTNETWORKCHAT_REDIS_PORT = "$RedisPort"
    QTNETWORKCHAT_OBJECT_STORE = "s3"
    QTNETWORKCHAT_OBJECT_S3_ENDPOINT = $MinioEndpoint
}
$linuxExports = @(
    "export QTNETWORKCHAT_DB_DRIVER=QPSQL",
    "export QTNETWORKCHAT_PGHOST='$PostgresHost'",
    "export QTNETWORKCHAT_PGPORT='$PostgresPort'",
    "export QTNETWORKCHAT_PGDATABASE='$PostgresDatabase'",
    "export QTNETWORKCHAT_PGUSER='$PostgresUser'",
    "export QTNETWORKCHAT_PGPASSWORD='<redacted>'",
    "export QTNETWORKCHAT_REDIS_HOST='$RedisHost'",
    "export QTNETWORKCHAT_REDIS_PORT='$RedisPort'",
    "export QTNETWORKCHAT_OBJECT_STORE=s3",
    "export QTNETWORKCHAT_OBJECT_S3_ENDPOINT='$MinioEndpoint'"
)

$result = [ordered]@{
    format = "qtnetworkchat-local-infra-check-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = [bool]$PlanOnly
    ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
    checks = @($checks)
    windowsEnvironment = $windowsEnv
    linuxExports = $linuxExports
    notes = "PostgreSQL and SQLite can coexist: keep QTNETWORKCHAT_DB_DRIVER unset for SQLite, set it to QPSQL for PostgreSQL."
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $parent = Split-Path -Parent $JsonPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $JsonPath -Encoding UTF8
}

$result | ConvertTo-Json -Depth 8
if (-not $result.ok) {
    exit 2
}
