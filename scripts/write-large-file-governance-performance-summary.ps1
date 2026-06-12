param(
    [Parameter(Mandatory = $true)]
    [string]$GovernanceDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$MarkdownPath,

    [string]$Title = "QtNetworkChat Large File Governance Performance Summary"
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

function Resolve-OutputPath([string]$PathValue) {
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
    $parent = Split-Path -Parent $resolved
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $resolved
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

function Get-CountValue([object]$ObjectValue, [string]$Name) {
    [int](Get-JsonValue $ObjectValue $Name 0)
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
    return [string]$Value
}

function Get-RatioPercent([double]$Numerator, [double]$Denominator) {
    if ($Denominator -le 0) {
        return 100.0
    }
    [math]::Round(($Numerator / $Denominator) * 100.0, 1)
}

function Add-Artifact([System.Collections.ArrayList]$Artifacts, [string]$Label, [string]$PathValue, [string]$BaseDir) {
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
    [void]$Artifacts.Add([pscustomobject]@{
            label = $Label
            exists = $exists
            relativePath = $relativePath
        })
}

$resolvedGovernanceDir = Resolve-ExistingDirectory $GovernanceDir "GovernanceDir"
$resolvedOutputPath = Resolve-OutputPath $OutputPath
$resolvedMarkdownPath = if ([string]::IsNullOrWhiteSpace($MarkdownPath)) { "" } else { Resolve-OutputPath $MarkdownPath }

$healthPath = Join-Path $resolvedGovernanceDir "last-health.json"
$routeSummaryPath = Join-Path $resolvedGovernanceDir "large-file-route-summary.json"
$s3SummaryPath = Join-Path $resolvedGovernanceDir "s3-request-results-summary.json"
$runbookPath = Join-Path $resolvedGovernanceDir "s3-stability-runbook.json"
$rotationSummaryPath = Join-Path $resolvedGovernanceDir "receipt-rotation-summary.json"
$reconcileSummaryPath = Join-Path (Join-Path $resolvedGovernanceDir "reconcile") "reconcile-summary.json"

$scanPaths = @(
    $healthPath,
    $routeSummaryPath,
    $s3SummaryPath,
    $runbookPath,
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
    throw "Sensitive fields were found; governance performance summary was not created."
}

$health = Read-JsonFile $healthPath
$routeSummary = Read-JsonFile $routeSummaryPath
$s3Summary = Read-JsonFile $s3SummaryPath
$runbook = Read-JsonFile $runbookPath
$rotationSummary = Read-JsonFile $rotationSummaryPath
$reconcileSummary = Read-JsonFile $reconcileSummaryPath

$reasonCounts = Get-JsonValue $s3Summary "reasonCounts" ([pscustomobject]@{})
$runbookMetrics = Get-JsonValue $runbook "metrics" ([pscustomobject]@{})

$deliveredCleaned = Get-CountValue $routeSummary "deliveredCleaned"
$deliveredRetained = Get-CountValue $routeSummary "deliveredRetained"
$deliveredWithoutCleanup = Get-CountValue $routeSummary "deliveredWithoutCleanup"
$failedFallbackRetained = Get-CountValue $routeSummary "failedFallbackRetained"
$failedWithoutFallback = Get-CountValue $routeSummary "failedWithoutFallback"
$s3LineCount = Get-CountValue $s3Summary "s3LineCount"
$s3SuccessCount = Get-CountValue $reasonCounts "success"
$s3TimeoutCount = Get-CountValue $reasonCounts "timeout"
$s3NetworkCount = Get-CountValue $reasonCounts "network"
$s3RetryableCount = Get-CountValue $reasonCounts "retryable"
$s3ServerCount = Get-CountValue $reasonCounts "server"
$s3AuthCount = Get-CountValue $reasonCounts "auth"
$s3TlsCount = Get-CountValue $reasonCounts "tls"
$s3HashCount = Get-CountValue $reasonCounts "hash"
$s3SizeCount = Get-CountValue $reasonCounts "size"
$reconcileCleaned = Get-CountValue $reconcileSummary "cleanedCount"
$reconcileRetained = Get-CountValue $reconcileSummary "retainedCount"
$reconcileReceiptCount = Get-CountValue $reconcileSummary "receiptCount"
$reconcileFallbackCount = Get-CountValue $reconcileSummary "fallbackCount"
$receiptTotalRecords = Get-CountValue $rotationSummary "totalRecords"
$receiptRetainedRecords = Get-CountValue $rotationSummary "retainedRecords"
$receiptArchivedRecords = Get-CountValue $rotationSummary "archivedRecords"
$coverageAreaCount = Get-CountValue $runbookMetrics "coverageAreaCount"
$coverageGapCount = Get-CountValue $runbookMetrics "coverageGapCount"
$coverageActionableGapCount = Get-CountValue $runbookMetrics "coverageActionableGapCount"

$deliveredTouchCount = $deliveredCleaned + $deliveredRetained + $deliveredWithoutCleanup
$failedTouchCount = $failedFallbackRetained + $failedWithoutFallback
$reconcileTouchCount = $reconcileCleaned + $reconcileRetained
$s3TransientCount = $s3TimeoutCount + $s3NetworkCount + $s3RetryableCount + $s3ServerCount
$s3IdentityFailureCount = $s3AuthCount + $s3TlsCount
$s3IntegrityCount = $s3HashCount + $s3SizeCount

$deliveryClosurePercent = Get-RatioPercent $deliveredCleaned $deliveredTouchCount
$fallbackProtectionPercent = Get-RatioPercent $failedFallbackRetained $failedTouchCount
$reconcileRetentionPercent = Get-RatioPercent $reconcileRetained $reconcileTouchCount
$receiptArchivePercent = Get-RatioPercent $receiptArchivedRecords $receiptTotalRecords
$s3SuccessPercent = Get-RatioPercent $s3SuccessCount $s3LineCount
$s3TransientPercent = Get-RatioPercent $s3TransientCount $s3LineCount

$bottlenecks = New-Object System.Collections.Generic.List[string]
if ($deliveredWithoutCleanup -gt 0) {
    $bottlenecks.Add("delivery-cleanup-gaps")
}
if ($failedWithoutFallback -gt 0) {
    $bottlenecks.Add("fallback-protection-gaps")
}
if ($coverageActionableGapCount -gt 0) {
    $bottlenecks.Add("s3-actionable-coverage-gaps")
}
if ($s3TransientCount -gt 0) {
    $bottlenecks.Add("s3-transient-pressure")
}
if ($s3IdentityFailureCount -gt 0) {
    $bottlenecks.Add("s3-credential-or-tls-failures")
}
if ($s3IntegrityCount -gt 0) {
    $bottlenecks.Add("s3-integrity-failures")
}
if ($reconcileRetained -gt 0) {
    $bottlenecks.Add("reconcile-retained-follow-up")
}
if ($receiptArchivedRecords -gt 0) {
    $bottlenecks.Add("receipt-archive-pressure")
}

$healthOk = [bool](Get-JsonValue $health "ok" $false)
$summaryReadiness = if (-not $healthOk) {
    "blocked"
} elseif ($bottlenecks.Count -gt 0) {
    "review"
} else {
    "verified"
}
$releaseGate = if (-not $healthOk) {
    "blocked-governance-health"
} elseif ($bottlenecks.Count -gt 0) {
    "review-governance-performance"
} else {
    "can-review-governance-performance-summary"
}
$operatorAction = if (-not $healthOk) {
    "Review governance health and alert outputs before trusting performance closeout metrics."
} elseif ($bottlenecks.Count -gt 0) {
    "Review delivery closure, fallback protection, S3 pressure, and receipt pressure before treating governance performance as closed."
} else {
    "Archive this governance performance summary with dashboard, report, and diagnostics evidence."
}
$reason = if (-not $healthOk) {
    [string](Get-JsonValue $health "reason" "governance health is not ok")
} elseif ($bottlenecks.Count -gt 0) {
    @($bottlenecks) -join ", "
} else {
    "governance performance indicators are aligned"
}

$metrics = [ordered]@{
    routeLines = Get-CountValue $routeSummary "routeLineCount"
    routeKeys = Get-CountValue $routeSummary "routeKeys"
    deliveredTouchCount = $deliveredTouchCount
    deliveredCleaned = $deliveredCleaned
    deliveredRetained = $deliveredRetained
    deliveredWithoutCleanup = $deliveredWithoutCleanup
    failedTouchCount = $failedTouchCount
    failedFallbackRetained = $failedFallbackRetained
    failedWithoutFallback = $failedWithoutFallback
    s3Lines = $s3LineCount
    s3SuccessCount = $s3SuccessCount
    s3TransientCount = $s3TransientCount
    s3IdentityFailureCount = $s3IdentityFailureCount
    s3IntegrityCount = $s3IntegrityCount
    reconcileReceiptCount = $reconcileReceiptCount
    reconcileFallbackCount = $reconcileFallbackCount
    reconcileCleaned = $reconcileCleaned
    reconcileRetained = $reconcileRetained
    receiptTotalRecords = $receiptTotalRecords
    receiptRetainedRecords = $receiptRetainedRecords
    receiptArchivedRecords = $receiptArchivedRecords
    coverageAreaCount = $coverageAreaCount
    coverageGapCount = $coverageGapCount
    coverageActionableGapCount = $coverageActionableGapCount
}
$ratios = [ordered]@{
    deliveryClosurePercent = $deliveryClosurePercent
    fallbackProtectionPercent = $fallbackProtectionPercent
    reconcileRetentionPercent = $reconcileRetentionPercent
    receiptArchivePercent = $receiptArchivePercent
    s3SuccessPercent = $s3SuccessPercent
    s3TransientPercent = $s3TransientPercent
}

$artifacts = New-Object System.Collections.ArrayList
Add-Artifact $artifacts "health" $healthPath $resolvedGovernanceDir
Add-Artifact $artifacts "route summary" $routeSummaryPath $resolvedGovernanceDir
Add-Artifact $artifacts "s3 summary" $s3SummaryPath $resolvedGovernanceDir
Add-Artifact $artifacts "s3 stability runbook" $runbookPath $resolvedGovernanceDir
Add-Artifact $artifacts "receipt rotation summary" $rotationSummaryPath $resolvedGovernanceDir
Add-Artifact $artifacts "reconcile summary" $reconcileSummaryPath $resolvedGovernanceDir

$summary = [pscustomobject]@{
    format = "qtnetworkchat-large-file-governance-performance-summary-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    title = $Title
    readOnly = $true
    status = if ($healthOk) { "ready" } else { "blocked" }
    ok = ($healthOk -and $bottlenecks.Count -eq 0)
    reason = $reason
    sensitiveHits = [int]$sensitiveHits.Count
    summary = [pscustomobject]@{
        readiness = $summaryReadiness
        operatorAction = $operatorAction
    }
    auditSummary = [pscustomobject]@{
        releaseGate = $releaseGate
        auditFocus = if ($bottlenecks.Count -gt 0) { @($bottlenecks.ToArray()) } else { @("routine-governance-performance-review") }
        evidenceBundle = @("performance-summary-json", "performance-summary-markdown", "dashboard-json", "report-markdown")
    }
    metrics = [pscustomobject]$metrics
    ratios = [pscustomobject]$ratios
    bottlenecks = @($bottlenecks.ToArray())
    artifacts = @($artifacts)
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add(("# {0}" -f $Title))
    $lines.Add("")
    $lines.Add(('- Generated at: `{0}`' -f $summary.generatedAt))
    $lines.Add('- Read only: `true`')
    $lines.Add(('- Status: `{0}`' -f $summary.status))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $summary.ok)))
    $lines.Add(('- Reason: `{0}`' -f $summary.reason))
    $lines.Add(('- Readiness: `{0}`' -f $summary.summary.readiness))
    $lines.Add(('- Release gate: `{0}`' -f $summary.auditSummary.releaseGate))
    $lines.Add(('- Operator action: `{0}`' -f $summary.summary.operatorAction))
    $lines.Add("")
    $lines.Add("## Core Metrics")
    $lines.Add("")
    $lines.Add("| Metric | Value |")
    $lines.Add("| --- | --- |")
    foreach ($key in $metrics.Keys) {
        $lines.Add(("| {0} | {1} |" -f $key, (Format-Value $metrics[$key])))
    }
    $lines.Add("")
    $lines.Add("## Core Ratios")
    $lines.Add("")
    $lines.Add("| Ratio | Value |")
    $lines.Add("| --- | --- |")
    foreach ($key in $ratios.Keys) {
        $lines.Add(("| {0} | {1} |" -f $key, (Format-Value $ratios[$key])))
    }
    $lines.Add("")
    $lines.Add("## Bottlenecks")
    $lines.Add("")
    if ($summary.bottlenecks.Count -eq 0) {
        $lines.Add("- none")
    } else {
        foreach ($item in $summary.bottlenecks) {
            $lines.Add(("- {0}" -f $item))
        }
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
    $lines.Add("This summary is generated from local governance artifacts only. It does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "large file governance performance summary"
Write-Host ("  json: {0}" -f $resolvedOutputPath)
if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
}
Write-Host ("  release gate: {0}" -f $releaseGate)
Write-Host ("  bottlenecks: {0}" -f $summary.bottlenecks.Count)
