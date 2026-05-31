param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [int]$CountPerReason = 2,

    [switch]$RunAnalysis,

    [switch]$NoFailOnWarning
)

$ErrorActionPreference = "Stop"

if ($CountPerReason -lt 1) {
    throw "CountPerReason must be 1 or greater."
}

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

function New-RouteLine([int]$Index, [string]$Reason, [string]$Operation, [string]$Event, [string]$Result) {
    $hashSeed = ("{0:x8}" -f $Index)
    $fileHash = (($hashSeed * 8).Substring(0, 64))
    $objectKey = ("batchs3{0:d4}{1}" -f $Index, $Reason.Replace("-", ""))
    "2026-05-30T00:00:{0:d2}Z redis_large_file_route event={1} result={2} sourceInstanceId=batch-source transferId=batch-{0:d4} objectKey={3} receiverId=receiver-{0:d4} fileHash={4} bytes=2097152 storeType=s3 operation={5} reason={6}" -f ($Index % 60), $Event, $Result, $objectKey, $fileHash, $Operation, $Reason
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$routeLogPath = Join-Path $resolvedOutputDir "s3-failure-batch-route.log"
$summaryPath = Join-Path $resolvedOutputDir "s3-failure-batch-summary.json"
$alertSummaryPath = Join-Path $resolvedOutputDir "s3-failure-batch-alert-summary.json"

$scenarios = @(
    @{ Reason = "timeout"; Operation = "write"; Event = "object_write"; Result = "skipped" },
    @{ Reason = "network"; Operation = "read"; Event = "offer_read"; Result = "rejected" },
    @{ Reason = "tls"; Operation = "validate"; Event = "offer_validation"; Result = "rejected" },
    @{ Reason = "auth"; Operation = "head"; Event = "failed"; Result = "skipped" },
    @{ Reason = "retryable"; Operation = "put"; Event = "object_write"; Result = "skipped" },
    @{ Reason = "server"; Operation = "delete"; Event = "object_delete"; Result = "retained" },
    @{ Reason = "hash"; Operation = "validate"; Event = "offer_validation"; Result = "rejected" },
    @{ Reason = "size"; Operation = "get"; Event = "failed"; Result = "skipped" },
    @{ Reason = "chunk-ack-timeout"; Operation = "deliver"; Event = "offer_delivery"; Result = "failed" },
    @{ Reason = "receiver-disconnected"; Operation = "deliver"; Event = "offer_delivery"; Result = "failed" },
    @{ Reason = "success"; Operation = "delete"; Event = "delivered_cleanup"; Result = "cleaned" }
)

$lines = New-Object System.Collections.Generic.List[string]
$index = 0
foreach ($scenario in $scenarios) {
    for ($i = 0; $i -lt $CountPerReason; ++$i) {
        $index += 1
        $lines.Add((New-RouteLine $index $scenario.Reason $scenario.Operation $scenario.Event $scenario.Result))
    }
}
$lines | Set-Content -LiteralPath $routeLogPath -Encoding UTF8

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $routeLogPath $sensitiveHits
if ($sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    throw "Sensitive fields were found in generated S3 failure batch sample."
}

Write-Host "S3 failure batch sample"
Write-Host ("  route log: {0}" -f $routeLogPath)
Write-Host ("  scenarios: {0}" -f $scenarios.Count)
Write-Host ("  count per reason: {0}" -f $CountPerReason)
Write-Host ("  route lines: {0}" -f $lines.Count)
Write-Host "  sensitive hits: 0"
Write-Host "This sample is read-only: it does not connect to Redis/S3/MinIO and does not modify queues, attachments, objects, or receipt files."

if ($RunAnalysis) {
    $analyzer = Join-Path $PSScriptRoot "analyze-s3-request-results.ps1"
    Write-Host ""
    Write-Host "running S3 request result analysis for batch sample"
    & powershell -ExecutionPolicy Bypass -File $analyzer `
        -Path $routeLogPath `
        -SummaryPath $summaryPath `
        -AlertSummaryPath $alertSummaryPath `
        -WarnTimeout 0 `
        -WarnRetryable 0 `
        -WarnAuth 0 `
        -WarnTls 0 `
        -WarnHash 0 `
        -WarnSize 0 `
        -NoFailOnWarning
    if ($LASTEXITCODE -ne 0) {
        throw ("S3 failure batch analysis failed with exit code {0}" -f $LASTEXITCODE)
    }
    if (-not $NoFailOnWarning) {
        $alert = Get-Content -LiteralPath $alertSummaryPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not [bool]$alert.ok) {
            throw "S3 failure batch sample produced warnings. Use -NoFailOnWarning for drill output generation."
        }
    }
}
