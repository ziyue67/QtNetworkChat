param(
    [Parameter(Mandatory = $true)]
    [string]$GovernanceDir,

    [Parameter(Mandatory = $true)]
    [string]$DashboardPath,

    [string]$MarkdownPath,

    [string]$Title = "QtNetworkChat Large File Governance Dashboard"
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

function Resolve-ExistingDirectory([string]$PathValue, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Container)) {
        throw ("{0} not found: {1}" -f $Label, $PathValue)
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

function New-Artifact([string]$Label, [string]$PathValue, [string]$BaseDir) {
    $exists = -not [string]::IsNullOrWhiteSpace($PathValue) -and (Test-Path -LiteralPath $PathValue -PathType Leaf)
    $relativePath = ""
    if ($exists) {
        Push-Location -LiteralPath $BaseDir
        try {
            $relativePath = (Resolve-Path -LiteralPath $PathValue -Relative).TrimStart('.', '\', '/')
        } finally {
            Pop-Location
        }
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            $relativePath = Split-Path -Leaf $PathValue
        }
    }
    [pscustomobject]@{
        label        = $Label
        exists       = $exists
        relativePath = $relativePath
    }
}

function Add-Metric([System.Collections.IDictionary]$Metrics, [string]$Name, [object]$Value) {
    if ($null -ne $Value) {
        $Metrics[$Name] = $Value
    }
}

function Format-Value([object]$Value) {
    if ($null -eq $Value) {
        return "n/a"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    return [string]$Value
}

$resolvedGovernanceDir = Resolve-ExistingDirectory $GovernanceDir "GovernanceDir"
$resolvedDashboardPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DashboardPath)
$dashboardParent = Split-Path -Parent $resolvedDashboardPath
if (-not [string]::IsNullOrWhiteSpace($dashboardParent)) {
    New-Item -ItemType Directory -Path $dashboardParent -Force | Out-Null
}

$overviewPath = Join-Path $resolvedGovernanceDir "governance-alert-overview.json"
$healthPath = Join-Path $resolvedGovernanceDir "last-health.json"
$routeSummaryPath = Join-Path $resolvedGovernanceDir "large-file-route-summary.json"
$routeAlertPath = Join-Path $resolvedGovernanceDir "large-file-route-alert-summary.json"
$s3SummaryPath = Join-Path $resolvedGovernanceDir "s3-request-results-summary.json"
$s3AlertPath = Join-Path $resolvedGovernanceDir "s3-request-results-alert-summary.json"
$s3RunbookPath = Join-Path $resolvedGovernanceDir "s3-stability-runbook.json"
$s3RunbookMarkdownPath = Join-Path $resolvedGovernanceDir "s3-stability-runbook.md"
$s3RunbookAlertPath = Join-Path $resolvedGovernanceDir "s3-stability-runbook-alert-summary.json"
$rotationSummaryPath = Join-Path $resolvedGovernanceDir "receipt-rotation-summary.json"
$rotationAlertPath = Join-Path $resolvedGovernanceDir "receipt-rotation-alert-summary.json"
$reconcileSummaryPath = Join-Path (Join-Path $resolvedGovernanceDir "reconcile") "reconcile-summary.json"
$reportPath = Join-Path $resolvedGovernanceDir "large-file-governance-report.md"
$htmlReportPath = Join-Path $resolvedGovernanceDir "large-file-governance-report.html"
$acceptancePackagePath = Join-Path (Join-Path $resolvedGovernanceDir "acceptance-package") "large-file-acceptance.zip"
$diagnosticsPackagePath = Join-Path (Join-Path $resolvedGovernanceDir "diagnostics-package") "large-file-governance-diagnostics.zip"

$scanPaths = @(
    $overviewPath,
    $healthPath,
    $routeSummaryPath,
    $routeAlertPath,
    $s3SummaryPath,
    $s3AlertPath,
    $s3RunbookPath,
    $s3RunbookMarkdownPath,
    $s3RunbookAlertPath,
    $rotationSummaryPath,
    $rotationAlertPath,
    $reconcileSummaryPath,
    $reportPath,
    $htmlReportPath
)
$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; governance dashboard was not created."
}

