param(
    [Parameter(Mandatory = $true)]
    [string]$HealthCheckPath,

    [string]$EventLogSource = "QtNetworkChatGovernance",

    [string]$WebhookUrl,

    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($HealthCheckPath)) {
    throw "HealthCheckPath is required."
}
if (-not (Test-Path -LiteralPath $HealthCheckPath)) {
    throw ("HealthCheckPath not found: {0}" -f $HealthCheckPath)
}

$raw = Get-Content -LiteralPath $HealthCheckPath -Raw -Encoding UTF8
if ([string]::IsNullOrWhiteSpace($raw)) {
    throw "HealthCheckPath is empty."
}
$health = $raw | ConvertFrom-Json
if ($null -eq $health -or $null -eq $health.status) {
    throw "HealthCheckPath does not contain a valid health check result."
}

if ($health.status -eq "healthy") {
    Write-Host "governance is healthy; no notification needed"
    exit 0
}

$sensitivePatterns = @(
    "endpoint\s*=",
    "bucket\s*=",
    "https?://[^\s]+",
    "access[-_\s]?key",
    "secret[-_\s]?key",
    "session[-_\s]?token",
    "Authorization",
    "Credential",
    "Signature"
)

$message = ("QtNetworkChat governance {0}: {1} (warnings={2}, sources={3})" -f
    $health.status, $health.reason, $health.totalWarnings, $health.alertCount)

foreach ($pattern in $sensitivePatterns) {
    if ($message -match $pattern) {
        throw ("Notification message contains sensitive pattern rejected: {0}" -f $pattern)
    }
}

$notified = $false

if (-not [string]::IsNullOrWhiteSpace($EventLogSource)) {
    if ($DryRun) {
        Write-Host ("dry-run: would write EventLog entry: Source={0} Message={1}" -f $EventLogSource, $message)
    } else {
        try {
            $sourceExists = [System.Diagnostics.EventLog]::SourceExists($EventLogSource)
            if (-not $sourceExists) {
                Write-Host ("EventLog source '{0}' not registered; skipping EventLog write (register with New-EventLog -LogName Application -Source '{0}' as admin)" -f $EventLogSource)
            } else {
                Write-EventLog -LogName Application -Source $EventLogSource -EventId 9001 -EntryType Warning -Message $message
                Write-Host ("EventLog entry written: Source={0} EventId=9001" -f $EventLogSource)
                $notified = $true
            }
        } catch {
            Write-Host ("EventLog write failed (non-fatal): {0}" -f $_.Exception.Message)
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($WebhookUrl)) {
    foreach ($pattern in $sensitivePatterns) {
        if ($WebhookUrl -match $pattern) {
            throw ("WebhookUrl contains sensitive pattern rejected: {0}" -f $pattern)
        }
    }

    $payload = [pscustomobject]@{
        text   = $message
        status = $health.status
        reason = $health.reason
        totalWarnings = $health.totalWarnings
        alertCount = $health.alertCount
    } | ConvertTo-Json -Depth 3

    if ($DryRun) {
        Write-Host ("dry-run: would POST to webhook: {0}" -f $WebhookUrl)
        Write-Host ("dry-run: payload: {0}" -f $payload)
    } else {
        try {
            $response = Invoke-RestMethod -Uri $WebhookUrl -Method Post -Body $payload -ContentType "application/json" -TimeoutSec 10
            Write-Host ("webhook POST succeeded: {0}" -f $WebhookUrl)
            $notified = $true
        } catch {
            Write-Host ("webhook POST failed (non-fatal): {0}" -f $_.Exception.Message)
        }
    }
}

$result = [pscustomobject]@{
    notified = $notified
    dryRun   = $DryRun.IsPresent
    status   = $health.status
    message  = $message
}

Write-Host ""
Write-Host ("notification result: notified={0} dryRun={1} status={2}" -f $notified, $DryRun.IsPresent, $health.status)

$result | ConvertTo-Json -Depth 3
