param(
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$DataDir = "build-qt6-mingw\pg-real-data",
    [string]$LogPath = "build-qt6-mingw\pg-real.log",
    [string]$Database = "qtnetworkchat",
    [string]$User = "postgres",
    [string]$Password = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$HostAddress = "127.0.0.1",
    [int]$Port = 5432,
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
    param([string]$Name, [bool]$Ok, [string]$Detail)
    [pscustomobject]@{
        name = $Name
        ok = $Ok
        detail = $Detail
    }
}

function Get-BootstrapOperatorAction {
    param(
        [bool]$PlanOnly,
        [bool]$Ok,
        [bool]$Initialized,
        [bool]$DatabaseReady
    )

    if (-not $Ok) {
        return "Fix PostgreSQL bootstrap prerequisites before attempting local startup."
    }
    if ($PlanOnly) {
        return "Local PostgreSQL bootstrap prerequisites look ready; next run can start or reuse the target instance."
    }
    if (-not $Initialized) {
        return "Review initdb output and data directory permissions before retrying local PostgreSQL bootstrap."
    }
    if (-not $DatabaseReady) {
        return "Review pg_ctl and psql output before trusting the local PostgreSQL bootstrap."
    }
    "Archive the redacted bootstrap evidence and use it for local PostgreSQL smoke readiness review."
}

function Get-BootstrapReleaseGate {
    param(
        [bool]$PlanOnly,
        [string]$Readiness
    )

    if ($Readiness -eq "blocked") {
        return "blocked"
    }
    if ($PlanOnly) {
        return "await-local-bootstrap-run"
    }
    "can-run-pgsql-smoke"
}

$pgBin = Resolve-RepoPath $PostgresBinDir
$dataPath = Resolve-RepoPath $DataDir
$logFile = Resolve-RepoPath $LogPath
$initdbPath = Join-Path $pgBin "initdb.exe"
$pgCtlPath = Join-Path $pgBin "pg_ctl.exe"
$pgIsReadyPath = Join-Path $pgBin "pg_isready.exe"
$createdbPath = Join-Path $pgBin "createdb.exe"
$psqlPath = Join-Path $pgBin "psql.exe"

$checks = @(
    (New-Check "postgres-bin" (Test-Path -LiteralPath $pgBin -PathType Container) $pgBin),
    (New-Check "initdb" (Test-Path -LiteralPath $initdbPath -PathType Leaf) $initdbPath),
    (New-Check "pg-ctl" (Test-Path -LiteralPath $pgCtlPath -PathType Leaf) $pgCtlPath),
    (New-Check "pg-isready" (Test-Path -LiteralPath $pgIsReadyPath -PathType Leaf) $pgIsReadyPath),
    (New-Check "createdb" (Test-Path -LiteralPath $createdbPath -PathType Leaf) $createdbPath),
    (New-Check "psql" (Test-Path -LiteralPath $psqlPath -PathType Leaf) $psqlPath)
)

$result = [ordered]@{
    format = "qtnetworkchat-local-postgres-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    planOnly = [bool]$PlanOnly
    ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
    checks = $checks
    dataDir = $dataPath
    logPath = $logFile
    host = $HostAddress
    port = $Port
    database = $Database
    user = $User
    password = "<redacted>"
    initialized = Test-Path -LiteralPath (Join-Path $dataPath "PG_VERSION") -PathType Leaf
    started = $false
    databaseReady = $false
}
$result.summary = [ordered]@{
    readiness = if ($result.ok) { "ready" } else { "blocked" }
    failedCheckCount = @($checks | Where-Object { -not $_.ok }).Count
    operatorAction = Get-BootstrapOperatorAction -PlanOnly ([bool]$PlanOnly) -Ok ([bool]$result.ok) -Initialized ([bool]$result.initialized) -DatabaseReady ([bool]$result.databaseReady)
}
$result.auditSummary = [ordered]@{
    releaseGate = Get-BootstrapReleaseGate -PlanOnly ([bool]$PlanOnly) -Readiness $result.summary.readiness
    evidenceBundle = @("json", "bootstrap-checks")
    bootstrapMode = if ($PlanOnly) { "plan" } else { "local-runtime" }
    dataDirectoryExists = Test-Path -LiteralPath $dataPath -PathType Container
    logDirectoryExists = Test-Path -LiteralPath (Split-Path -Parent $logFile) -PathType Container
    requiresPassword = (-not [bool]$PlanOnly)
    auditFocus = @("bootstrap-prerequisites", "local-runtime-paths")
}

if (-not $PlanOnly) {
    if (-not $result.ok) {
        $missing = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
        throw ("Local PostgreSQL prerequisites missing: {0}" -f ($missing -join ", "))
    }
    if ([string]::IsNullOrWhiteSpace($Password)) {
        throw "Password is required. Pass -Password or set QTNETWORKCHAT_PGPASSWORD."
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dataPath) | Out-Null
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logFile) | Out-Null
    $pwFile = Join-Path (Split-Path -Parent $dataPath) "pg-real-pw.txt"
    Set-Content -LiteralPath $pwFile -Value $Password -NoNewline -Encoding ASCII

    try {
        if (-not (Test-Path -LiteralPath (Join-Path $dataPath "PG_VERSION") -PathType Leaf)) {
            & $initdbPath -D $dataPath -U $User --auth-host=scram-sha-256 --auth-local=trust --pwfile=$pwFile | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "initdb failed with exit code $LASTEXITCODE" }
            $result.initialized = $true
        }

        & $pgIsReadyPath -h $HostAddress -p $Port | Out-Null
        if ($LASTEXITCODE -ne 0) {
            & $pgCtlPath -D $dataPath -l $logFile -o "`"-h`" `"$HostAddress`" `"-p`" `"$Port`"" start | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "pg_ctl start failed with exit code $LASTEXITCODE" }
        }
        $result.started = $true

        $oldPassword = $env:PGPASSWORD
        $oldNativePreference = $PSNativeCommandUseErrorActionPreference
        $oldErrorActionPreference = $ErrorActionPreference
        try {
            $env:PGPASSWORD = $Password
            $PSNativeCommandUseErrorActionPreference = $false
            $ErrorActionPreference = "Continue"
            & $createdbPath -h $HostAddress -p $Port -U $User $Database 2>&1 | Out-Null
            & $psqlPath -h $HostAddress -p $Port -U $User -d $Database -tAc "select 1" | Out-Null
            $result.databaseReady = $LASTEXITCODE -eq 0
            $result.ok = [bool]$result.databaseReady
            $result.summary.readiness = if ($result.ok) { "verified" } else { "blocked" }
            $result.summary.operatorAction = Get-BootstrapOperatorAction -PlanOnly $false -Ok ([bool]$result.ok) -Initialized ([bool]$result.initialized) -DatabaseReady ([bool]$result.databaseReady)
            $result.auditSummary.releaseGate = Get-BootstrapReleaseGate -PlanOnly $false -Readiness $result.summary.readiness
            $result.auditSummary.dataDirectoryExists = Test-Path -LiteralPath $dataPath -PathType Container
            $result.auditSummary.logDirectoryExists = Test-Path -LiteralPath (Split-Path -Parent $logFile) -PathType Container
        } finally {
            $env:PGPASSWORD = $oldPassword
            $PSNativeCommandUseErrorActionPreference = $oldNativePreference
            $ErrorActionPreference = $oldErrorActionPreference
        }
    } finally {
        Remove-Item -LiteralPath $pwFile -Force -ErrorAction SilentlyContinue
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