$overview = Read-JsonFile $overviewPath
$health = Read-JsonFile $healthPath
$routeSummary = Read-JsonFile $routeSummaryPath
$s3Summary = Read-JsonFile $s3SummaryPath
$s3Runbook = Read-JsonFile $s3RunbookPath
$rotationSummary = Read-JsonFile $rotationSummaryPath
$reconcileSummary = Read-JsonFile $reconcileSummaryPath

$metrics = [ordered]@{}
Add-Metric $metrics "routeLines" (Get-JsonValue $routeSummary "routeLineCount")
Add-Metric $metrics "routeKeys" (Get-JsonValue $routeSummary "routeKeys")
Add-Metric $metrics "s3Lines" (Get-JsonValue $s3Summary "s3LineCount")
Add-Metric $metrics "s3Timeouts" (Get-JsonValue $s3Summary "timeoutCount")
Add-Metric $metrics "s3AuthFailures" (Get-JsonValue $s3Summary "authCount")
Add-Metric $metrics "s3TlsFailures" (Get-JsonValue $s3Summary "tlsCount")
Add-Metric $metrics "s3CoverageAreas" (Get-JsonValue (Get-JsonValue $s3Runbook "metrics" ([pscustomobject]@{})) "coverageAreaCount")
Add-Metric $metrics "s3CoverageFixedReasons" (Get-JsonValue (Get-JsonValue $s3Runbook "metrics" ([pscustomobject]@{})) "coverageFixedReasonCount")
Add-Metric $metrics "s3CoverageGaps" (Get-JsonValue (Get-JsonValue $s3Runbook "metrics" ([pscustomobject]@{})) "coverageGapCount")
Add-Metric $metrics "s3CoverageActionableGaps" (Get-JsonValue (Get-JsonValue $s3Runbook "metrics" ([pscustomobject]@{})) "coverageActionableGapCount")
Add-Metric $metrics "reconcileCleaned" (Get-JsonValue $reconcileSummary "cleanedCount")
Add-Metric $metrics "reconcileRetained" (Get-JsonValue $reconcileSummary "retainedCount")
Add-Metric $metrics "receiptRetained" (Get-JsonValue $rotationSummary "retainedRecords")
Add-Metric $metrics "receiptArchived" (Get-JsonValue $rotationSummary "archivedRecords")

$alerts = @()
if ($null -ne $overview -and $null -ne $overview.alerts) {
    foreach ($alert in @($overview.alerts)) {
        $alerts += [pscustomobject]@{
            kind     = Get-JsonValue $alert "kind" ""
            ok       = [bool](Get-JsonValue $alert "ok" $false)
            warnings = @((Get-JsonValue $alert "warnings" @()))
            source   = Get-JsonValue $alert "source" ""
        }
    }
}

$artifacts = @()
$artifacts += New-Artifact "alert overview" $overviewPath $resolvedGovernanceDir
$artifacts += New-Artifact "health" $healthPath $resolvedGovernanceDir
$artifacts += New-Artifact "route summary" $routeSummaryPath $resolvedGovernanceDir
$artifacts += New-Artifact "s3 summary" $s3SummaryPath $resolvedGovernanceDir
$artifacts += New-Artifact "s3 stability runbook" $s3RunbookPath $resolvedGovernanceDir
$artifacts += New-Artifact "s3 stability runbook markdown" $s3RunbookMarkdownPath $resolvedGovernanceDir
$artifacts += New-Artifact "receipt rotation summary" $rotationSummaryPath $resolvedGovernanceDir
$artifacts += New-Artifact "reconcile summary" $reconcileSummaryPath $resolvedGovernanceDir
$artifacts += New-Artifact "markdown report" $reportPath $resolvedGovernanceDir
$artifacts += New-Artifact "html report" $htmlReportPath $resolvedGovernanceDir
$artifacts += New-Artifact "acceptance package" $acceptancePackagePath $resolvedGovernanceDir
$artifacts += New-Artifact "diagnostics package" $diagnosticsPackagePath $resolvedGovernanceDir

