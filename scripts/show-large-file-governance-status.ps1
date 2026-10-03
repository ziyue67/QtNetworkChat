param(
    [string]$GovernanceDir,

    [string]$DashboardPath,

    [string]$JsonPath,

    [string]$MarkdownPath,

    [switch]$FailOnUnhealthy
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    "endpoint\s*=",
    "bucket\s*=",
    "objectUrl\s*=",
    "object-url\s*=",
    "https?://",
    "access[-_\s]?key",
    "secret[-_\s]?key",
    "session[-_\s]?token",
    "Authorization",
    "Credential",
    "Signature"
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Read-JsonFile([string]$PathValue) {
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
            if ($line -match $pattern) {
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

function Normalize-Bool([object]$Value, [bool]$DefaultValue = $false) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    if ($text -in @("true", "1", "yes", "on")) {
        return $true
    }
    if ($text -in @("false", "0", "no", "off")) {
        return $false
    }
    $DefaultValue
}

$resolvedGovernanceDir = Resolve-OptionalPath $GovernanceDir
$resolvedDashboardPath = Resolve-OptionalPath $DashboardPath
if ([string]::IsNullOrWhiteSpace($resolvedDashboardPath) -and -not [string]::IsNullOrWhiteSpace($resolvedGovernanceDir)) {
    $candidate = Join-Path $resolvedGovernanceDir "large-file-governance-dashboard.json"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $resolvedDashboardPath = $candidate
    }
}

$healthPath = ""
$overviewPath = ""
if (-not [string]::IsNullOrWhiteSpace($resolvedGovernanceDir)) {
    $healthPath = Join-Path $resolvedGovernanceDir "last-health.json"
    $overviewPath = Join-Path $resolvedGovernanceDir "governance-alert-overview.json"
}

$scanPaths = @($resolvedDashboardPath, $healthPath, $overviewPath) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; governance status was not displayed."
}

$dashboard = Read-JsonFile $resolvedDashboardPath
$health = Read-JsonFile $healthPath
$overview = Read-JsonFile $overviewPath
if ($null -eq $dashboard -and $null -eq $health -and $null -eq $overview) {
    throw "No governance dashboard, health, or alert overview input was found."
}

$metrics = Get-JsonValue $dashboard "metrics" ([pscustomobject]@{})
$performanceSummary = Get-JsonValue $dashboard "performanceSummary" $null
$s3StabilizationCoverage = @((Get-JsonValue $dashboard "s3StabilizationCoverage" @()))
$s3CoverageGapAreas = @((Get-JsonValue $dashboard "s3CoverageGapAreas" @()))
$s3CoverageActionableGapAreas = @((Get-JsonValue $dashboard "s3CoverageActionableGapAreas" @()))
$alerts = @((Get-JsonValue $dashboard "alerts" @()))
if ($alerts.Count -eq 0 -and $null -ne $overview) {
    $alerts = @((Get-JsonValue $overview "alerts" @()))
}

