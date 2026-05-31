param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$AggregatedPath,

    [switch]$NoFailOnWarning
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    throw "OutputDir is required."
}
if (-not (Test-Path -LiteralPath $OutputDir)) {
    throw ("OutputDir not found: {0}" -f $OutputDir)
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)

$knownAlertFiles = @(
    "large-file-route-alert-summary.json",
    "s3-request-results-alert-summary.json",
    "s3-failure-batch-alert-summary.json",
    "s3-real-backend-evidence-alert-summary.json",
    "s3-stability-runbook-alert-summary.json",
    "receipt-rotation-alert-summary.json"
)

$alerts = New-Object System.Collections.Generic.List[object]
$totalWarnings = 0
$allOk = $true

foreach ($fileName in $knownAlertFiles) {
    $filePath = Join-Path $resolvedOutputDir $fileName
    if (-not (Test-Path -LiteralPath $filePath)) {
        continue
    }
    $raw = Get-Content -LiteralPath $filePath -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        continue
    }
    $parsed = $raw | ConvertFrom-Json
    if ($null -eq $parsed) {
        continue
    }
    if ($null -eq $parsed.kind -or $null -eq $parsed.ok) {
        continue
    }
    $warnCount = 0
    if ($null -ne $parsed.warnings) {
        $warnCount = @($parsed.warnings).Count
    }
    $totalWarnings += $warnCount
    if (-not $parsed.ok) {
        $allOk = $false
    }
    $alerts.Add([pscustomobject]@{
        kind     = $parsed.kind
        ok       = [bool]$parsed.ok
        warnings = @($parsed.warnings)
        metrics  = $parsed.metrics
        source   = $fileName
    })
}

$alertArray = @($alerts.ToArray())
$aggregated = [pscustomobject]@{
    ok             = [bool]$allOk
    totalWarnings  = [int]$totalWarnings
    alertCount     = [int]$alerts.Count
    alerts         = $alertArray
}

if (-not [string]::IsNullOrWhiteSpace($AggregatedPath)) {
    $resolvedAggregatedPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($AggregatedPath)
    $aggParent = Split-Path -Parent $resolvedAggregatedPath
    if (-not [string]::IsNullOrWhiteSpace($aggParent)) {
        New-Item -ItemType Directory -Path $aggParent -Force | Out-Null
    }
    $aggregated | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resolvedAggregatedPath -Encoding UTF8
    Write-Host ("aggregated alert summary written: {0}" -f $resolvedAggregatedPath)
}

Write-Host ""
Write-Host "governance alert overview"
Write-Host ("  sources found: {0}" -f $alerts.Count)
Write-Host ("  total warnings: {0}" -f $totalWarnings)
Write-Host ("  overall ok: {0}" -f $allOk)

if (-not $allOk) {
    Write-Host ""
    Write-Host "warnings by kind:"
    foreach ($alert in $alerts) {
        if (-not $alert.ok) {
            Write-Host ("  [{0}] {1} warning(s)" -f $alert.kind, $alert.warnings.Count)
            foreach ($w in $alert.warnings) {
                Write-Host ("    - {0}" -f $w)
            }
        }
    }
    if (-not $NoFailOnWarning) {
        throw "Governance alert aggregation found warnings."
    }
}
