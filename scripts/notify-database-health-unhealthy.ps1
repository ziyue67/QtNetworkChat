param(
    [Parameter(Mandatory = $true)]
    [string]$DashboardPath,

    [string]$AlertPath,

    [string]$MarkdownPath,

    [string]$EventLogSource = "QtNetworkChatDatabaseHealth",

    [string]$WebhookUrl,

    [switch]$DryRun,

    [switch]$FailOnUnhealthy
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'QTNETWORKCHAT_PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'Authorization',
    'Credential',
    'Signature'
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Read-JsonFile([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw "DashboardPath not found: $PathValue"
    }
    $raw = Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw "DashboardPath is empty: $PathValue"
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

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $PathValue -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "<redacted>") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
    }
}

function Assert-NoSensitiveText([string]$Label, [string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }
    foreach ($pattern in $sensitivePatterns) {
        if ($Value -match $pattern -and $Value -notmatch "<redacted>") {
            throw ("{0} contains sensitive pattern rejected: {1}" -f $Label, $pattern)
        }
    }
}

function Normalize-Bool([object]$Value, [bool]$DefaultValue = $false) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return $Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    return $text -eq "true" -or $text -eq "1" -or $text -eq "yes"
}

function Format-Value([object]$Value) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        return "n/a"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    [string]$Value
}

$resolvedDashboardPath = Resolve-OptionalPath $DashboardPath
$resolvedAlertPath = Resolve-OptionalPath $AlertPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
$dashboard = Read-JsonFile $resolvedDashboardPath

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $resolvedDashboardPath $sensitiveHits

$dashboardStatus = [string](Get-JsonValue $dashboard "status" "unknown")
$dashboardOk = Normalize-Bool (Get-JsonValue $dashboard "ok" $false)
$driver = [string](Get-JsonValue $dashboard "driver" "unknown")
$failedChecks = @((Get-JsonValue $dashboard "failedChecks" @()))
$warnings = @((Get-JsonValue $dashboard "warnings" @()))
$dashboardSensitiveHits = @((Get-JsonValue $dashboard "sensitiveHits" @()))
foreach ($hit in $dashboardSensitiveHits) {
    if (-not [string]::IsNullOrWhiteSpace([string]$hit)) {
        [void]$sensitiveHits.Add([string]$hit)
    }
}

$severity = if ($sensitiveHits.Count -gt 0) {
    "critical"
} elseif ($dashboardStatus -eq "unhealthy" -or $failedChecks.Count -gt 0) {
    "warning"
} elseif ($dashboardStatus -eq "healthy" -and $dashboardOk) {
    "info"
} else {
    "unknown"
}
$notify = $severity -eq "critical" -or $severity -eq "warning" -or $dashboardStatus -eq "unknown"
$message = ("QtNetworkChat database health {0}: driver={1}, failedChecks={2}, warnings={3}, sensitiveHits={4}" -f
    $dashboardStatus, $driver, $failedChecks.Count, $warnings.Count, $sensitiveHits.Count)
Assert-NoSensitiveText "notification message" $message
Assert-NoSensitiveText "webhook url" $WebhookUrl

$notified = $false
if ($notify -and -not [string]::IsNullOrWhiteSpace($EventLogSource)) {
    if ($DryRun) {
        Write-Host ("dry-run: would write EventLog entry: Source={0} Message={1}" -f $EventLogSource, $message)
    } else {
        try {
            if ([System.Diagnostics.EventLog]::SourceExists($EventLogSource)) {
                Write-EventLog -LogName Application -Source $EventLogSource -EventId 9101 -EntryType Warning -Message $message
                $notified = $true
                Write-Host ("EventLog entry written: Source={0} EventId=9101" -f $EventLogSource)
            } else {
                Write-Host ("EventLog source '{0}' not registered; skipping EventLog write" -f $EventLogSource)
            }
        } catch {
            Write-Host ("EventLog write failed (non-fatal): {0}" -f $_.Exception.Message)
        }
    }
}

if ($notify -and -not [string]::IsNullOrWhiteSpace($WebhookUrl)) {
    $payload = [ordered]@{
        text = $message
        status = $dashboardStatus
        severity = $severity
        driver = $driver
        failedChecks = $failedChecks
        warningCount = $warnings.Count
        sensitiveHitCount = $sensitiveHits.Count
    } | ConvertTo-Json -Depth 5
    if ($DryRun) {
        Write-Host ("dry-run: would POST to webhook: {0}" -f $WebhookUrl)
        Write-Host ("dry-run: payload: {0}" -f $payload)
    } else {
        try {
            Invoke-RestMethod -Uri $WebhookUrl -Method Post -Body $payload -ContentType "application/json" -TimeoutSec 10 | Out-Null
            $notified = $true
            Write-Host ("webhook POST succeeded: {0}" -f $WebhookUrl)
        } catch {
            Write-Host ("webhook POST failed (non-fatal): {0}" -f $_.Exception.Message)
        }
    }
}

$alert = [ordered]@{
    format = "qtnetworkchat-database-health-alert-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    status = $dashboardStatus
    ok = [bool]($dashboardStatus -eq "healthy" -and $dashboardOk -and $sensitiveHits.Count -eq 0)
    severity = $severity
    notify = [bool]$notify
    notified = [bool]$notified
    dryRun = [bool]$DryRun.IsPresent
    driver = $driver
    failedChecks = @($failedChecks)
    warnings = @($warnings)
    sensitiveHits = @($sensitiveHits)
    message = $message
    dashboardPath = $resolvedDashboardPath
}

if (-not [string]::IsNullOrWhiteSpace($resolvedAlertPath)) {
    $alertParent = Split-Path -Parent $resolvedAlertPath
    if (-not [string]::IsNullOrWhiteSpace($alertParent)) {
        New-Item -ItemType Directory -Path $alertParent -Force | Out-Null
    }
    $alert | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedAlertPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($resolvedMarkdownPath)) {
    $markdownParent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
        New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat Database Health Alert")
    $lines.Add("")
    $lines.Add(('- Status: `{0}`' -f $alert.status))
    $lines.Add(('- Severity: `{0}`' -f $alert.severity))
    $lines.Add(('- OK: `{0}`' -f (Format-Value $alert.ok)))
    $lines.Add(('- Notify: `{0}`' -f (Format-Value $alert.notify)))
    $lines.Add(('- Driver: `{0}`' -f (Format-Value $alert.driver)))
    $lines.Add(('- Failed checks: `{0}`' -f (@($alert.failedChecks) -join ", ")))
    $lines.Add(('- Warnings: `{0}`' -f (@($alert.warnings) -join ", ")))
    $lines.Add(('- Sensitive hits: `{0}`' -f @($alert.sensitiveHits).Count))
    $lines.Add("")
    $lines.Add("This alert is generated from a local redacted database health dashboard. It does not connect to PostgreSQL, Redis, S3, or MinIO, and it does not modify application data.")
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

Write-Host "database health alert"
Write-Host ("  status: {0}" -f $alert.status)
Write-Host ("  severity: {0}" -f $alert.severity)
Write-Host ("  notify: {0}" -f $alert.notify)
$alert | ConvertTo-Json -Depth 8

if ($FailOnUnhealthy -and -not $alert.ok) {
    exit 2
}