$status = [string](Get-JsonValue $dashboard "status" (Get-JsonValue $health "status" "unknown"))
$ok = Normalize-Bool (Get-JsonValue $dashboard "ok" (Get-JsonValue $health "ok" $false))
$reason = [string](Get-JsonValue $dashboard "reason" (Get-JsonValue $health "reason" "dashboard or health data missing"))
$totalWarnings = [int](Get-JsonValue $dashboard "totalWarnings" (Get-JsonValue $overview "totalWarnings" 0))
$alertCount = [int](Get-JsonValue $dashboard "alertCount" (Get-JsonValue $overview "alertCount" $alerts.Count))
$dashboardSummary = Get-JsonValue $dashboard "summary" $null
$dashboardAuditSummary = Get-JsonValue $dashboard "auditSummary" $null
$readiness = [string](Get-JsonValue $dashboardSummary "readiness" "")
if ([string]::IsNullOrWhiteSpace($readiness)) {
    $readiness = if (-not $ok) {
        "blocked"
    } elseif ($s3CoverageActionableGapAreas.Count -gt 0) {
        "review"
    } else {
        "verified"
    }
}
$operatorAction = [string](Get-JsonValue $dashboardSummary "operatorAction" "")
if ([string]::IsNullOrWhiteSpace($operatorAction)) {
    $operatorAction = if (-not $ok) {
        "Review governance alerts, health status, and diagnostics before trusting this status."
    } elseif ($s3CoverageActionableGapAreas.Count -gt 0) {
        "Review S3 coverage gaps before promoting large-file governance evidence."
    } else {
        "Archive the redacted governance status for release readiness review."
    }
}
$releaseGate = [string](Get-JsonValue $dashboardAuditSummary "releaseGate" "")
if ([string]::IsNullOrWhiteSpace($releaseGate)) {
    $releaseGate = if (-not $ok) {
        "blocked-governance-health"
    } elseif ($s3CoverageActionableGapAreas.Count -gt 0) {
        "review-s3-coverage-gaps"
    } else {
        "can-review-governance-evidence"
    }
}
$auditFocus = @((Get-JsonValue $dashboardAuditSummary "auditFocus" @()))
if ($auditFocus.Count -eq 0) {
    $auditFocus = if ($s3CoverageActionableGapAreas.Count -gt 0) {
        @($s3CoverageActionableGapAreas | ForEach-Object { "s3-coverage-gap:" + [string]$_ })
    } else {
        @("routine-governance-review")
    }
}

$warningSources = @()
foreach ($alert in $alerts) {
    $alertWarnings = @((Get-JsonValue $alert "warnings" @()))
    if ($alertWarnings.Count -gt 0 -or -not (Normalize-Bool (Get-JsonValue $alert "ok" $false))) {
        $warningSources += [pscustomobject]@{
            kind        = [string](Get-JsonValue $alert "kind" "")
            ok          = Normalize-Bool (Get-JsonValue $alert "ok" $false)
            warningCount = [int]$alertWarnings.Count
            warnings    = $alertWarnings
        }
    }
}