$dashboard = [pscustomobject]@{
    format        = "qtnetworkchat-large-file-governance-dashboard-v1"
    generatedAt   = (Get-Date).ToUniversalTime().ToString("o")
    title         = $Title
    readOnly      = $true
    status        = Format-Value (Get-JsonValue $health "status" "unknown")
    ok            = [bool](Get-JsonValue $health "ok" $false)
    reason        = Format-Value (Get-JsonValue $health "reason" "health file missing")
    totalWarnings = [int](Get-JsonValue $overview "totalWarnings" 0)
    alertCount    = [int](Get-JsonValue $overview "alertCount" 0)
    metrics       = [pscustomobject]$metrics
    s3StabilizationCoverage = @((Get-JsonValue $s3Runbook "stabilizationCoverage" @()))
    s3CoverageGapAreas = @((Get-JsonValue $s3Runbook "coverageGapAreas" @()))
    s3CoverageActionableGapAreas = @((Get-JsonValue $s3Runbook "coverageActionableGapAreas" @()))
    s3CoveragePolicy = Get-JsonValue $s3Runbook "coveragePolicy" ([pscustomobject]@{})
    alerts        = $alerts
    artifacts     = $artifacts
    sensitiveHits = [int]$sensitiveHits.Count
}

$dashboard | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedDashboardPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MarkdownPath)
    $markdownParent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
        New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(("# {0}" -f $Title))
    $lines.Add("")
    $lines.Add(('- Generated at: `{0}`' -f $dashboard.generatedAt))
    $lines.Add('- Read only: `true`')
    $lines.Add(('- Status: `{0}`' -f $dashboard.status))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $dashboard.ok)))
    $lines.Add(('- Reason: `{0}`' -f $dashboard.reason))
    $lines.Add(('- Total warnings: `{0}`' -f $dashboard.totalWarnings))
    $lines.Add("")
    $lines.Add("## Metrics")
    $lines.Add("")
    $lines.Add("| Metric | Value |")
    $lines.Add("| --- | --- |")
    foreach ($key in $metrics.Keys) {
        $lines.Add(("| {0} | {1} |" -f $key, (Format-Value $metrics[$key])))
    }
    if ($dashboard.s3StabilizationCoverage.Count -gt 0) {
        $lines.Add("")
        $lines.Add("## S3 Stabilization Coverage")
        $lines.Add("")
        $lines.Add("| Area | Event | Operation | Fixed reasons | Observed event operations |")
        $lines.Add("| --- | --- | --- | --- | ---: |")
        foreach ($item in $dashboard.s3StabilizationCoverage) {
            $fixedReasons = @((Get-JsonValue $item "fixedReasons" @())) -join ", "
            $lines.Add(("| {0} | {1} | {2} | {3} | {4} |" -f
                    (Get-JsonValue $item "area" ""),
                    (Get-JsonValue $item "event" ""),
                    (Get-JsonValue $item "operation" ""),
                    $fixedReasons,
                    (Get-JsonValue $item "observedEventOperationCount" 0)))
        }
        if ($dashboard.s3CoverageGapAreas.Count -gt 0) {
            $lines.Add("")
            $gapText = @($dashboard.s3CoverageGapAreas) -join ", "
            $lines.Add(("- Coverage gaps: {0}" -f $gapText))
        }
        if ($dashboard.s3CoverageActionableGapAreas.Count -gt 0) {
            $actionableGapText = @($dashboard.s3CoverageActionableGapAreas) -join ", "
            $lines.Add(("- Actionable coverage gaps: {0}" -f $actionableGapText))
        }
    }
    $lines.Add("")
    $lines.Add("## Alerts")
    $lines.Add("")
    $lines.Add("| Kind | OK | Warnings | Source |")
    $lines.Add("| --- | --- | --- | --- |")
    foreach ($alert in $alerts) {
        $warningText = if ($null -eq $alert.warnings -or @($alert.warnings).Count -eq 0) { "" } else { (@($alert.warnings) -join "; ") }
        $lines.Add(("| {0} | {1} | {2} | {3} |" -f $alert.kind, (Format-Value $alert.ok), $warningText, $alert.source))
    }
    $lines.Add("")
    $lines.Add("## Artifacts")
    $lines.Add("")
    $lines.Add("| Artifact | Exists | Path |")
    $lines.Add("| --- | --- | --- |")
    foreach ($artifact in $artifacts) {
        $lines.Add(("| {0} | {1} | {2} |" -f $artifact.label, (Format-Value $artifact.exists), $artifact.relativePath))
    }
    $lines.Add("")
    $lines.Add("This dashboard is generated from local governance artifacts only. It does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "large file governance dashboard"
Write-Host ("  json: {0}" -f $resolvedDashboardPath)
if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
}
Write-Host ("  status: {0}" -f $dashboard.status)
Write-Host ("  sensitive hits: {0}" -f $dashboard.sensitiveHits)
