param(
    [Parameter(Mandatory = $true)]
    [string]$S3SummaryPath,

    [string]$RouteSummaryPath,

    [string]$GovernanceStatusPath,

    [string]$SmokeLogPath,

    [string]$NotesPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$MarkdownPath,

    [string]$AlertSummaryPath,

    [int]$MinS3Lines = 1,

    [switch]$RequireSuccess,

    [switch]$RequireFailureReason,

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

$failureReasons = @(
    "timeout",
    "network",
    "tls",
    "auth",
    "not_found",
    "retryable",
    "client",
    "server",
    "unknown",
    "size",
    "hash",
    "object-store-unavailable",
    "write_failed",
    "receiver-disconnected",
    "chunk-rejected",
    "chunk-ack-timeout"
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

function Write-ParentDirectory([string]$PathValue) {
    $parent = Split-Path -Parent $PathValue
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

if ($MinS3Lines -lt 0) {
    throw "MinS3Lines must be 0 or greater."
}

$resolvedS3SummaryPath = Resolve-RequiredFile $S3SummaryPath "S3SummaryPath"
$resolvedRouteSummaryPath = Resolve-OptionalFile $RouteSummaryPath
$resolvedGovernanceStatusPath = Resolve-OptionalFile $GovernanceStatusPath
$resolvedSmokeLogPath = Resolve-OptionalFile $SmokeLogPath
$resolvedNotesPath = Resolve-OptionalFile $NotesPath
$resolvedOutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)

$inputPaths = @(
    $resolvedS3SummaryPath,
    $resolvedRouteSummaryPath,
    $resolvedGovernanceStatusPath,
    $resolvedSmokeLogPath,
    $resolvedNotesPath
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $inputPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found; S3 real backend evidence was not accepted."
}

$s3Summary = Read-JsonFile $resolvedS3SummaryPath
$routeSummary = if (-not [string]::IsNullOrWhiteSpace($resolvedRouteSummaryPath)) { Read-JsonFile $resolvedRouteSummaryPath } else { $null }
$governanceStatus = if (-not [string]::IsNullOrWhiteSpace($resolvedGovernanceStatusPath)) { Read-JsonFile $resolvedGovernanceStatusPath } else { $null }

$reasonCounts = Get-JsonValue $s3Summary "reasonCounts" ([pscustomobject]@{})
$operationCounts = Get-JsonValue $s3Summary "operationCounts" ([pscustomobject]@{})
$resultCounts = Get-JsonValue $s3Summary "resultCounts" ([pscustomobject]@{})
$s3LineCount = Get-CountValue $s3Summary "s3LineCount"
$summarySensitiveHits = Get-CountValue $s3Summary "sensitiveHits"
$successCount = Get-CountValue $reasonCounts "success"
$failureReasonCounts = [ordered]@{}
$failureTotal = 0
foreach ($reason in $failureReasons) {
    $count = Get-CountValue $reasonCounts $reason
    if ($count -gt 0) {
        $failureReasonCounts[$reason] = $count
        $failureTotal += $count
    }
}

$warnings = New-Object System.Collections.Generic.List[string]
if ($summarySensitiveHits -gt 0) {
    $warnings.Add(("s3Summary sensitiveHits={0}" -f $summarySensitiveHits))
}
if ($s3LineCount -lt $MinS3Lines) {
    $warnings.Add(("s3LineCount={0} is below required minimum {1}" -f $s3LineCount, $MinS3Lines))
}
if ($RequireSuccess -and $successCount -lt 1) {
    $warnings.Add("success reason is required but was not observed")
}
if ($RequireFailureReason -and $failureTotal -lt 1) {
    $warnings.Add("at least one fixed failure reason is required but was not observed")
}

$routeLineCount = if ($null -ne $routeSummary) { Get-CountValue $routeSummary "routeLineCount" } else { 0 }
$governanceStatusText = if ($null -ne $governanceStatus) { [string](Get-JsonValue $governanceStatus "status" "unknown") } else { "not-provided" }
$governanceOk = if ($null -ne $governanceStatus) { [bool](Get-JsonValue $governanceStatus "ok" $false) } else { $false }

$evidence = [pscustomobject]@{
    format = "qtnetworkchat-s3-real-backend-evidence-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    readOnly = $true
    ok = ($warnings.Count -eq 0)
    warnings = @($warnings)
    inputs = [pscustomobject]@{
        s3SummaryPath = $resolvedS3SummaryPath
        routeSummaryPath = $resolvedRouteSummaryPath
        governanceStatusPath = $resolvedGovernanceStatusPath
        smokeLogPath = $resolvedSmokeLogPath
        notesPath = $resolvedNotesPath
    }
    metrics = [pscustomobject]@{
        s3LineCount = $s3LineCount
        routeLineCount = $routeLineCount
        successCount = $successCount
        fixedFailureReasonCount = $failureTotal
        sensitiveHits = [int]$sensitiveHits.Count
        s3SummarySensitiveHits = $summarySensitiveHits
        governanceStatus = $governanceStatusText
        governanceOk = $governanceOk
    }
    reasonCounts = $reasonCounts
    operationCounts = $operationCounts
    resultCounts = $resultCounts
    fixedFailureReasonCounts = [pscustomobject]$failureReasonCounts
    notes = "Evidence is produced from local summaries/logs only; this script does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files."
}

Write-ParentDirectory $resolvedOutputPath
$evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MarkdownPath)
    Write-ParentDirectory $resolvedMarkdownPath
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat S3 Real Backend Evidence")
    $lines.Add("")
    $lines.Add(('- OK: `{0}`' -f ([string]$evidence.ok).ToLowerInvariant()))
    $lines.Add(('- S3 lines: `{0}`' -f $s3LineCount))
    $lines.Add(('- Success count: `{0}`' -f $successCount))
    $lines.Add(('- Fixed failure reasons: `{0}`' -f $failureTotal))
    $lines.Add(('- Governance status: `{0}`' -f $governanceStatusText))
    $lines.Add("")
    $lines.Add("## Warnings")
    $lines.Add("")
    if ($warnings.Count -eq 0) {
        $lines.Add("- none")
    } else {
        foreach ($warning in $warnings) {
            $lines.Add(("- {0}" -f $warning))
        }
    }
    $lines.Add("")
    $lines.Add("This evidence file is read-only and contains no endpoint, bucket, object URL, credentials, or signatures.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($AlertSummaryPath)) {
    $resolvedAlertSummaryPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($AlertSummaryPath)
    Write-ParentDirectory $resolvedAlertSummaryPath
    [pscustomobject]@{
        kind = "s3-real-backend-evidence"
        ok = ($warnings.Count -eq 0)
        warnings = @($warnings)
        metrics = $evidence.metrics
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedAlertSummaryPath -Encoding UTF8
}

Write-Host "S3 real backend evidence"
Write-Host ("  output: {0}" -f $resolvedOutputPath)
Write-Host ("  s3 lines: {0}" -f $s3LineCount)
Write-Host ("  success: {0}" -f $successCount)
Write-Host ("  fixed failure reasons: {0}" -f $failureTotal)
Write-Host ("  sensitive hits: {0}" -f $sensitiveHits.Count)
if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "warnings"
    foreach ($warning in $warnings) {
        Write-Host ("  {0}" -f $warning)
    }
    if (-not $NoFailOnWarning) {
        throw "S3 real backend evidence warnings were found."
    }
}
