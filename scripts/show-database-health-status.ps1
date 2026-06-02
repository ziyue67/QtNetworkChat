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
    }
    sensitiveHits = @($sensitiveHits)
    checks = $checks
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
    $lines.Add(("| sensitiveHits | {0} |" -f $summary.sensitiveHits.Count))
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
