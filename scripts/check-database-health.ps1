param(
    [ValidateSet("sqlite", "postgres")]
    [string]$Driver = "sqlite",
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$PostgresHost = "127.0.0.1",
    [int]$PostgresPort = 5432,
    [string]$PostgresDatabase = "qtnetworkchat",
    [string]$PostgresUser = "postgres",
    [AllowEmptyString()]
    [string]$PostgresPassword = $env:QTNETWORKCHAT_PGPASSWORD,
    [string]$SQLitePath = "accounts.sqlite3",
    [int]$ReconnectBackoffMs = 2000,
    [int]$PoolMaxConnections = 16,
    [int]$PoolIdleMs = 300000,
    [int]$SlowQueryMs = 1000,
    [switch]$InjectSlowQueryProbe,
    [int]$SlowQueryProbeSeconds = 1,
    [ValidateSet("none", "query", "schema", "auth", "network", "tls")]
    [string]$InjectQueryFailureReason = "none",
    [switch]$DisableConnectionPool,
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
        reason = if ($Ok) { "ok" } elseif ([string]::IsNullOrWhiteSpace($Reason)) { "unknown" } else { $Reason }
    }
}

function Get-PostgresErrorReason([string]$Text) {
    $value = ([string]$Text).ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($value)) {
        return "query"
    }
    if ($value -match "password|authentication|permission denied|role") {
        return "auth"
    }
    if ($value -match "ssl|tls|certificate") {
        return "tls"
    }
    if ($value -match "connection refused|timeout|timed out|could not connect|host|network|socket") {
        return "network"
    }
    if ($value -match "relation .* does not exist|table|schema|requiredtables=([0-9]+)/10") {
        return "schema"
    }
    return "query"
}

function Sanitize-HealthDetail([string]$Text) {
    $value = [string]$Text
    if ([string]::IsNullOrWhiteSpace($value)) {
        return ""
    }
    $value = $value -replace '(?i)(password|passphrase)\s*[:=]\s*[^;\s]+', '$1=<redacted>'
    $value = $value -replace '(?i)qtnetworkchat_pgpassword\s*[:=]\s*[^;\s]+', 'QTNETWORKCHAT_PGPASSWORD=<redacted>'
    $value = $value -replace '(?i)authorization\s*[:=]\s*[^;\r\n]+', 'Authorization=<redacted>'
    $value = $value -replace '\s+', ' '
    $value = $value.Trim()
    if ($value.Length -gt 160) {
        return $value.Substring(0, 160) + "..."
    }
    $value
}

function Get-InjectedQueryFailureDetail([string]$Reason) {
    switch ($Reason) {
        "schema" { return "Injected schema probe: relation does not exist" }
        "auth" { return "Injected auth probe: permission denied" }
        "network" { return "Injected network probe: connection refused" }
        "tls" { return "Injected tls probe: certificate verify failed" }
        "query" { return "Injected query probe: syntax error" }
        default { return "" }
    }
}

function Add-QueryFailure {
    param(
        [System.Collections.IDictionary]$Metrics,
        [string]$Reason,
        [string]$CheckName = "",
        [string]$Detail = ""
    )

    $fixedReason = if ([string]::IsNullOrWhiteSpace($Reason)) { "query" } else { $Reason }
    if (-not $Metrics.errorReasons.Contains($fixedReason)) {
        $fixedReason = "query"
    }
    $Metrics.queryFailureCount += 1
    $Metrics.lastErrorReason = $fixedReason
    $Metrics.lastErrorCheck = if ([string]::IsNullOrWhiteSpace($CheckName)) { "unknown" } else { $CheckName }
    $Metrics.lastErrorSample = Sanitize-HealthDetail $Detail
    $Metrics.errorReasons[$fixedReason] += 1
}

function Get-HealthOperatorAction {
    param(
        [string]$Driver,
        [bool]$PlanOnly,
        [bool]$Ok,
        [string]$Status,
        [int]$SlowQueryCount,
        [int]$QueryFailureCount
    )

    if (-not $Ok -or $Status -eq "unhealthy") {
        return "Fix runtime, credential, schema, or path issues before trusting database health."
    }
    if ($PlanOnly) {
        return "Runtime prerequisites look ready; next run can execute live database health checks."
    }
    if ($QueryFailureCount -gt 0) {
        return "Investigate query failures before promoting this database health snapshot."
    }
    if ($SlowQueryCount -gt 0) {
        return "Review slow queries and pool backoff thresholds before treating the database as release-ready."
    }
    if ($Driver -eq "postgres") {
        return "Archive the redacted PostgreSQL health evidence and use it for release readiness review."
    }
    "Archive the redacted SQLite health evidence and use it for local readiness review."
}

