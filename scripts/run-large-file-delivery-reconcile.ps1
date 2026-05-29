param(
    [string[]]$RouteLogPath,

    [string]$ReceiptPath,

    [Parameter(Mandatory = $true)]
    [string[]]$QueuePath,

    [Parameter(Mandatory = $true)]
    [string]$SourceInstanceId,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [switch]$EmitRouteLog,

    [switch]$NoFailOnSensitive,

    [switch]$RunS3Analysis,

    [string]$S3AnalysisSummaryPath,

    [switch]$RunRotate,

    [string]$RotationSummaryPath,

    [int]$RotationKeepRecords,

    [int]$RotationMaxAgeDays
)

$ErrorActionPreference = "Stop"

function Invoke-CheckedScript([string]$ScriptPath, [string[]]$Arguments, [string]$OutputPath) {
    $parent = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $output = & powershell -ExecutionPolicy Bypass -File $ScriptPath @Arguments 2>&1
    $output | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    $output | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw ("Script failed with exit code {0}: {1}" -f $LASTEXITCODE, $ScriptPath)
    }
}

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$receiptsPath = Join-Path $resolvedOutputDir "receipts.jsonl"
$fallbacksPath = Join-Path $resolvedOutputDir "fallbacks.jsonl"
$receiptLogPath = Join-Path $resolvedOutputDir "export-receipts.log"
$fallbackLogPath = Join-Path $resolvedOutputDir "export-fallbacks.log"
$reconcileLogPath = Join-Path $resolvedOutputDir "reconcile.log"

$receiptScript = Join-Path $PSScriptRoot "export-large-file-receipts.ps1"
$fallbackScript = Join-Path $PSScriptRoot "export-large-file-fallbacks.ps1"
$reconcileScript = Join-Path $PSScriptRoot "reconcile-large-file-delivery.ps1"
$s3AnalysisScript = Join-Path $PSScriptRoot "analyze-s3-request-results.ps1"
$rotationScript = Join-Path $PSScriptRoot "rotate-large-file-receipts.ps1"

if ([string]::IsNullOrWhiteSpace($S3AnalysisSummaryPath)) {
    $S3AnalysisSummaryPath = Join-Path $resolvedOutputDir "s3-analysis-summary.json"
}
if ([string]::IsNullOrWhiteSpace($RotationSummaryPath)) {
    $RotationSummaryPath = Join-Path $resolvedOutputDir "rotation-summary.json"
}

$totalSteps = 3
if ($RunS3Analysis) { $totalSteps++ }
if ($RunRotate) { $totalSteps++ }
$currentStep = 0

function StepLabel([string]$Label) {
    $script:currentStep++
    Write-Host ""
    Write-Host ("step {0}/{1} {2}" -f $script:currentStep, $totalSteps, $Label)
}

if (($null -eq $RouteLogPath -or $RouteLogPath.Count -eq 0) -and [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Either RouteLogPath or ReceiptPath is required."
}
if (($null -ne $RouteLogPath -and $RouteLogPath.Count -gt 0) -and -not [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    throw "Use either RouteLogPath or ReceiptPath, not both."
}

$fallbackArgs = @("-QueuePath") + $QueuePath + @("-SourceInstanceId", $SourceInstanceId, "-OutputPath", $fallbacksPath)
$reconcileArgs = @("-ReceiptPath", $receiptsPath, "-FallbackPath", $fallbacksPath)
if ($EmitRouteLog) {
    $reconcileArgs += "-EmitRouteLog"
}
if ($NoFailOnSensitive) {
    $fallbackArgs += "-NoFailOnSensitive"
    $reconcileArgs += "-NoFailOnSensitive"
}

Write-Host "large file delivered reconciliation run"
Write-Host ("  output dir: {0}" -f $resolvedOutputDir)

StepLabel "export or copy receipts"
if (-not [string]::IsNullOrWhiteSpace($ReceiptPath)) {
    if (-not (Test-Path -LiteralPath $ReceiptPath)) {
        throw "Receipt path not found: $ReceiptPath"
    }
    Copy-Item -LiteralPath $ReceiptPath -Destination $receiptsPath -Force
    Write-Host ("persisted receipts copied: {0}" -f $ReceiptPath) | Tee-Object -FilePath $receiptLogPath
} else {
    $receiptArgs = @("-Path") + $RouteLogPath + @("-OutputPath", $receiptsPath)
    if ($NoFailOnSensitive) {
        $receiptArgs += "-NoFailOnSensitive"
    }
    Invoke-CheckedScript $receiptScript $receiptArgs $receiptLogPath
}

StepLabel "export fallbacks"
Invoke-CheckedScript $fallbackScript $fallbackArgs $fallbackLogPath

StepLabel "reconcile"
Invoke-CheckedScript $reconcileScript $reconcileArgs $reconcileLogPath

if ($RunS3Analysis) {
    StepLabel "S3 request result analysis"
    $s3AnalysisArgs = @("-Path") + @($resolvedOutputDir) + @("-SummaryPath", $s3AnalysisSummaryPath)
    if ($NoFailOnSensitive) {
        $s3AnalysisArgs += "-NoFailOnSensitive"
    }
    $s3AnalysisLogPath = Join-Path $resolvedOutputDir "s3-analysis.log"
    Invoke-CheckedScript $s3AnalysisScript $s3AnalysisArgs $s3AnalysisLogPath
}

if ($RunRotate) {
    StepLabel "receipt rotation"
    $rotationArgs = @("-ReceiptPath", $receiptsPath)
    if (-not [string]::IsNullOrWhiteSpace($RotationSummaryPath)) {
        $rotationArgs += @("-SummaryPath", $RotationSummaryPath)
    }
    if ($RotationKeepRecords -gt 0) {
        $rotationArgs += @("-KeepRecords", $RotationKeepRecords)
    }
    if ($RotationMaxAgeDays -gt 0) {
        $rotationArgs += @("-MaxAgeDays", $RotationMaxAgeDays)
    }
    if ($NoFailOnSensitive) {
        $rotationArgs += "-NoFailOnSensitive"
    }
    $rotationLogPath = Join-Path $resolvedOutputDir "rotation.log"
    Invoke-CheckedScript $rotationScript $rotationArgs $rotationLogPath
}

Write-Host ""
Write-Host "outputs"
Write-Host ("  receipts: {0}" -f $receiptsPath)
Write-Host ("  fallbacks: {0}" -f $fallbacksPath)
Write-Host ("  reconcile log: {0}" -f $reconcileLogPath)
if ($RunS3Analysis) {
    Write-Host ("  S3 analysis summary: {0}" -f $S3AnalysisSummaryPath)
}
if ($RunRotate) {
    Write-Host ("  rotation summary: {0}" -f $RotationSummaryPath)
}
Write-Host ""
Write-Host "This run is read-only: it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
