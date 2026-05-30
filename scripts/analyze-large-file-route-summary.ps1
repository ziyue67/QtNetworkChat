param(
    [Parameter(Mandatory = $true)]
    [string[]]$SummaryPath,

    [int]$WarnFailedWithoutFallback = 0,

    [int]$WarnDeliveredWithoutCleanup = 0,

    [int]$WarnSensitiveHits = 0,

    [string]$AlertSummaryPath,

    [switch]$NoFailOnWarning
)

$ErrorActionPreference = "Stop"

function Get-Int64Field($Object, [string]$Name) {
    if ($null -eq $Object.PSObject.Properties[$Name]) {
        return 0L
    }
    $value = $Object.$Name
    if ($null -eq $value) {
        return 0L
    }
    $parsed = 0L
    if ([int64]::TryParse([string]$value, [ref]$parsed)) {
        return $parsed
    }
    0L
}

if ($WarnFailedWithoutFallback -lt 0) {
    throw "WarnFailedWithoutFallback must be 0 or greater."
}
if ($WarnDeliveredWithoutCleanup -lt 0) {
    throw "WarnDeliveredWithoutCleanup must be 0 or greater."
}
if ($WarnSensitiveHits -lt 0) {
    throw "WarnSensitiveHits must be 0 or greater."
}

$summaries = @()
foreach ($path in $SummaryPath) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Summary path not found: $path"
    }
    try {
        $summary = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw ("Invalid route summary JSON: {0}: {1}" -f $path, $_.Exception.Message)
    }
    $summaries += [pscustomobject]@{
        path = $path
        routeLineCount = Get-Int64Field $summary "routeLineCount"
        routeKeys = Get-Int64Field $summary "routeKeys"
        deliveredWithoutCleanup = Get-Int64Field $summary "deliveredWithoutCleanup"
        reconcileRetained = Get-Int64Field $summary "reconcileRetained"
        failedFallbackRetained = Get-Int64Field $summary "failedFallbackRetained"
        failedWithoutFallback = Get-Int64Field $summary "failedWithoutFallback"
        sensitiveHits = Get-Int64Field $summary "sensitiveHits"
    }
}

$routeLineCount = 0L
$routeKeys = 0L
$deliveredWithoutCleanup = 0L
$reconcileRetained = 0L
$failedFallbackRetained = 0L
$failedWithoutFallback = 0L
$sensitiveHits = 0L
$warnings = New-Object System.Collections.Generic.List[string]

foreach ($summary in $summaries) {
    $routeLineCount += $summary.routeLineCount
    $routeKeys += $summary.routeKeys
    $deliveredWithoutCleanup += $summary.deliveredWithoutCleanup
    $reconcileRetained += $summary.reconcileRetained
    $failedFallbackRetained += $summary.failedFallbackRetained
    $failedWithoutFallback += $summary.failedWithoutFallback
    $sensitiveHits += $summary.sensitiveHits

    if ($summary.sensitiveHits -gt $WarnSensitiveHits) {
        $warnings.Add(("{0}: sensitiveHits={1} exceeds threshold {2}" -f
                $summary.path, $summary.sensitiveHits, $WarnSensitiveHits))
    }
    if ($summary.failedWithoutFallback -gt $WarnFailedWithoutFallback) {
        $warnings.Add(("{0}: failedWithoutFallback={1} exceeds threshold {2}" -f
                $summary.path, $summary.failedWithoutFallback, $WarnFailedWithoutFallback))
    }
    if ($summary.deliveredWithoutCleanup -gt $WarnDeliveredWithoutCleanup) {
        $warnings.Add(("{0}: deliveredWithoutCleanup={1} exceeds threshold {2}" -f
                $summary.path, $summary.deliveredWithoutCleanup, $WarnDeliveredWithoutCleanup))
    }
}

Write-Host "large file route summaries"
Write-Host ("  files: {0}" -f $summaries.Count)
Write-Host ("  route lines: {0}" -f $routeLineCount)
Write-Host ("  route keys: {0}" -f $routeKeys)
Write-Host ("  failed fallback retained: {0}" -f $failedFallbackRetained)
Write-Host ("  failed without fallback: {0}" -f $failedWithoutFallback)
Write-Host ("  delivered without cleanup: {0}" -f $deliveredWithoutCleanup)
Write-Host ("  reconcile retained: {0}" -f $reconcileRetained)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits)

Write-Host ""
Write-Host "summary rows"
foreach ($summary in ($summaries | Sort-Object path)) {
    Write-Host ("  {0} failedWithoutFallback={1} failedFallbackRetained={2} deliveredWithoutCleanup={3} reconcileRetained={4} sensitiveHits={5}" -f
        $summary.path,
        $summary.failedWithoutFallback,
        $summary.failedFallbackRetained,
        $summary.deliveredWithoutCleanup,
        $summary.reconcileRetained,
        $summary.sensitiveHits)
}

if (-not [string]::IsNullOrWhiteSpace($AlertSummaryPath)) {
    $resolvedAlertSummaryPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($AlertSummaryPath)
    $alertParent = Split-Path -Parent $resolvedAlertSummaryPath
    if (-not [string]::IsNullOrWhiteSpace($alertParent)) {
        New-Item -ItemType Directory -Path $alertParent -Force | Out-Null
    }

    [pscustomobject]@{
        kind = "large-file-route-summary"
        ok = ($warnings.Count -eq 0)
        warnings = @($warnings)
        metrics = [pscustomobject]@{
            files = $summaries.Count
            routeLineCount = $routeLineCount
            routeKeys = $routeKeys
            failedFallbackRetained = $failedFallbackRetained
            failedWithoutFallback = $failedWithoutFallback
            deliveredWithoutCleanup = $deliveredWithoutCleanup
            reconcileRetained = $reconcileRetained
            sensitiveHits = $sensitiveHits
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resolvedAlertSummaryPath -Encoding UTF8
}

if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "warnings"
    foreach ($warning in $warnings) {
        Write-Host ("  {0}" -f $warning)
    }
    if (-not $NoFailOnWarning) {
        throw "Large file route summary warnings were found."
    }
}
