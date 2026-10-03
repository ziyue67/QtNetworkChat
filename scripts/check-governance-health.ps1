param(
    [Parameter(Mandatory = $true)]
    [string]$AlertOverviewPath,

    [string]$HealthOutputPath,

    [int]$MaxWarnings = 0,

    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($AlertOverviewPath)) {
    throw "AlertOverviewPath is required."
}
if (-not (Test-Path -LiteralPath $AlertOverviewPath)) {
    $health = [pscustomobject]@{
        status       = "unknown"
        reason       = "alert overview file not found"
        path         = $AlertOverviewPath
        checkedAt    = (Get-Date).ToUniversalTime().ToString("o")
        ok           = $false
        totalWarnings = -1
        alertCount   = -1
    }
    if (-not [string]::IsNullOrWhiteSpace($HealthOutputPath)) {
        $resolvedHealth = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HealthOutputPath)
        $healthParent = Split-Path -Parent $resolvedHealth
        if (-not [string]::IsNullOrWhiteSpace($healthParent)) {
            New-Item -ItemType Directory -Path $healthParent -Force | Out-Null
        }
        $health | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $resolvedHealth -Encoding UTF8
    }
    if (-not $Quiet) {
        Write-Host "UNKNOWN: alert overview not found"
    }
    exit 2
}

$raw = Get-Content -LiteralPath $AlertOverviewPath -Raw -Encoding UTF8
if ([string]::IsNullOrWhiteSpace($raw)) {
    throw "AlertOverviewPath is empty."
}
$overview = $raw | ConvertFrom-Json
if ($null -eq $overview -or $null -eq $overview.ok) {
    throw "AlertOverviewPath does not contain a valid governance alert overview."
}

$totalWarnings = 0
if ($null -ne $overview.totalWarnings) {
    $totalWarnings = [int]$overview.totalWarnings
}
$alertCount = 0
if ($null -ne $overview.alertCount) {
    $alertCount = [int]$overview.alertCount
}

$isHealthy = [bool]$overview.ok -and ($totalWarnings -le $MaxWarnings)

$status = if ($isHealthy) { "healthy" } else { "unhealthy" }
$reasons = New-Object System.Collections.Generic.List[string]
if (-not $overview.ok) {
    $reasons.Add("overview.ok=false")
}
if ($totalWarnings -gt $MaxWarnings) {
    $reasons.Add(("totalWarnings={0} exceeds max={1}" -f $totalWarnings, $MaxWarnings))
}
$reason = if ($reasons.Count -eq 0) { "all checks passed" } else { $reasons -join "; " }

$health = [pscustomobject]@{
    status        = $status
    reason        = $reason
    path          = $AlertOverviewPath
    checkedAt     = (Get-Date).ToUniversalTime().ToString("o")
    ok            = $isHealthy
    totalWarnings = $totalWarnings
    alertCount    = $alertCount
}

if (-not [string]::IsNullOrWhiteSpace($HealthOutputPath)) {
    $resolvedHealth = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HealthOutputPath)
    $healthParent = Split-Path -Parent $resolvedHealth
    if (-not [string]::IsNullOrWhiteSpace($healthParent)) {
        New-Item -ItemType Directory -Path $healthParent -Force | Out-Null
    }
    $health | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $resolvedHealth -Encoding UTF8
}

if (-not $Quiet) {
    Write-Host ("governance health: {0}" -f $status)
    Write-Host ("  reason: {0}" -f $reason)
    Write-Host ("  warnings: {0}" -f $totalWarnings)
    Write-Host ("  alert sources: {0}" -f $alertCount)
}

if ($isHealthy) {
    exit 0
} else {
    exit 1
}
