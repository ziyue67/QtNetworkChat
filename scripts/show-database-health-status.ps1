param(
    [string]$HealthPath,
    [string]$JsonPath,
    [string]$MarkdownPath,
    [switch]$FailOnUnhealthy
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'Authorization',
    'Credential',
    'Signature'
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Read-JsonFile([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw "HealthPath does not exist: $PathValue"
    }
    $raw = Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw "HealthPath is empty: $PathValue"
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
            if ($line -match $pattern -and $line -notmatch "<redacted>") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
    }
}

function Format-Value([object]$Value) {
    if ($null -eq $Value) {
        return "n/a"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    [string]$Value
}

$resolvedHealthPath = Resolve-OptionalPath $HealthPath
$health = Read-JsonFile $resolvedHealthPath

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $resolvedHealthPath $sensitiveHits

$checks = @()
foreach ($check in @(Get-JsonValue $health "checks" @())) {
    $checks += [pscustomobject]@{
        name = [string](Get-JsonValue $check "name" "")
        ok = [bool](Get-JsonValue $check "ok" $false)
        detail = [string](Get-JsonValue $check "detail" "")
        reason = [string](Get-JsonValue $check "reason" "")
    }
}

$config = Get-JsonValue $health "config" $null
$environment = Get-JsonValue $health "environment" $null
$driver = [string](Get-JsonValue $health "driver" (Get-JsonValue $config "driver" (Get-JsonValue $environment "QTNETWORKCHAT_DB_DRIVER" "unknown")))
$status = [string](Get-JsonValue $health "status" "unknown")
$ok = [bool](Get-JsonValue $health "ok" $false)
$queryMetrics = Get-JsonValue $health "queryMetrics" $null
$slowQueryThresholdMs = [int](Get-JsonValue $queryMetrics "slowQueryThresholdMs" 0)
$slowQueryCount = [int](Get-JsonValue $queryMetrics "slowQueryCount" 0)
$queryFailureCount = [int](Get-JsonValue $queryMetrics "queryFailureCount" 0)
$lastSlowQueryMs = [int](Get-JsonValue $queryMetrics "lastSlowQueryMs" 0)
$lastErrorReason = [string](Get-JsonValue $queryMetrics "lastErrorReason" "")
$lastErrorCheck = [string](Get-JsonValue $queryMetrics "lastErrorCheck" "")
$lastErrorSample = [string](Get-JsonValue $queryMetrics "lastErrorSample" "")
$reconnectPolicy = Get-JsonValue $health "reconnectPolicy" $null
$threadPolicy = Get-JsonValue $reconnectPolicy "threadPolicy" $null
if ($sensitiveHits.Count -gt 0) {
    $status = "unhealthy"
    $ok = $false
}

$summary = [ordered]@{
    format = "qtnetworkchat-database-health-status-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    sourcePath = $resolvedHealthPath
    sourceFormat = [string](Get-JsonValue $health "format" "")
    status = $status
    ok = $ok
    driver = $driver
    checkCount = $checks.Count
    failedChecks = @($checks | Where-Object { -not $_.ok } | ForEach-Object { $_.name })
    queryMetrics = [ordered]@{
        slowQueryThresholdMs = $slowQueryThresholdMs
        slowQueryCount = $slowQueryCount
        queryFailureCount = $queryFailureCount
        lastSlowQueryMs = $lastSlowQueryMs
        lastErrorReason = $lastErrorReason
        lastErrorCheck = $lastErrorCheck
        lastErrorSample = $lastErrorSample
    }
    reconnectPolicy = [ordered]@{
        poolEnabled = Get-JsonValue $reconnectPolicy "poolEnabled" $null
        maxConnections = Get-JsonValue $reconnectPolicy "maxConnections" $null
        idleMs = Get-JsonValue $reconnectPolicy "idleMs" $null
        backoffMs = Get-JsonValue $reconnectPolicy "backoffMs" $null
        slowQueryMs = Get-JsonValue $reconnectPolicy "slowQueryMs" $null
        circuitBreaker = Get-JsonValue $reconnectPolicy "circuitBreaker" ""
        threadPolicy = [ordered]@{
            connectionOwnership = Get-JsonValue $threadPolicy "connectionOwnership" ""
            crossThreadReuse = Get-JsonValue $threadPolicy "crossThreadReuse" $null
            checkoutScope = Get-JsonValue $threadPolicy "checkoutScope" ""
            idleReclaim = Get-JsonValue $threadPolicy "idleReclaim" ""
            guidance = Get-JsonValue $threadPolicy "guidance" ""
        }
    }
    sensitiveHits = @($sensitiveHits)
    checks = $checks
}
$auditFocus = New-Object System.Collections.Generic.List[string]
if ($queryFailureCount -gt 0) { [void]$auditFocus.Add("query-failures") }
if ($slowQueryCount -gt 0) { [void]$auditFocus.Add("slow-queries") }
if ($summary.failedChecks.Count -gt 0) { [void]$auditFocus.Add("failed-checks") }
if ($sensitiveHits.Count -gt 0) { [void]$auditFocus.Add("sensitive-fields") }
if ($auditFocus.Count -eq 0) { [void]$auditFocus.Add("routine-health-review") }
$summary.summary = [ordered]@{
    readiness = if ($summary.ok) { "verified" } else { "blocked" }
    operatorAction = if (-not $summary.ok) {
        "Review failed checks, sensitive hits, and last error evidence before trusting this health snapshot."
    } elseif ($queryFailureCount -gt 0) {
        "Investigate query failures before promoting this database health snapshot."
    } elseif ($slowQueryCount -gt 0) {
        "Review slow queries and pool thresholds before promoting this database health snapshot."
    } else {
        "Archive the redacted database health summary for release readiness review."
    }
}
$summary.auditSummary = [ordered]@{
    releaseGate = if (-not $summary.ok) {
        "blocked"
    } elseif ($queryFailureCount -gt 0) {
        "review-query-failures"
    } elseif ($slowQueryCount -gt 0) {
        "review-slow-queries"
    } else {
        "can-review-health-evidence"
    }
    evidenceBundle = @("status-json", "status-markdown", "query-metrics")
    auditFocus = @($auditFocus)
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $resolvedJsonPath = Resolve-OptionalPath $JsonPath
    $parent = Split-Path -Parent $resolvedJsonPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedJsonPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
    $parent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat Database Health")
    $lines.Add("")
    $lines.Add(("| status | {0} |" -f (Format-Value $summary.status)))
    $lines.Add(("| ok | {0} |" -f (Format-Value $summary.ok)))
    $lines.Add(("| driver | {0} |" -f (Format-Value $summary.driver)))
    $lines.Add(("| checkCount | {0} |" -f (Format-Value $summary.checkCount)))
    $lines.Add(("| slowQueryThresholdMs | {0} |" -f (Format-Value $summary.queryMetrics.slowQueryThresholdMs)))
    $lines.Add(("| slowQueryCount | {0} |" -f (Format-Value $summary.queryMetrics.slowQueryCount)))
    $lines.Add(("| queryFailureCount | {0} |" -f (Format-Value $summary.queryMetrics.queryFailureCount)))
    $lines.Add(("| lastErrorReason | {0} |" -f (Format-Value $summary.queryMetrics.lastErrorReason)))
    $lines.Add(("| lastErrorCheck | {0} |" -f (Format-Value $summary.queryMetrics.lastErrorCheck)))
    $lines.Add(("| lastErrorSample | {0} |" -f (Format-Value $summary.queryMetrics.lastErrorSample)))
    $lines.Add(("| readiness | {0} |" -f (Format-Value $summary.summary.readiness)))
    $lines.Add(("| operatorAction | {0} |" -f (Format-Value $summary.summary.operatorAction)))
    $lines.Add(("| releaseGate | {0} |" -f (Format-Value $summary.auditSummary.releaseGate)))
    $lines.Add(("| poolEnabled | {0} |" -f (Format-Value $summary.reconnectPolicy.poolEnabled)))
    $lines.Add(("| poolMaxConnections | {0} |" -f (Format-Value $summary.reconnectPolicy.maxConnections)))
    $lines.Add(("| poolIdleMs | {0} |" -f (Format-Value $summary.reconnectPolicy.idleMs)))
    $lines.Add(("| reconnectBackoffMs | {0} |" -f (Format-Value $summary.reconnectPolicy.backoffMs)))
    $lines.Add(("| threadConnectionOwnership | {0} |" -f (Format-Value $summary.reconnectPolicy.threadPolicy.connectionOwnership)))
    $lines.Add(("| threadCrossReuse | {0} |" -f (Format-Value $summary.reconnectPolicy.threadPolicy.crossThreadReuse)))
    $lines.Add(("| sensitiveHits | {0} |" -f $summary.sensitiveHits.Count))
    $lines.Add(("| auditFocus | {0} |" -f (@($summary.auditSummary.auditFocus) -join ", ")))
    $lines.Add("")
    $lines.Add("| check | ok | detail | reason |")
    $lines.Add("| --- | --- | --- | --- |")
    foreach ($check in $checks) {
        $lines.Add(("| {0} | {1} | {2} | {3} |" -f $check.name, (Format-Value $check.ok), (Format-Value $check.detail), (Format-Value $check.reason)))
    }
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

$summary | ConvertTo-Json -Depth 8
if ($FailOnUnhealthy -and -not $summary.ok) {
    exit 2
}
