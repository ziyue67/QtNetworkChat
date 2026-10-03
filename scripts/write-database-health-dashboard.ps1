param(
    [string]$HealthPath,
    [string]$StatusPath,
    [string]$TaskPreviewPath,
    [Parameter(Mandatory = $true)]
    [string]$DashboardPath,
    [string]$MarkdownPath,
    [switch]$FailOnUnhealthy
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
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

function Normalize-Bool([object]$Value, [bool]$DefaultValue = $false) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return $Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    return $text -eq "true" -or $text -eq "1" -or $text -eq "yes"
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

$resolvedHealthPath = Resolve-OptionalPath $HealthPath
$resolvedStatusPath = Resolve-OptionalPath $StatusPath
$resolvedTaskPreviewPath = Resolve-OptionalPath $TaskPreviewPath
$resolvedDashboardPath = Resolve-OptionalPath $DashboardPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath

if ([string]::IsNullOrWhiteSpace($resolvedHealthPath) -and
    [string]::IsNullOrWhiteSpace($resolvedStatusPath) -and
    [string]::IsNullOrWhiteSpace($resolvedTaskPreviewPath)) {
    throw "At least one of HealthPath, StatusPath, or TaskPreviewPath is required."
}

$health = Read-OptionalJson $resolvedHealthPath
$statusSummary = Read-OptionalJson $resolvedStatusPath
$taskPreview = Read-OptionalJson $resolvedTaskPreviewPath

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $resolvedHealthPath $sensitiveHits
Add-SensitiveHits $resolvedStatusPath $sensitiveHits
Add-SensitiveHits $resolvedTaskPreviewPath $sensitiveHits

$warnings = New-Object System.Collections.ArrayList
if (-not [string]::IsNullOrWhiteSpace($resolvedHealthPath) -and $null -eq $health) {
    [void]$warnings.Add("health-input-missing-or-empty")
}
if (-not [string]::IsNullOrWhiteSpace($resolvedStatusPath) -and $null -eq $statusSummary) {
    [void]$warnings.Add("status-input-missing-or-empty")
}
if (-not [string]::IsNullOrWhiteSpace($resolvedTaskPreviewPath) -and $null -eq $taskPreview) {
    [void]$warnings.Add("task-preview-missing-or-empty")
}
if ($sensitiveHits.Count -gt 0) {
    [void]$warnings.Add("sensitive-fields-detected")
}

$healthOk = Normalize-Bool (Get-JsonValue $health "ok" $false)
$statusOk = Normalize-Bool (Get-JsonValue $statusSummary "ok" $false)
$healthPlanOnly = Normalize-Bool (Get-JsonValue $health "planOnly" $false)
$statusPlanOnly = Normalize-Bool (Get-JsonValue $statusSummary "planOnly" $false)
$planOnly = [bool]($healthPlanOnly -or $statusPlanOnly)
$healthStatus = [string](Get-JsonValue $health "status" "")
$statusStatus = [string](Get-JsonValue $statusSummary "status" "")
$driver = [string](Get-JsonValue $statusSummary "driver" (Get-JsonValue $health "driver" (Get-JsonValue (Get-JsonValue $health "config" $null) "driver" "unknown")))
$healthQueryMetrics = Get-JsonValue $health "queryMetrics" $null
$statusQueryMetrics = Get-JsonValue $statusSummary "queryMetrics" $null
$slowQueryThresholdMs = [int](Get-JsonValue $statusQueryMetrics "slowQueryThresholdMs" (Get-JsonValue $healthQueryMetrics "slowQueryThresholdMs" 0))
$slowQueryCount = [int](Get-JsonValue $statusQueryMetrics "slowQueryCount" (Get-JsonValue $healthQueryMetrics "slowQueryCount" 0))
$queryFailureCount = [int](Get-JsonValue $statusQueryMetrics "queryFailureCount" (Get-JsonValue $healthQueryMetrics "queryFailureCount" 0))
$lastSlowQueryMs = [int](Get-JsonValue $statusQueryMetrics "lastSlowQueryMs" (Get-JsonValue $healthQueryMetrics "lastSlowQueryMs" 0))
$lastErrorReason = [string](Get-JsonValue $statusQueryMetrics "lastErrorReason" (Get-JsonValue $healthQueryMetrics "lastErrorReason" ""))
$lastErrorCheck = [string](Get-JsonValue $statusQueryMetrics "lastErrorCheck" (Get-JsonValue $healthQueryMetrics "lastErrorCheck" ""))
$lastErrorSample = [string](Get-JsonValue $statusQueryMetrics "lastErrorSample" (Get-JsonValue $healthQueryMetrics "lastErrorSample" ""))
$healthReconnectPolicy = Get-JsonValue $health "reconnectPolicy" $null
$statusReconnectPolicy = Get-JsonValue $statusSummary "reconnectPolicy" $null
$reconnectPolicy = if ($null -ne $statusReconnectPolicy) { $statusReconnectPolicy } else { $healthReconnectPolicy }
$poolMetrics = Get-JsonValue $health "pool" $null
$threadPolicy = Get-JsonValue $reconnectPolicy "threadPolicy" $null
if ($null -eq $threadPolicy) {
    $threadPolicy = Get-JsonValue $poolMetrics "threadPolicy" $null
}
$checks = @((Get-JsonValue $health "checks" (Get-JsonValue $statusSummary "checks" @())))
$failedChecks = @()
foreach ($check in $checks) {
    if (-not (Normalize-Bool (Get-JsonValue $check "ok" $false))) {
        $failedChecks += [string](Get-JsonValue $check "name" "unknown")
    }
}
foreach ($failedCheck in @((Get-JsonValue $statusSummary "failedChecks" @()))) {
    if (-not [string]::IsNullOrWhiteSpace([string]$failedCheck) -and $failedChecks -notcontains [string]$failedCheck) {
        $failedChecks += [string]$failedCheck
    }
}
if ($failedChecks.Count -gt 0) {
    [void]$warnings.Add("database-checks-failed")
}
if ($queryFailureCount -gt 0) {
    [void]$warnings.Add("database-query-failures")
}
if ($slowQueryCount -gt 0) {
    [void]$warnings.Add("database-slow-queries")
}
if ($planOnly) {
    [void]$warnings.Add("plan-only-health-evidence")
}

$hasHealthSignal = $null -ne $health -or $null -ne $statusSummary
$ok = $hasHealthSignal -and ($healthOk -or $statusOk) -and $failedChecks.Count -eq 0 -and $sensitiveHits.Count -eq 0
$overallStatus = if ($sensitiveHits.Count -gt 0 -or $failedChecks.Count -gt 0 -or (($null -ne $health -and -not $healthOk) -and ($null -ne $statusSummary -and -not $statusOk))) {
    "unhealthy"
} elseif ($ok) {
    "healthy"
} else {
    "unknown"
}

$taskConfigured = $null -ne $taskPreview
$dashboard = [ordered]@{
    format = "qtnetworkchat-database-health-dashboard-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    status = $overallStatus
    ok = [bool]($overallStatus -eq "healthy")
    planOnly = $planOnly
    driver = $driver
    healthStatus = $healthStatus
    statusSummaryStatus = $statusStatus
    checkCount = $checks.Count
    failedChecks = @($failedChecks)
    queryMetrics = [ordered]@{
        slowQueryThresholdMs = $slowQueryThresholdMs
        slowQueryCount = $slowQueryCount
        queryFailureCount = $queryFailureCount
        lastSlowQueryMs = $lastSlowQueryMs
        lastErrorReason = $lastErrorReason
        lastErrorCheck = $lastErrorCheck
        lastErrorSample = $lastErrorSample
        errorReasons = Get-JsonValue $statusQueryMetrics "errorReasons" (Get-JsonValue $healthQueryMetrics "errorReasons" $null)
    }
    reconnectPolicy = [ordered]@{
        poolEnabled = Normalize-Bool (Get-JsonValue $reconnectPolicy "poolEnabled" $false)
        maxConnections = Get-JsonValue $reconnectPolicy "maxConnections" $null
        idleMs = Get-JsonValue $reconnectPolicy "idleMs" $null
        backoffMs = Get-JsonValue $reconnectPolicy "backoffMs" $null
        slowQueryMs = Get-JsonValue $reconnectPolicy "slowQueryMs" $null
        pooledConnections = Get-JsonValue $poolMetrics "pooledConnections" $null
        pooledConnectionThreadCount = Get-JsonValue $poolMetrics "pooledConnectionThreadCount" $null
        peakPooledConnections = Get-JsonValue $poolMetrics "peakPooledConnections" $null
        idleConnectionsClosed = Get-JsonValue $poolMetrics "idleConnectionsClosed" $null
        overflowConnectionsClosed = Get-JsonValue $poolMetrics "overflowConnectionsClosed" $null
        crossThreadCheckoutPrevented = Get-JsonValue $poolMetrics "crossThreadCheckoutPrevented" $null
        crossThreadReleaseDetected = Get-JsonValue $poolMetrics "crossThreadReleaseDetected" $null
        circuitBreaker = [string](Get-JsonValue $reconnectPolicy "circuitBreaker" "")
        threadPolicy = [ordered]@{
            connectionOwnership = [string](Get-JsonValue $threadPolicy "connectionOwnership" "")
            crossThreadReuse = Get-JsonValue $threadPolicy "crossThreadReuse" $null
            checkoutScope = [string](Get-JsonValue $threadPolicy "checkoutScope" "")
            releaseScope = [string](Get-JsonValue $threadPolicy "releaseScope" "")
            governance = [string](Get-JsonValue $threadPolicy "governance" "")
            idleReclaim = [string](Get-JsonValue $threadPolicy "idleReclaim" "")
            guidance = [string](Get-JsonValue $threadPolicy "guidance" "")
        }
    }
    warningCount = $warnings.Count
    warnings = @($warnings)
    sensitiveHits = @($sensitiveHits)
    taskConfigured = [bool]$taskConfigured
    taskName = [string](Get-JsonValue $taskPreview "taskName" "")
    taskSchedule = [string](Get-JsonValue $taskPreview "schedule" "")
    taskRegister = Normalize-Bool (Get-JsonValue $taskPreview "register" $false)
    passwordSource = [string](Get-JsonValue $taskPreview "passwordSource" "")
    inputs = [ordered]@{
        healthPath = $resolvedHealthPath
        statusPath = $resolvedStatusPath
        taskPreviewPath = $resolvedTaskPreviewPath
    }
}
$dashboard.summary = [ordered]@{
    readiness = if (-not $dashboard.ok) {
        "blocked"
    } elseif ($planOnly) {
        "ready"
    } else {
        "verified"
    }
    operatorAction = if (-not $dashboard.ok) {
        "Review failed checks, sensitive hits, and task warnings before trusting this dashboard."
    } elseif ($planOnly) {
        "Run the database health task without PlanOnly and provide QTNETWORKCHAT_PGPASSWORD from the environment to verify live database health."
    } elseif ($queryFailureCount -gt 0) {
        "Investigate query failures before promoting this dashboard to release readiness."
    } elseif ($slowQueryCount -gt 0) {
        "Review slow queries and pool thresholds before promoting this dashboard to release readiness."
    } else {
        "Archive the redacted dashboard for release readiness review."
    }
}
$dashboard.auditSummary = [ordered]@{
    releaseGate = if (-not $dashboard.ok) {
        "blocked"
    } elseif ($planOnly) {
        "await-live-health-check"
    } elseif ($queryFailureCount -gt 0) {
        "review-query-failures"
    } elseif ($slowQueryCount -gt 0) {
        "review-slow-queries"
    } else {
        "can-review-health-evidence"
    }
    evidenceBundle = @("dashboard-json", "dashboard-markdown", "query-metrics")
    auditFocus = @($warnings | ForEach-Object { [string]$_ })
}
if ($dashboard.auditSummary.auditFocus.Count -eq 0) {
    $dashboard.auditSummary.auditFocus = @("routine-health-review")
}

$dashboardParent = Split-Path -Parent $resolvedDashboardPath
if (-not [string]::IsNullOrWhiteSpace($dashboardParent)) {
    New-Item -ItemType Directory -Path $dashboardParent -Force | Out-Null
}
$dashboard | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedDashboardPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    $markdownParent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
        New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat Database Health Dashboard")
    $lines.Add("")
    $lines.Add(('- Generated at: `{0}`' -f $dashboard.generatedAt))
    $lines.Add(('- Status: `{0}`' -f $dashboard.status))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $dashboard.ok)))
    $lines.Add(('- Plan only: `{0}`' -f (Format-Value $dashboard.planOnly)))
    $lines.Add(('- Driver: `{0}`' -f (Format-Value $dashboard.driver)))
    $lines.Add(('- Checks: `{0}`' -f $dashboard.checkCount))
    $lines.Add(('- Failed checks: `{0}`' -f (@($dashboard.failedChecks) -join ", ")))
    $lines.Add(('- Slow query threshold ms: `{0}`' -f $dashboard.queryMetrics.slowQueryThresholdMs))
    $lines.Add(('- Slow query count: `{0}`' -f $dashboard.queryMetrics.slowQueryCount))
    $lines.Add(('- Query failure count: `{0}`' -f $dashboard.queryMetrics.queryFailureCount))
    $lines.Add(('- Last error reason: `{0}`' -f (Format-Value $dashboard.queryMetrics.lastErrorReason)))
    $lines.Add(('- Last error check: `{0}`' -f (Format-Value $dashboard.queryMetrics.lastErrorCheck)))
    $lines.Add(('- Last error sample: `{0}`' -f (Format-Value $dashboard.queryMetrics.lastErrorSample)))
    $lines.Add(('- Readiness: `{0}`' -f (Format-Value $dashboard.summary.readiness)))
    $lines.Add(('- Operator action: `{0}`' -f (Format-Value $dashboard.summary.operatorAction)))
    $lines.Add(('- Release gate: `{0}`' -f (Format-Value $dashboard.auditSummary.releaseGate)))
    $lines.Add(('- Pool enabled: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.poolEnabled)))
    $lines.Add(('- Pool max connections: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.maxConnections)))
    $lines.Add(('- Pooled connections: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.pooledConnections)))
    $lines.Add(('- Pooled connection thread count: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.pooledConnectionThreadCount)))
    $lines.Add(('- Peak pooled connections: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.peakPooledConnections)))
    $lines.Add(('- Idle connections closed: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.idleConnectionsClosed)))
    $lines.Add(('- Overflow connections closed: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.overflowConnectionsClosed)))
    $lines.Add(('- Cross-thread checkout prevented: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.crossThreadCheckoutPrevented)))
    $lines.Add(('- Cross-thread release detected: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.crossThreadReleaseDetected)))
    $lines.Add(('- Pool idle ms: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.idleMs)))
    $lines.Add(('- Reconnect backoff ms: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.backoffMs)))
    $lines.Add(('- Thread connection ownership: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.threadPolicy.connectionOwnership)))
    $lines.Add(('- Thread cross reuse: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.threadPolicy.crossThreadReuse)))
    $lines.Add(('- Thread release scope: `{0}`' -f (Format-Value $dashboard.reconnectPolicy.threadPolicy.releaseScope)))
    $lines.Add(('- Warnings: `{0}`' -f $dashboard.warningCount))
    $lines.Add(('- Sensitive hits: `{0}`' -f @($dashboard.sensitiveHits).Count))
    $lines.Add(('- Task configured: `{0}`' -f (Format-Value $dashboard.taskConfigured)))
    $lines.Add(('- Password source: `{0}`' -f (Format-Value $dashboard.passwordSource)))
    $lines.Add(('- Audit focus: `{0}`' -f (@($dashboard.auditSummary.auditFocus) -join ", ")))
    if ($dashboard.warnings.Count -gt 0) {
        $lines.Add("")
        $lines.Add("## Warnings")
        foreach ($warning in $dashboard.warnings) {
            $lines.Add(('- `{0}`' -f $warning))
        }
    }
    $lines.Add("")
    $lines.Add("This dashboard is generated from local redacted health artifacts only. It does not connect to PostgreSQL, Redis, S3, or MinIO, and it does not modify application data.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "database health dashboard"
Write-Host ("  status: {0}" -f $dashboard.status)
Write-Host ("  dashboard: {0}" -f $resolvedDashboardPath)
if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
}

if ($FailOnUnhealthy -and -not $dashboard.ok) {
    exit 2
}
