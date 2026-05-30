param(
    [Parameter(Mandatory = $true)]
    [string]$GovernanceDir,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [string]$HtmlReportPath,

    [string]$Title = "QtNetworkChat Large File Governance Report"
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

function Format-Value([object]$Value) {
    if ($null -eq $Value) {
        return "n/a"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    return [string]$Value
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

function Add-MetricTable([System.Collections.Generic.List[string]]$Lines, [string]$Heading, [hashtable]$Metrics) {
    if ($null -eq $Metrics -or $Metrics.Count -eq 0) {
        return
    }
    $Lines.Add("")
    $Lines.Add(("## {0}" -f $Heading))
    $Lines.Add("")
    $Lines.Add("| Metric | Value |")
    $Lines.Add("| --- | --- |")
    foreach ($key in ($Metrics.Keys | Sort-Object)) {
        $Lines.Add(("| {0} | {1} |" -f $key, (Format-Value $Metrics[$key])))
    }
}

$resolvedGovernanceDir = Resolve-ExistingDirectory $GovernanceDir "GovernanceDir"
$resolvedReportPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ReportPath)
$reportParent = Split-Path -Parent $resolvedReportPath
if (-not [string]::IsNullOrWhiteSpace($reportParent)) {
    New-Item -ItemType Directory -Path $reportParent -Force | Out-Null
}

$overviewPath = Join-Path $resolvedGovernanceDir "governance-alert-overview.json"
$healthPath = Join-Path $resolvedGovernanceDir "last-health.json"
$routeSummaryPath = Join-Path $resolvedGovernanceDir "large-file-route-summary.json"
$s3SummaryPath = Join-Path $resolvedGovernanceDir "s3-request-results-summary.json"
$rotationSummaryPath = Join-Path $resolvedGovernanceDir "receipt-rotation-summary.json"
$reconcileSummaryPath = Join-Path (Join-Path $resolvedGovernanceDir "reconcile") "reconcile-summary.json"

$overview = Read-JsonFile $overviewPath
$health = Read-JsonFile $healthPath
$routeSummary = Read-JsonFile $routeSummaryPath
$s3Summary = Read-JsonFile $s3SummaryPath
$rotationSummary = Read-JsonFile $rotationSummaryPath
$reconcileSummary = Read-JsonFile $reconcileSummaryPath

$scanPaths = @(
    $overviewPath,
    $healthPath,
    $routeSummaryPath,
    $s3SummaryPath,
    $rotationSummaryPath,
    $reconcileSummaryPath
)
$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; governance report was not created."
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add(("# {0}" -f $Title))
$lines.Add("")
$lines.Add(('- Generated at: `{0}`' -f (Get-Date).ToUniversalTime().ToString("o")))
$lines.Add('- Read only: `true`')
$lines.Add(('- Sensitive hits: `{0}`' -f $sensitiveHits.Count))
$lines.Add("")
$lines.Add("## Health")
$lines.Add("")
$lines.Add("| Field | Value |")
$lines.Add("| --- | --- |")
$lines.Add(("| status | {0} |" -f (Format-Value $health.status)))
$lines.Add(("| reason | {0} |" -f (Format-Value $health.reason)))
$lines.Add(("| ok | {0} |" -f (Format-Value $health.ok)))
$lines.Add(("| totalWarnings | {0} |" -f (Format-Value $health.totalWarnings)))
$lines.Add(("| alertCount | {0} |" -f (Format-Value $health.alertCount)))
$lines.Add("")
$lines.Add("## Alert Overview")
$lines.Add("")
$lines.Add("| Field | Value |")
$lines.Add("| --- | --- |")
$lines.Add(("| ok | {0} |" -f (Format-Value $overview.ok)))
$lines.Add(("| totalWarnings | {0} |" -f (Format-Value $overview.totalWarnings)))
$lines.Add(("| alertCount | {0} |" -f (Format-Value $overview.alertCount)))

if ($null -ne $overview -and $null -ne $overview.alerts) {
    $lines.Add("")
    $lines.Add("## Alert Sources")
    $lines.Add("")
    $lines.Add("| Kind | OK | Warnings | Source |")
    $lines.Add("| --- | --- | --- | --- |")
    foreach ($alert in @($overview.alerts)) {
        $warningText = if ($null -eq $alert.warnings -or @($alert.warnings).Count -eq 0) { "" } else { (@($alert.warnings) -join "; ") }
        $lines.Add(("| {0} | {1} | {2} | {3} |" -f (Format-Value $alert.kind), (Format-Value $alert.ok), $warningText, (Format-Value $alert.source)))
    }
}

$routeMetrics = @{}
if ($null -ne $routeSummary) {
    foreach ($name in @("routeLineCount", "routeKeys", "sensitiveHits", "failedCount", "deliveredCount", "fallbackCount")) {
        if ($routeSummary.PSObject.Properties.Name -contains $name) {
            $routeMetrics[$name] = $routeSummary.$name
        }
    }
}
Add-MetricTable $lines "Route Summary" $routeMetrics

$s3Metrics = @{}
if ($null -ne $s3Summary) {
    foreach ($name in @("routeLineCount", "s3LineCount", "sensitiveHits", "timeoutCount", "retryableCount", "authCount", "tlsCount", "hashCount", "sizeCount")) {
        if ($s3Summary.PSObject.Properties.Name -contains $name) {
            $s3Metrics[$name] = $s3Summary.$name
        }
    }
}
Add-MetricTable $lines "S3 Request Summary" $s3Metrics

$rotationMetrics = @{}
if ($null -ne $rotationSummary) {
    foreach ($name in @("totalRecords", "retainedRecords", "archivedRecords", "deletedArchives", "sensitiveHits")) {
        if ($rotationSummary.PSObject.Properties.Name -contains $name) {
            $rotationMetrics[$name] = $rotationSummary.$name
        }
    }
}
Add-MetricTable $lines "Receipt Rotation Summary" $rotationMetrics

$reconcileMetrics = @{}
if ($null -ne $reconcileSummary) {
    foreach ($name in @("receiptCount", "fallbackCount", "cleanedCount", "retainedCount", "sensitiveHits")) {
        if ($reconcileSummary.PSObject.Properties.Name -contains $name) {
            $reconcileMetrics[$name] = $reconcileSummary.$name
        }
    }
}
Add-MetricTable $lines "Delivered Reconcile Summary" $reconcileMetrics

$lines.Add("")
$lines.Add("## Operator Notes")
$lines.Add("")
$lines.Add("- This report is generated from local governance artifacts only.")
$lines.Add("- It does not connect to Redis/S3/MinIO.")
$lines.Add("- It does not modify queues, attachments, objects, or receipt files.")
$lines.Add("- If the report cannot be generated because sensitive fields are detected, treat the source artifact as unsafe to share.")

$lines | Set-Content -LiteralPath $resolvedReportPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($HtmlReportPath)) {
    $resolvedHtmlPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($HtmlReportPath)
    $htmlParent = Split-Path -Parent $resolvedHtmlPath
    if (-not [string]::IsNullOrWhiteSpace($htmlParent)) {
        New-Item -ItemType Directory -Path $htmlParent -Force | Out-Null
    }
    $body = ($lines | ForEach-Object {
        $escaped = [System.Net.WebUtility]::HtmlEncode($_)
        if ($escaped.StartsWith("# ")) {
            "<h1>$($escaped.Substring(2))</h1>"
        } elseif ($escaped.StartsWith("## ")) {
            "<h2>$($escaped.Substring(3))</h2>"
        } elseif ($escaped.StartsWith("- ")) {
            "<p>$escaped</p>"
        } elseif ($escaped.StartsWith("|")) {
            "<pre>$escaped</pre>"
        } elseif ([string]::IsNullOrWhiteSpace($escaped)) {
            ""
        } else {
            "<p>$escaped</p>"
        }
    }) -join "`n"
    @(
        "<!doctype html>",
        "<html lang=""en"">",
        "<head><meta charset=""utf-8""><title>$([System.Net.WebUtility]::HtmlEncode($Title))</title>",
        "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:32px;line-height:1.5;color:#1f2937}h1,h2{color:#111827}pre{background:#f3f4f6;padding:8px;border-radius:4px;white-space:pre-wrap}</style>",
        "</head><body>",
        $body,
        "</body></html>"
    ) | Set-Content -LiteralPath $resolvedHtmlPath -Encoding UTF8
}

Write-Host "large file governance report"
Write-Host ("  markdown: {0}" -f $resolvedReportPath)
if (-not [string]::IsNullOrWhiteSpace($HtmlReportPath)) {
    Write-Host ("  html: {0}" -f $resolvedHtmlPath)
}
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)
