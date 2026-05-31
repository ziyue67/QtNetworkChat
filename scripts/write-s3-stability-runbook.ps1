param(
    [Parameter(Mandatory = $true)]
    [string]$S3SummaryPath,

    [string]$EvidencePath,

    [string]$GovernanceStatusPath,

    [string]$CoveragePolicyPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$MarkdownPath,

    [string]$AlertSummaryPath,

    [int]$MinSuccess = 1,

    [int]$WarnTimeout = 0,

    [int]$WarnRetryable = 0,

    [int]$WarnAuth = 0,

    [int]$WarnTls = 0,

    [int]$WarnHash = 0,

    [int]$WarnSize = 0,

    [int]$WarnSensitiveHits = 0,

    [switch]$WarnUnobservedCoverage,

    [switch]$NoFailOnWarning
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

function Resolve-RequiredFile([string]$PathValue, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $Label, $PathValue)
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Resolve-OptionalFile([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw ("Optional input not found: {0}" -f $PathValue)
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
    $raw = Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw ("JSON file is empty: {0}" -f $PathValue)
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

function Get-StringArrayValue([object]$ObjectValue, [string]$Name) {
    $value = Get-JsonValue $ObjectValue $Name @()
    @($value) | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
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

function New-Action([string]$Reason, [int]$Count, [string]$Severity, [string]$Action, [string]$Validation) {
    [pscustomobject]@{
        reason = $Reason
        count = $Count
        severity = $Severity
        action = $Action
        validation = $Validation
    }
}

function New-CoverageItem([string]$Area, [string]$Event, [string]$Operation, [string[]]$Reasons, [object]$ReasonCounts, [object]$EventOperationCounts) {
    $observed = [ordered]@{}
    $observedTotal = 0
    foreach ($reason in $Reasons) {
        $count = Get-CountValue $ReasonCounts $reason
        $observed[$reason] = $count
        $observedTotal += $count
    }
    [pscustomobject]@{
        area = $Area
        event = $Event
        operation = $Operation
        fixedReasons = @($Reasons)
        coveredByDefaultCTest = $true
        observedReasonCounts = [pscustomobject]$observed
        observedReasonTotal = $observedTotal
        observedEventOperationCount = Get-CountValue $EventOperationCounts ("{0}:{1}" -f $Event, $Operation)
    }
}

foreach ($value in @(
        @{ Name = "MinSuccess"; Value = $MinSuccess },
        @{ Name = "WarnTimeout"; Value = $WarnTimeout },
        @{ Name = "WarnRetryable"; Value = $WarnRetryable },
        @{ Name = "WarnAuth"; Value = $WarnAuth },
        @{ Name = "WarnTls"; Value = $WarnTls },
        @{ Name = "WarnHash"; Value = $WarnHash },
        @{ Name = "WarnSize"; Value = $WarnSize },
        @{ Name = "WarnSensitiveHits"; Value = $WarnSensitiveHits })) {
    if ($value.Value -lt 0) {
        throw ("{0} must be 0 or greater." -f $value.Name)
    }
}

$resolvedS3SummaryPath = Resolve-RequiredFile $S3SummaryPath "S3SummaryPath"
$resolvedEvidencePath = Resolve-OptionalFile $EvidencePath
$resolvedGovernanceStatusPath = Resolve-OptionalFile $GovernanceStatusPath
$resolvedCoveragePolicyPath = Resolve-OptionalFile $CoveragePolicyPath
$resolvedOutputPath = Resolve-OutputPath $OutputPath

$inputPaths = @($resolvedS3SummaryPath, $resolvedEvidencePath, $resolvedGovernanceStatusPath, $resolvedCoveragePolicyPath) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $inputPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; S3 stability runbook was not written."
}

$s3Summary = Read-JsonFile $resolvedS3SummaryPath
$evidence = if (-not [string]::IsNullOrWhiteSpace($resolvedEvidencePath)) { Read-JsonFile $resolvedEvidencePath } else { $null }
$governanceStatus = if (-not [string]::IsNullOrWhiteSpace($resolvedGovernanceStatusPath)) { Read-JsonFile $resolvedGovernanceStatusPath } else { $null }
$coveragePolicyInput = if (-not [string]::IsNullOrWhiteSpace($resolvedCoveragePolicyPath)) { Read-JsonFile $resolvedCoveragePolicyPath } else { $null }

$reasonCounts = Get-JsonValue $s3Summary "reasonCounts" ([pscustomobject]@{})
$operationCounts = Get-JsonValue $s3Summary "operationCounts" ([pscustomobject]@{})
$eventOperationCounts = Get-JsonValue $s3Summary "eventOperationCounts" ([pscustomobject]@{})
$s3LineCount = Get-CountValue $s3Summary "s3LineCount"
$summarySensitiveHits = Get-CountValue $s3Summary "sensitiveHits"
$successCount = Get-CountValue $reasonCounts "success"
$timeoutCount = Get-CountValue $reasonCounts "timeout"
$retryableCount = Get-CountValue $reasonCounts "retryable"
$authCount = Get-CountValue $reasonCounts "auth"
$tlsCount = Get-CountValue $reasonCounts "tls"
$hashCount = Get-CountValue $reasonCounts "hash"
$sizeCount = Get-CountValue $reasonCounts "size"
$serverCount = Get-CountValue $reasonCounts "server"
$networkCount = Get-CountValue $reasonCounts "network"
$notFoundCount = Get-CountValue $reasonCounts "not_found"

$warnings = New-Object System.Collections.Generic.List[string]
if ($summarySensitiveHits -gt $WarnSensitiveHits) {
    $warnings.Add(("s3Summary sensitiveHits={0} exceeds threshold {1}" -f $summarySensitiveHits, $WarnSensitiveHits))
}
if ($successCount -lt $MinSuccess) {
    $warnings.Add(("success={0} is below required minimum {1}" -f $successCount, $MinSuccess))
}
foreach ($threshold in @(
        @{ Reason = "timeout"; Count = $timeoutCount; Value = $WarnTimeout },
        @{ Reason = "retryable"; Count = $retryableCount; Value = $WarnRetryable },
        @{ Reason = "auth"; Count = $authCount; Value = $WarnAuth },
        @{ Reason = "tls"; Count = $tlsCount; Value = $WarnTls },
        @{ Reason = "hash"; Count = $hashCount; Value = $WarnHash },
        @{ Reason = "size"; Count = $sizeCount; Value = $WarnSize })) {
    if ($threshold.Count -gt $threshold.Value) {
        $warnings.Add(("{0}={1} exceeds threshold {2}" -f $threshold.Reason, $threshold.Count, $threshold.Value))
    }
}

$evidenceOk = if ($null -ne $evidence) { [bool](Get-JsonValue $evidence "ok" $false) } else { $false }
$governanceStatusText = if ($null -ne $governanceStatus) { [string](Get-JsonValue $governanceStatus "status" "unknown") } else { "not-provided" }
$governanceOk = if ($null -ne $governanceStatus) { [bool](Get-JsonValue $governanceStatus "ok" $false) } else { $false }
if ($null -ne $evidence -and -not $evidenceOk) {
    $warnings.Add("s3 real backend evidence is not ok")
}
if ($null -ne $governanceStatus -and -not $governanceOk) {
    $warnings.Add(("governance status is {0}" -f $governanceStatusText))
}

$actions = New-Object System.Collections.Generic.List[object]
if ($timeoutCount -gt 0) {
    $actions.Add((New-Action "timeout" $timeoutCount "warning" "Review request timeout budget, network latency, and object size distribution before increasing QTNETWORKCHAT_OBJECT_S3_TIMEOUT_MS." "Run route/S3 analysis again and confirm timeout count is within threshold."))
}
if ($retryableCount -gt 0 -or $serverCount -gt 0 -or $networkCount -gt 0) {
    $actions.Add((New-Action "retryable" ($retryableCount + $serverCount + $networkCount) "warning" "Keep offline fallback enabled, inspect transient network/server categories, and rerun the same acceptance flow before changing cleanup policy." "Confirm failed_received fallback and delivered_reconcile retained/cleaned counts still match expectations."))
}
if ($authCount -gt 0) {
    $actions.Add((New-Action "auth" $authCount "critical" "Rotate or re-check local process credentials and permissions outside logs; do not paste credentials into route logs, notes, reports, or tasks." "Rerun smoke and evidence generation after credentials are fixed."))
}
if ($tlsCount -gt 0) {
    $actions.Add((New-Action "tls" $tlsCount "critical" "Fix certificate trust or endpoint TLS configuration; only use QTNETWORKCHAT_OBJECT_S3_TLS_VERIFY=0 for an explicit local lab run." "Rerun smoke and verify no TLS warning remains in S3 request summary."))
}
if ($hashCount -gt 0 -or $sizeCount -gt 0) {
    $actions.Add((New-Action "integrity" ($hashCount + $sizeCount) "critical" "Treat hash/size mismatch as fail-closed: preserve source fallback, do not trust ETag, and inspect producer/consumer byte counts." "Confirm GET body SHA-256 and size validation pass before considering cleanup."))
}
if ($notFoundCount -gt 0) {
    $actions.Add((New-Action "not_found" $notFoundCount "warning" "Check object TTL, prefix consistency, and source fallback availability; do not delete fallback solely because the object is missing." "Confirm receiver can still replay from source offline fallback."))
}
if ($actions.Count -eq 0) {
    $actions.Add((New-Action "baseline" 0 "info" "No S3 stability action is required by current thresholds; keep the same fail-closed and fallback policy." "Archive this runbook with the evidence and dashboard outputs."))
}

$stabilizationCoverage = @(
    New-CoverageItem "source-write-fallback" "object_write" "write" @("timeout", "network", "tls", "auth", "retryable", "server", "client") $reasonCounts $eventOperationCounts
    New-CoverageItem "remote-validation-fail-closed" "offer_validation" "validate" @("tls", "auth", "retryable", "hash", "size") $reasonCounts $eventOperationCounts
    New-CoverageItem "remote-read-fail-closed" "offer_read" "read" @("timeout", "network", "tls", "auth", "retryable", "server", "not_found", "unknown") $reasonCounts $eventOperationCounts
    New-CoverageItem "source-delete-retained" "object_delete" "delete" @("timeout", "network", "tls", "auth", "retryable", "server", "unknown") $reasonCounts $eventOperationCounts
    New-CoverageItem "delivery-fallback-retained" "offer_delivery" "deliver" @("receiver-disconnected", "chunk-rejected", "chunk-ack-timeout") $reasonCounts $eventOperationCounts
)
$coverageReasonCount = 0
$coverageObservedAreaCount = 0
$coverageGapAreas = @()
foreach ($item in $stabilizationCoverage) {
    $coverageReasonCount += @($item.fixedReasons).Count
    if ([int]$item.observedEventOperationCount -gt 0) {
        $coverageObservedAreaCount += 1
    } else {
        $coverageGapAreas += [string]$item.area
    }
}
$coverageGapCount = @($coverageGapAreas).Count
$allCoverageAreas = @($stabilizationCoverage | ForEach-Object { [string]$_.area })
$policyRequiredAreas = Get-StringArrayValue $coveragePolicyInput "requiredAreas"
if (@($policyRequiredAreas).Count -eq 0) {
    $policyRequiredAreas = $allCoverageAreas
}
$policyAllowedGapAreas = Get-StringArrayValue $coveragePolicyInput "allowedGapAreas"
$policyMinObservedAreas = [int](Get-JsonValue $coveragePolicyInput "minObservedAreas" 0)
$unknownRequiredAreas = @($policyRequiredAreas | Where-Object { $_ -notin $allCoverageAreas })
$unknownAllowedGapAreas = @($policyAllowedGapAreas | Where-Object { $_ -notin $allCoverageAreas })
if (@($unknownRequiredAreas).Count -gt 0) {
    throw ("Coverage policy contains unknown requiredAreas: {0}" -f ($unknownRequiredAreas -join ", "))
}
if (@($unknownAllowedGapAreas).Count -gt 0) {
    throw ("Coverage policy contains unknown allowedGapAreas: {0}" -f ($unknownAllowedGapAreas -join ", "))
}
$coverageActionableGapAreas = @($coverageGapAreas | Where-Object { $_ -in $policyRequiredAreas -and $_ -notin $policyAllowedGapAreas })
$coverageActionableGapCount = @($coverageActionableGapAreas).Count
if ($WarnUnobservedCoverage -and $coverageActionableGapCount -gt 0) {
    $warnings.Add(("stabilizationCoverage actionable gaps={0}: {1}" -f $coverageActionableGapCount, ($coverageActionableGapAreas -join ", ")))
}
if ($WarnUnobservedCoverage -and $policyMinObservedAreas -gt 0 -and $coverageObservedAreaCount -lt $policyMinObservedAreas) {
    $warnings.Add(("stabilizationCoverage observed areas={0} below policy minimum {1}" -f $coverageObservedAreaCount, $policyMinObservedAreas))
}

$metrics = [pscustomobject]@{
    "s3LineCount" = $s3LineCount
    "successCount" = $successCount
    "timeoutCount" = $timeoutCount
    "retryableCount" = $retryableCount
    "networkCount" = $networkCount
    "serverCount" = $serverCount
    "authCount" = $authCount
    "tlsCount" = $tlsCount
    "hashCount" = $hashCount
    "sizeCount" = $sizeCount
    "notFoundCount" = $notFoundCount
    "coverageAreaCount" = @($stabilizationCoverage).Count
    "coverageFixedReasonCount" = $coverageReasonCount
    "coverageObservedAreaCount" = $coverageObservedAreaCount
    "coverageGapCount" = $coverageGapCount
    "coverageActionableGapCount" = $coverageActionableGapCount
    "coveragePolicyMinObservedAreas" = $policyMinObservedAreas
    "sensitiveHits" = [int]$sensitiveHits.Count
    "s3SummarySensitiveHits" = $summarySensitiveHits
    "evidenceOk" = $evidenceOk
    "governanceStatus" = $governanceStatusText
    "governanceOk" = $governanceOk
}

$inputs = [pscustomobject]@{
    "s3SummaryPath" = $resolvedS3SummaryPath
    "evidencePath" = $resolvedEvidencePath
    "governanceStatusPath" = $resolvedGovernanceStatusPath
    "coveragePolicyPath" = $resolvedCoveragePolicyPath
}

$runbookOk = ($warnings.Count -eq 0)
$runbook = [pscustomobject]@{
    "format" = "qtnetworkchat-s3-stability-runbook-v1"
    "generatedAt" = (Get-Date).ToUniversalTime().ToString("o")
    "readOnly" = $true
    "ok" = $runbookOk
    "warnings" = @($warnings)
    "metrics" = $metrics
    "reasonCounts" = $reasonCounts
    "operationCounts" = $operationCounts
    "eventOperationCounts" = $eventOperationCounts
    "stabilizationCoverage" = @($stabilizationCoverage)
    "coverageGapAreas" = @($coverageGapAreas)
    "coverageActionableGapAreas" = @($coverageActionableGapAreas)
    "coveragePolicy" = [pscustomobject]@{
        requiredAreas = @($policyRequiredAreas)
        allowedGapAreas = @($policyAllowedGapAreas)
        minObservedAreas = $policyMinObservedAreas
    }
    "actions" = @($actions.ToArray())
    "inputs" = $inputs
    "notes" = "This runbook is generated from local summaries only; it does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files."
}

$runbook | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = Resolve-OutputPath $MarkdownPath
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat S3 Stability Runbook")
    $lines.Add("")
    $lines.Add(('- OK: `{0}`' -f ([string]$runbookOk).ToLowerInvariant()))
    $lines.Add(('- S3 lines: `{0}`' -f $s3LineCount))
    $lines.Add(('- Success count: `{0}`' -f $successCount))
    $lines.Add(('- Governance status: `{0}`' -f $governanceStatusText))
    $lines.Add(('- Coverage gaps: `{0}`' -f $coverageGapCount))
    $lines.Add(('- Actionable coverage gaps: `{0}`' -f $coverageActionableGapCount))
    $lines.Add("")
    $lines.Add("## Actions")
    $lines.Add("")
    $lines.Add("| Reason | Count | Severity | Action | Validation |")
    $lines.Add("| --- | ---: | --- | --- | --- |")
    foreach ($action in $actions) {
        $lines.Add(("| {0} | {1} | {2} | {3} | {4} |" -f $action.reason, $action.count, $action.severity, $action.action, $action.validation))
    }
    $lines.Add("")
    $lines.Add("## Stabilization Coverage")
    $lines.Add("")
    $lines.Add("| Area | Event | Operation | Fixed reasons | Observed event operations |")
    $lines.Add("| --- | --- | --- | --- | ---: |")
    foreach ($item in $stabilizationCoverage) {
        $lines.Add(("| {0} | {1} | {2} | {3} | {4} |" -f $item.area, $item.event, $item.operation, (@($item.fixedReasons) -join ", "), $item.observedEventOperationCount))
    }
    if ($coverageGapCount -gt 0) {
        $lines.Add("")
        $gapText = $coverageGapAreas -join ", "
        $lines.Add(("Unobserved coverage areas: {0}" -f $gapText))
    }
    if ($coverageActionableGapCount -gt 0) {
        $actionableGapText = $coverageActionableGapAreas -join ", "
        $lines.Add(("Actionable coverage gaps: {0}" -f $actionableGapText))
    }
    $lines.Add("")
    $lines.Add("This runbook is read-only and contains no sensitive request data.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($AlertSummaryPath)) {
    $resolvedAlertSummaryPath = Resolve-OutputPath $AlertSummaryPath
    [pscustomobject]@{
        kind = "s3-stability-runbook"
        ok = $runbookOk
        warnings = @($warnings)
        metrics = $runbook.metrics
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedAlertSummaryPath -Encoding UTF8
}

Write-Host "S3 stability runbook"
Write-Host ("  output: {0}" -f $resolvedOutputPath)
Write-Host ("  s3 lines: {0}" -f $s3LineCount)
Write-Host ("  success: {0}" -f $successCount)
Write-Host ("  actions: {0}" -f $actions.Count)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)
if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "warnings"
    foreach ($warning in $warnings) {
        Write-Host ("  {0}" -f $warning)
    }
    if (-not $NoFailOnWarning) {
        throw "S3 stability runbook warnings were found."
    }
}
