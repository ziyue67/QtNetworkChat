param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [switch]$RunReconcile,

    [switch]$RunRotate
)

$ErrorActionPreference = "Stop"

$resolvedOutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$routeLogPath = Join-Path $resolvedOutputDir "sample-route.log"
$queuePath = Join-Path $resolvedOutputDir "sample-offline-queue.jsonl"
$receiptPath = Join-Path $resolvedOutputDir "sample-delivered-receipts.jsonl"
$rotationReceiptPath = Join-Path $resolvedOutputDir "sample-rotation-receipts.jsonl"
$rotationSummaryPath = Join-Path $resolvedOutputDir "sample-rotation-summary.json"
$reconcileOutputDir = Join-Path $resolvedOutputDir "reconcile-output"
$sourceInstanceId = "source-sample-a"
$hashA = "a" * 64
$hashB = "b" * 64

@(
    "redis_large_file_route event=delivered result=published sourceInstanceId=$sourceInstanceId transferId=sample-clean objectKey=sampleclean001.bin receiverId=receiver-001 fileHash=$hashA bytes=1048576 storeType=s3 operation=publish",
    "redis_large_file_route event=delivered result=published sourceInstanceId=$sourceInstanceId transferId=sample-partial objectKey=samplepartial001.bin receiverId=receiver-002 fileHash=$hashB bytes=524288 storeType=s3 operation=publish",
    "redis_large_file_route event=failed result=published sourceInstanceId=$sourceInstanceId transferId=sample-noise objectKey=samplenoise001.bin receiverId=receiver-003 fileHash=$hashA reason=network storeType=s3 operation=publish",
    "redis_large_file_route event=delivered_reconcile result=retained reason=confirmed-bytes-insufficient sourceInstanceId=$sourceInstanceId transferId=sample-retained-log objectKey=sampleretained001.bin receiverId=receiver-004 fileHash=$hashB bytes=256 storeType=s3 operation=reconcile"
) | Set-Content -LiteralPath $routeLogPath -Encoding UTF8

@(
    ([pscustomobject]@{
        sourceInstanceId = $sourceInstanceId
        transferId = "sample-clean"
        receiverId = "receiver-001"
        objectKey = "sampleclean001.bin"
        fileHash = $hashA
        confirmedBytes = 1048576
        result = "cleaned"
        reason = "cleaned"
        cleanupResult = "cleaned"
        createdAt = "2026-05-29T00:00:00Z"
    } | ConvertTo-Json -Compress),
    ([pscustomobject]@{
        sourceInstanceId = $sourceInstanceId
        transferId = "sample-partial"
        receiverId = "receiver-002"
        objectKey = "samplepartial001.bin"
        fileHash = $hashB
        confirmedBytes = 524288
        result = "retained"
        reason = "confirmed-bytes-insufficient"
        cleanupResult = "retained"
        createdAt = "2026-05-29T00:00:01Z"
    } | ConvertTo-Json -Compress)
) | Set-Content -LiteralPath $receiptPath -Encoding UTF8

@(
    ([pscustomobject]@{
        sourceInstanceId = $sourceInstanceId
        transferId = "sample-old"
        receiverId = "receiver-009"
        objectKey = "sampleold001.bin"
        fileHash = $hashA
        confirmedBytes = 128
        result = "retained"
        reason = "receipt-not-matched"
        cleanupResult = "retained"
        createdAt = "2026-01-01T00:00:00Z"
    } | ConvertTo-Json -Compress),
    (Get-Content -LiteralPath $receiptPath)
) | Set-Content -LiteralPath $rotationReceiptPath -Encoding UTF8

@(
    ([pscustomobject]@{
        transferId = "sample-clean"
        receiverId = "receiver-001"
        objectStoreKey = "sampleclean001.bin"
        fileHash = $hashA
        fileSize = 1048576
    } | ConvertTo-Json -Compress),
    ([pscustomobject]@{
        transferId = "sample-partial"
        receiverId = "receiver-002"
        objectStoreKey = "samplepartial001.bin"
        fileHash = $hashB
        fileSize = 1048576
    } | ConvertTo-Json -Compress),
    ([pscustomobject]@{
        transferId = "sample-no-object-store-key"
        receiverId = "receiver-005"
        fileHash = $hashA
        fileSize = 128
    } | ConvertTo-Json -Compress)
) | Set-Content -LiteralPath $queuePath -Encoding UTF8

Write-Host "large file reconcile sample"
Write-Host ("  route log: {0}" -f $routeLogPath)
Write-Host ("  persisted receipts: {0}" -f $receiptPath)
Write-Host ("  rotation receipts: {0}" -f $rotationReceiptPath)
Write-Host ("  rotation summary: {0}" -f $rotationSummaryPath)
Write-Host ("  offline queue: {0}" -f $queuePath)
Write-Host ("  sourceInstanceId: {0}" -f $sourceInstanceId)
Write-Host ""
Write-Host "The sample contains one cleaned candidate, one retained candidate, and one old receipt for rotation; it has no endpoint, bucket, object URL, credentials, or signature fields."

if ($RunReconcile) {
    $runner = Join-Path $PSScriptRoot "run-large-file-delivery-reconcile.ps1"
    Write-Host ""
    Write-Host "running read-only reconciliation sample"
    & powershell -ExecutionPolicy Bypass -File $runner `
        -ReceiptPath $receiptPath `
        -QueuePath $queuePath `
        -SourceInstanceId $sourceInstanceId `
        -OutputDir $reconcileOutputDir `
        -EmitRouteLog
    if ($LASTEXITCODE -ne 0) {
        throw ("Sample reconciliation failed with exit code {0}" -f $LASTEXITCODE)
    }
}

if ($RunRotate) {
    $rotator = Join-Path $PSScriptRoot "rotate-large-file-receipts.ps1"
    Write-Host ""
    Write-Host "running read-only-safe receipt rotation sample"
    & powershell -ExecutionPolicy Bypass -File $rotator `
        -ReceiptPath $rotationReceiptPath `
        -KeepRecords 2 `
        -MaxAgeDays 30 `
        -CompressArchive `
        -SummaryPath $rotationSummaryPath
    if ($LASTEXITCODE -ne 0) {
        throw ("Sample receipt rotation failed with exit code {0}" -f $LASTEXITCODE)
    }
}