$summary = [pscustomobject]@{
    format         = "qtnetworkchat-large-file-governance-status-v1"
    generatedAt    = (Get-Date).ToUniversalTime().ToString("o")
    readOnly       = $true
    status         = $status
    ok             = $ok
    reason         = $reason
    totalWarnings  = $totalWarnings
    alertCount     = $alertCount
    summary        = [pscustomobject]@{
        readiness      = $readiness
        operatorAction = $operatorAction
    }
    auditSummary   = [pscustomobject]@{
        releaseGate = $releaseGate
        auditFocus  = @($auditFocus)
    }
    warningSources = $warningSources
    metrics        = $metrics
    s3StabilizationCoverage = $s3StabilizationCoverage
    s3CoverageGapAreas = $s3CoverageGapAreas
    s3CoverageActionableGapAreas = $s3CoverageActionableGapAreas
    sensitiveHits  = [int]$sensitiveHits.Count
    inputs         = [pscustomobject]@{
        governanceDir = $resolvedGovernanceDir
        dashboardPath = $resolvedDashboardPath
        healthPath    = $healthPath
        overviewPath  = $overviewPath
    }
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $resolvedJsonPath = Resolve-OptionalPath $JsonPath
    $jsonParent = Split-Path -Parent $resolvedJsonPath
    if (-not [string]::IsNullOrWhiteSpace($jsonParent)) {
        New-Item -ItemType Directory -Path $jsonParent -Force | Out-Null
    }
    $summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedJsonPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
    $markdownParent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
        New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat Large File Governance Status")
    $lines.Add("")
    $lines.Add(('- Status: `{0}`' -f $summary.status))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $summary.ok)))
    $lines.Add(('- Reason: `{0}`' -f $summary.reason))
    $lines.Add(('- Total warnings: `{0}`' -f $summary.totalWarnings))
    $lines.Add(('- Alert count: `{0}`' -f $summary.alertCount))
    $lines.Add(('- Readiness: `{0}`' -f $summary.summary.readiness))
    $lines.Add(('- Operator action: `{0}`' -f $summary.summary.operatorAction))
    $lines.Add(('- Release gate: `{0}`' -f $summary.auditSummary.releaseGate))
    $lines.Add("")
    $lines.Add("## Warning Sources")
    $lines.Add("")
    $lines.Add("| Kind | OK | Warning Count |")
    $lines.Add("| --- | --- | --- |")
    foreach ($source in $warningSources) {
        $lines.Add(("| {0} | {1} | {2} |" -f $source.kind, (Format-Value $source.ok), $source.warningCount))
    }
    if ($summary.s3StabilizationCoverage.Count -gt 0) {
        $lines.Add("")
        $lines.Add("## S3 Stabilization Coverage")
        $lines.Add("")
        $lines.Add("| Area | Event | Operation | Fixed Reason Count |")
        $lines.Add("| --- | --- | --- | ---: |")
        foreach ($item in $summary.s3StabilizationCoverage) {
            $fixedReasonCount = @((Get-JsonValue $item "fixedReasons" @())).Count
            $lines.Add(("| {0} | {1} | {2} | {3} |" -f
                    (Get-JsonValue $item "area" ""),
                    (Get-JsonValue $item "event" ""),
                    (Get-JsonValue $item "operation" ""),
                    $fixedReasonCount))
        }
        if ($summary.s3CoverageGapAreas.Count -gt 0) {
            $lines.Add("")
            $gapText = @($summary.s3CoverageGapAreas) -join ", "
            $lines.Add(("- Coverage gaps: {0}" -f $gapText))
        }
        if ($summary.s3CoverageActionableGapAreas.Count -gt 0) {
            $actionableGapText = @($summary.s3CoverageActionableGapAreas) -join ", "
            $lines.Add(("- Actionable coverage gaps: {0}" -f $actionableGapText))
        }
    }
    if ($null -ne $performanceSummary) {
        $lines.Add("")
        $lines.Add("## Performance Closeout")
        $lines.Add("")
        $lines.Add(('- Readiness: `{0}`' -f (Format-Value (Get-JsonValue (Get-JsonValue $performanceSummary "summary" $null) "readiness" "unknown"))))
        $lines.Add(('- Release gate: `{0}`' -f (Format-Value (Get-JsonValue (Get-JsonValue $performanceSummary "auditSummary" $null) "releaseGate" "unknown"))))
        $performanceBottlenecks = @((Get-JsonValue $performanceSummary "bottlenecks" @()))
        $lines.Add(('- Bottlenecks: `{0}`' -f ($(if ($performanceBottlenecks.Count -gt 0) { $performanceBottlenecks -join ", " } else { "none" }))))
    }
    $lines.Add("")
    $lines.Add("This status view is read-only. It does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "large file governance status"
Write-Host ("  status: {0}" -f $summary.status)
Write-Host ("  ok: {0}" -f (Format-Value $summary.ok))
Write-Host ("  reason: {0}" -f $summary.reason)
Write-Host ("  warnings: {0}" -f $summary.totalWarnings)
Write-Host ("  alert sources: {0}" -f $summary.alertCount)
Write-Host ("  readiness: {0}" -f $summary.summary.readiness)
Write-Host ("  release gate: {0}" -f $summary.auditSummary.releaseGate)
Write-Host ("  s3 coverage areas: {0}" -f $summary.s3StabilizationCoverage.Count)
Write-Host ("  s3 coverage gaps: {0}" -f $summary.s3CoverageGapAreas.Count)
Write-Host ("  s3 actionable coverage gaps: {0}" -f $summary.s3CoverageActionableGapAreas.Count)
Write-Host ("  sensitive hits: {0}" -f $summary.sensitiveHits)

if ($FailOnUnhealthy -and (-not $summary.ok -or $summary.status -ne "healthy")) {
    exit 2
}
