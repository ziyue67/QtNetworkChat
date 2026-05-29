param(
    [Parameter(Mandatory = $true)]
    [string[]]$RouteLogPath,

    [Parameter(Mandatory = $true)]
    [string[]]$QueuePath,

    [Parameter(Mandatory = $true)]
    [string]$SourceInstanceId,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [switch]$EmitRouteLog,

    [switch]$NoFailOnSensitive
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

$receiptArgs = @("-Path") + $RouteLogPath + @("-OutputPath", $receiptsPath)
$fallbackArgs = @("-QueuePath") + $QueuePath + @("-SourceInstanceId", $SourceInstanceId, "-OutputPath", $fallbacksPath)
$reconcileArgs = @("-ReceiptPath", $receiptsPath, "-FallbackPath", $fallbacksPath)
if ($EmitRouteLog) {
    $reconcileArgs += "-EmitRouteLog"
}
if ($NoFailOnSensitive) {
    $receiptArgs += "-NoFailOnSensitive"
    $fallbackArgs += "-NoFailOnSensitive"
    $reconcileArgs += "-NoFailOnSensitive"
}

Write-Host "large file delivered reconciliation run"
Write-Host ("  output dir: {0}" -f $resolvedOutputDir)
Write-Host ""
Write-Host "step 1/3 export receipts"
Invoke-CheckedScript $receiptScript $receiptArgs $receiptLogPath

Write-Host ""
Write-Host "step 2/3 export fallbacks"
Invoke-CheckedScript $fallbackScript $fallbackArgs $fallbackLogPath

Write-Host ""
Write-Host "step 3/3 reconcile"
Invoke-CheckedScript $reconcileScript $reconcileArgs $reconcileLogPath

Write-Host ""
Write-Host "outputs"
Write-Host ("  receipts: {0}" -f $receiptsPath)
Write-Host ("  fallbacks: {0}" -f $fallbacksPath)
Write-Host ("  reconcile log: {0}" -f $reconcileLogPath)
Write-Host ""
Write-Host "This run is read-only: it does not connect to Redis/S3/MinIO and does not modify queues, attachments, or objects."