function Get-HealthReleaseGate {
    param(
        [bool]$PlanOnly,
        [bool]$Ok,
        [int]$SlowQueryCount,
        [int]$QueryFailureCount
    )

    if (-not $Ok) {
        return "blocked"
    }
    if ($PlanOnly) {
        return "await-live-health-check"
    }
    if ($QueryFailureCount -gt 0) {
        return "review-query-failures"
    }
    if ($SlowQueryCount -gt 0) {
        return "review-slow-queries"
    }
    "can-review-health-evidence"
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
    $checks.Add((New-Check "qt-qpsql-plugin" (Test-Path -LiteralPath $qpsqlPluginPath -PathType Leaf) $qpsqlPluginPath "runtime"))
    $checks.Add((New-Check "postgres-psql" (Test-Path -LiteralPath $psqlPath -PathType Leaf) $psqlPath "runtime"))
    $checks.Add((New-Check "postgres-libpq-runtime" (Test-Path -LiteralPath $libpqPath -PathType Leaf) $libpqPath "runtime"))
    $environment = [ordered]@{
        QT_PLUGIN_PATH = $qtPluginDir
        QTNETWORKCHAT_DB_DRIVER = "QPSQL"
        QTNETWORKCHAT_PGHOST = $PostgresHost
        QTNETWORKCHAT_PGPORT = "$PostgresPort"
        QTNETWORKCHAT_PGDATABASE = $PostgresDatabase
        QTNETWORKCHAT_PGUSER = $PostgresUser
        QTNETWORKCHAT_PGPASSWORD = "<redacted>"
        QTNETWORKCHAT_DB_POOL = if ($DisableConnectionPool) { "0" } else { "1" }
        QTNETWORKCHAT_DB_POOL_MAX = "$PoolMaxConnections"
        QTNETWORKCHAT_DB_POOL_IDLE_MS = "$PoolIdleMs"
        QTNETWORKCHAT_DB_RECONNECT_BACKOFF_MS = "$ReconnectBackoffMs"
        QTNETWORKCHAT_DB_SLOW_QUERY_MS = "$SlowQueryMs"
        QTNETWORKCHAT_DB_HEALTH_SLOW_PROBE = if ($InjectSlowQueryProbe) { "1" } else { "0" }
        QTNETWORKCHAT_DB_HEALTH_FAILURE_PROBE = $InjectQueryFailureReason
    }
} else {
    $checks.Add((New-Check "sqlite-parent" (Test-Path -LiteralPath (Split-Path -Parent $resolvedSqlitePath) -PathType Container) (Split-Path -Parent $resolvedSqlitePath) "path"))
    $environment = [ordered]@{
        QTNETWORKCHAT_DB_DRIVER = "QSQLITE"
        QTNETWORKCHAT_DB_PATH = $resolvedSqlitePath
        QTNETWORKCHAT_DB_POOL = "0"
        QTNETWORKCHAT_DB_POOL_MAX = "$PoolMaxConnections"
        QTNETWORKCHAT_DB_POOL_IDLE_MS = "$PoolIdleMs"
        QTNETWORKCHAT_DB_RECONNECT_BACKOFF_MS = "$ReconnectBackoffMs"
        QTNETWORKCHAT_DB_SLOW_QUERY_MS = "$SlowQueryMs"
        QTNETWORKCHAT_DB_HEALTH_SLOW_PROBE = "0"
        QTNETWORKCHAT_DB_HEALTH_FAILURE_PROBE = "none"
    }
}

$ok = @($checks | Where-Object { -not $_.ok }).Count -eq 0
$status = if ($ok) { "healthy" } else { "unhealthy" }
$queryMetrics = [ordered]@{
    slowQueryThresholdMs = $SlowQueryMs
    slowQueryCount = 0
    queryFailureCount = 0
    lastSlowQueryMs = 0
    lastErrorReason = ""
    lastErrorCheck = ""
    lastErrorSample = ""
    errorReasons = [ordered]@{
        runtime = 0
        auth = 0
        network = 0
        tls = 0
        schema = 0
        path = 0
        query = 0
    }
}

