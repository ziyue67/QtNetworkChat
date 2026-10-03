param(
    [ValidateSet("network", "auth", "missing-object", "receiver-disconnect", "all")]
    [string]$Scenario = "all",

    [string[]]$LogPath = @()
)

$ErrorActionPreference = "Stop"

$scenarioDefinitions = [ordered]@{
    "network" = [ordered]@{
        Inject = "Stop MinIO, block the S3 endpoint locally, or point QTNETWORKCHAT_OBJECT_S3_ENDPOINT at an unused local port."
        ExpectedReason = "timeout|network|retryable|server|unknown"
        ExpectedEvents = "object_write skipped, failed, failed_received fallback-retained, or delivered_cleanup retained depending on the operation that failed."
        FallbackCheck = "Origin offline attachment queue remains; receiver can later log in to the origin instance and receive the file."
    }
    "auth" = [ordered]@{
        Inject = "Start the server with an invalid QTNETWORKCHAT_OBJECT_S3_ACCESS_KEY or QTNETWORKCHAT_OBJECT_S3_SECRET_KEY."
        ExpectedReason = "auth"
        ExpectedEvents = "object_write skipped, offer_validation rejected, failed, and failed_received fallback-retained are acceptable depending on where auth fails."
        FallbackCheck = "No partial file is delivered; origin offline fallback remains available."
    }
    "missing-object" = [ordered]@{
        Inject = "After a large_file_offer is published, delete or corrupt the object through MinIO/S3 before the remote instance validates it."
        ExpectedReason = "not_found|size|hash"
        ExpectedEvents = "offer_validation rejected followed by large_file_failed and failed_received fallback-retained."
        FallbackCheck = "Source queue and local offline attachment are retained; object TTL cleanup alone must not delete the queue."
    }
    "receiver-disconnect" = [ordered]@{
        Inject = "Disconnect the receiver client during routed large-file chunk delivery or before ACK completion."
        ExpectedReason = "receiver-disconnected|chunk-ack-timeout|chunk-rejected:*"
        ExpectedEvents = "large_file_failed and failed_received fallback-retained."
        FallbackCheck = "Receiver can reconnect to the origin instance and receive the offline fallback file."
    }
}

function Write-Scenario([string]$Name, [hashtable]$Definition) {
    Write-Host ""
    Write-Host ("scenario: {0}" -f $Name)
    Write-Host ("  inject: {0}" -f $Definition.Inject)
    Write-Host ("  expected reason: {0}" -f $Definition.ExpectedReason)
    Write-Host ("  expected route events: {0}" -f $Definition.ExpectedEvents)
    Write-Host ("  fallback check: {0}" -f $Definition.FallbackCheck)
}

Write-Host "QtNetworkChat S3/MinIO failure drill"
Write-Host "This helper prints manual failure scenarios only; it does not connect to Redis, S3, MinIO, or edit any queue."
Write-Host "Keep QTNETWORKCHAT_OBJECT_S3_ENABLE disabled again after the drill unless you are actively testing."

if ($Scenario -eq "all") {
    foreach ($name in $scenarioDefinitions.Keys) {
        Write-Scenario $name $scenarioDefinitions[$name]
    }
} else {
    Write-Scenario $Scenario $scenarioDefinitions[$Scenario]
}

Write-Host ""
Write-Host "redaction boundary"
Write-Host "  Logs, Redis events, and offline queues may contain objectKey, transferId, receiverId, storeType, operation, bytes, and fixed reason buckets."
Write-Host "  They must not contain endpoint, bucket, object URL, access key, secret key, session token, Authorization, Credential, or Signature."

if ($LogPath.Count -gt 0) {
    $analyzer = Join-Path $PSScriptRoot "analyze-large-file-route-logs.ps1"
    if (-not (Test-Path -LiteralPath $analyzer)) {
        throw "Log analyzer not found: $analyzer"
    }
    Write-Host ""
    Write-Host "running log analyzer"
    & $analyzer -Path $LogPath
}