if (-not $PlanOnly -and $ok) {
    if ($normalizedDriver -eq "postgres") {
        if ([string]::IsNullOrWhiteSpace($PostgresPassword)) {
            $checks.Add((New-Check "postgres-password" $false "QTNETWORKCHAT_PGPASSWORD is required outside PlanOnly" "auth"))
            Add-QueryFailure $queryMetrics "auth" "postgres-password" "QTNETWORKCHAT_PGPASSWORD is required outside PlanOnly"
            $ok = $false
        } else {
            $oldPassword = $env:PGPASSWORD
            try {
                $env:PGPASSWORD = $PostgresPassword
                $query = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='public' AND table_name IN ('accounts','user_sessions','messages','offline_messages','friend_events','server_groups','server_group_members','server_group_removed_members','server_group_announcements','server_group_audit_events');"
                $queryTimer = [System.Diagnostics.Stopwatch]::StartNew()
                $psqlOutput = & $psqlPath @(
                    "-h", $PostgresHost,
                    "-p", "$PostgresPort",
                    "-U", $PostgresUser,
                    "-d", $PostgresDatabase,
                    "-t",
                    "-A",
                    "-c", $query
                ) 2>&1
                $queryTimer.Stop()
                $elapsedMs = [int][Math]::Min([int]::MaxValue, $queryTimer.ElapsedMilliseconds)
                if ($elapsedMs -gt $SlowQueryMs) {
                    $queryMetrics.slowQueryCount += 1
                    $queryMetrics.lastSlowQueryMs = $elapsedMs
                }
                $psqlExitCode = $LASTEXITCODE
                $tableCount = 0
                [void][int]::TryParse((([string]$psqlOutput).Trim()), [ref]$tableCount)
                $checkOk = $psqlExitCode -eq 0 -and $tableCount -eq 10
                $reason = if ($checkOk) { "ok" } elseif ($psqlExitCode -eq 0) { "schema" } else { Get-PostgresErrorReason ([string]$psqlOutput) }
                $detail = if ($psqlExitCode -eq 0) { "requiredTables=$tableCount/10" } else { "requiredTables=unknown/10" }
                $checks.Add((New-Check "postgres-required-tables" $checkOk $detail $reason))
                $ok = $ok -and $psqlExitCode -eq 0 -and $tableCount -eq 10
                if (-not $checkOk) {
                    Add-QueryFailure $queryMetrics $reason "postgres-required-tables" ([string]$psqlOutput)
                }
                if ($InjectSlowQueryProbe) {
                    $probeSeconds = [Math]::Max(0, $SlowQueryProbeSeconds)
                    $slowQuery = "SELECT pg_sleep($probeSeconds);"
                    $slowTimer = [System.Diagnostics.Stopwatch]::StartNew()
                    $slowOutput = & $psqlPath @(
                        "-h", $PostgresHost,
                        "-p", "$PostgresPort",
                        "-U", $PostgresUser,
                        "-d", $PostgresDatabase,
                        "-t",
                        "-A",
                        "-c", $slowQuery
                    ) 2>&1
                    $slowTimer.Stop()
                    $slowElapsedMs = [int][Math]::Min([int]::MaxValue, $slowTimer.ElapsedMilliseconds)
                    if ($slowElapsedMs -gt $SlowQueryMs) {
                        $queryMetrics.slowQueryCount += 1
                        $queryMetrics.lastSlowQueryMs = $slowElapsedMs
                    }
                    $slowExitCode = $LASTEXITCODE
                    $slowOk = $slowExitCode -eq 0
                    $checks.Add((New-Check "postgres-slow-query-probe" $slowOk ("elapsedMs=$slowElapsedMs thresholdMs=$SlowQueryMs") $(if ($slowOk) { "ok" } else { Get-PostgresErrorReason ([string]$slowOutput) })))
                    if (-not $slowOk) {
                        $ok = $false
                        Add-QueryFailure $queryMetrics (Get-PostgresErrorReason ([string]$slowOutput)) "postgres-slow-query-probe" ([string]$slowOutput)
                    }
                }
                if ($InjectQueryFailureReason -ne "none") {
                    Add-QueryFailure $queryMetrics $InjectQueryFailureReason "postgres-query-failure-probe" (Get-InjectedQueryFailureDetail $InjectQueryFailureReason)
                    $checks.Add((New-Check "postgres-query-failure-probe" $true ("injectedReason=$InjectQueryFailureReason") "ok"))
                }
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
    reconnectPolicy = [ordered]@{
        poolEnabled = [bool]($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool)
        maxConnections = $PoolMaxConnections
        idleMs = $PoolIdleMs
        backoffMs = $ReconnectBackoffMs
        slowQueryMs = $SlowQueryMs
        slowQueryProbeEnabled = [bool]$InjectSlowQueryProbe
        slowQueryProbeSeconds = $SlowQueryProbeSeconds
        queryFailureProbeReason = $InjectQueryFailureReason
        circuitBreaker = "skip-open-during-backoff"
        threadPolicy = [ordered]@{
            connectionOwnership = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "thread-affine pooled connections" } else { "direct-open per caller" }
            crossThreadReuse = $false
            checkoutScope = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "connection-name plus owning thread" } else { "not-applicable" }
            releaseScope = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "same thread that checked out or created the connection" } else { "not-applicable" }
            governance = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "cross-thread checkout is discarded and recreated; cross-thread release is closed instead of pooled" } else { "direct connections are closed by the caller" }
            idleReclaim = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "idle pooled connections are reclaimed after PoolIdleMs" } else { "not-applicable" }
            guidance = "Qt SQL connections are thread-affine; never reuse one opened connection object across threads."
        }
        reasonBuckets = @("ok", "runtime", "auth", "network", "tls", "schema", "path", "query")
    }
    queryMetrics = $queryMetrics
    checks = $checks
    environment = $environment
}
$failedChecks = @($checks | Where-Object { -not $_.ok })
$auditFocus = New-Object System.Collections.Generic.List[string]
if ($normalizedDriver -eq "postgres") {
    [void]$auditFocus.Add("postgres-runtime")
    [void]$auditFocus.Add("connection-pool-thread-policy")
} else {
    [void]$auditFocus.Add("sqlite-path")
}
if ($queryMetrics.queryFailureCount -gt 0) { [void]$auditFocus.Add("query-failures") }
if ($queryMetrics.slowQueryCount -gt 0) { [void]$auditFocus.Add("slow-queries") }
if ($InjectSlowQueryProbe) { [void]$auditFocus.Add("slow-query-probe") }
if ($InjectQueryFailureReason -ne "none") { [void]$auditFocus.Add("query-failure-probe") }
if ($failedChecks.Count -gt 0) { [void]$auditFocus.Add("failed-checks") }
if ($auditFocus.Count -eq 0) { [void]$auditFocus.Add("routine-health-review") }
$resultObject.summary = [ordered]@{
    readiness = if ($ok) { $(if ($PlanOnly) { "ready" } else { "verified" }) } else { "blocked" }
    failedCheckCount = $failedChecks.Count
    slowQueryCount = $queryMetrics.slowQueryCount
    queryFailureCount = $queryMetrics.queryFailureCount
    operatorAction = Get-HealthOperatorAction -Driver $normalizedDriver -PlanOnly ([bool]$PlanOnly) -Ok ([bool]$ok) -Status $status -SlowQueryCount $queryMetrics.slowQueryCount -QueryFailureCount $queryMetrics.queryFailureCount
}
$resultObject.auditSummary = [ordered]@{
    releaseGate = Get-HealthReleaseGate -PlanOnly ([bool]$PlanOnly) -Ok ([bool]$ok) -SlowQueryCount $queryMetrics.slowQueryCount -QueryFailureCount $queryMetrics.queryFailureCount
    driverMode = if ($normalizedDriver -eq "postgres") { "server-database" } else { "local-database" }
    poolMode = if ($normalizedDriver -eq "postgres" -and -not $DisableConnectionPool) { "pooled" } else { "direct-open" }
    evidenceBundle = @("json", "checks", "query-metrics")
    auditFocus = @($auditFocus)
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
