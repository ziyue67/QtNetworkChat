param(
    [Parameter(Mandatory = $true)]
    [string[]]$LastRunPath,

    [string]$AckPath,

    [int]$AckExpiryHours = 0,

    [int]$RetentionCount = 0,

    [string]$JsonPath,

    [string]$MarkdownPath,

    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'session[-_\s]?token',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Read-OptionalJson([string]$PathValue) {
    $resolvedPath = Resolve-OptionalPath $PathValue
    if ([string]::IsNullOrWhiteSpace($resolvedPath) -or -not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8
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

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    $resolvedPath = Resolve-OptionalPath $PathValue
    if ([string]::IsNullOrWhiteSpace($resolvedPath) -or -not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $resolvedPath -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "<redacted>") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $resolvedPath), $lineNumber, $pattern))
            }
        }
    }
}

function Parse-LastRunLine([string]$LineValue, [string]$SourceName) {
    $timestamp = "unknown"
    $exitCode = -1
    if ($LineValue -match '^([0-9]{4}-[0-9]{2}-[0-9]{2}T[^ ]+)') {
        $timestamp = $Matches[1]
    }
    if ($LineValue -match '(?:^|\s)exitCode=([0-9]+)') {
        $exitCode = [int]$Matches[1]
    }
    [pscustomobject]@{
        source = $SourceName
        timestamp = $timestamp
        exitCode = $exitCode
        ok = $exitCode -eq 0
    }
}

function Parse-UtcDate([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }
    $parsed = [datetimeoffset]::MinValue
    if ([datetimeoffset]::TryParse($Value, [ref]$parsed)) {
        return $parsed.ToUniversalTime()
    }
    $null
}

function Format-HourNumber([double]$Value) {
    if ($Value -lt 0) {
        $Value = 0
    }
    [math]::Round($Value, 2)
}

$normalizedLastRunPaths = @()
foreach ($path in $LastRunPath) {
    foreach ($part in ([string]$path -split ",")) {
        $trimmed = $part.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedLastRunPaths += $trimmed
        }
    }
}

$sensitiveHits = New-Object System.Collections.ArrayList
$runs = @()
foreach ($path in $normalizedLastRunPaths) {
    Add-SensitiveHits $path $sensitiveHits
    $resolvedPath = Resolve-OptionalPath $path
    if ([string]::IsNullOrWhiteSpace($resolvedPath) -or -not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        continue
    }
    foreach ($line in @(Get-Content -LiteralPath $resolvedPath -Encoding UTF8)) {
        $text = ([string]$line).Trim()
        if (-not [string]::IsNullOrWhiteSpace($text)) {
            $runs += (Parse-LastRunLine $text (Split-Path -Leaf $resolvedPath))
        }
    }
}

$ack = Read-OptionalJson $AckPath
if (-not [string]::IsNullOrWhiteSpace($AckPath)) {
    Add-SensitiveHits $AckPath $sensitiveHits
}
$acknowledged = [bool](Get-JsonValue $ack "acknowledged" $false)
$acknowledgedBy = [string](Get-JsonValue $ack "acknowledgedBy" "")
$acknowledgedAt = [string](Get-JsonValue $ack "acknowledgedAt" "")
$ackReason = [string](Get-JsonValue $ack "reason" "")
$ackExpired = $false
$ackTime = $null
$ackAgeHours = "unknown"
$ackExpiresAt = "unknown"
$ackHoursRemaining = "unknown"
$ackHoursOverdue = "unknown"

if ($RetentionCount -gt 0 -and $runs.Count -gt $RetentionCount) {
    $runs = @($runs | Sort-Object timestamp -Descending | Select-Object -First $RetentionCount)
}

if ($acknowledged) {
    $ackTime = Parse-UtcDate $acknowledgedAt
    if ($null -ne $ackTime) {
        $nowUtc = (Get-Date).ToUniversalTime()
        $expiryAge = ($nowUtc - $ackTime.UtcDateTime).TotalHours
        $ackAgeHours = Format-HourNumber $expiryAge
        if ($AckExpiryHours -gt 0) {
            $expiresAtUtc = $ackTime.UtcDateTime.AddHours($AckExpiryHours)
            $ackExpiresAt = $expiresAtUtc.ToString("o")
            $remainingHours = ($expiresAtUtc - $nowUtc).TotalHours
            if ($remainingHours -ge 0) {
                $ackHoursRemaining = Format-HourNumber $remainingHours
                $ackHoursOverdue = 0
            } else {
                $ackHoursRemaining = 0
                $ackHoursOverdue = Format-HourNumber (-$remainingHours)
            }
        }
    }
}

if ($AckExpiryHours -gt 0 -and $acknowledged) {
    if ($null -eq $ackTime) {
        $ackExpired = $true
    } else {
        $expiryAge = ((Get-Date).ToUniversalTime() - $ackTime.UtcDateTime).TotalHours
        if ($expiryAge -gt $AckExpiryHours) {
            $ackExpired = $true
        }
    }
    if ($ackExpired) {
        $acknowledged = $false
        if ($ackReason -eq "") {
            $ackReason = "expired"
        } else {
            $ackReason = "$ackReason;expired"
        }
    }
}

$latestRun = $null
if ($runs.Count -gt 0) {
    $latestRun = @($runs | Sort-Object timestamp -Descending | Select-Object -First 1)[0]
}
$failedRuns = @($runs | Where-Object { -not $_.ok })
$safeAcknowledgedBy = if ([string]::IsNullOrWhiteSpace($acknowledgedBy)) { "unknown" } else { $acknowledgedBy }
$safeAcknowledgedAt = if ([string]::IsNullOrWhiteSpace($acknowledgedAt)) { "unknown" } else { $acknowledgedAt }
$safeAckReason = if ([string]::IsNullOrWhiteSpace($ackReason)) { "unknown" } else { $ackReason }
$safeSensitiveHits = @($sensitiveHits)
$safeRuns = @($runs)
$ackReminder = "not-required"
if ($failedRuns.Count -gt 0) {
    if ($ackExpired) {
        $ackReminder = "renew-required"
    } elseif (-not $acknowledged) {
        $ackReminder = "acknowledge-required"
    } else {
        $remainingReminderHours = 0.0
        if ([double]::TryParse([string]$ackHoursRemaining, [ref]$remainingReminderHours) -and $remainingReminderHours -le 24) {
            $ackReminder = "renew-soon"
        } else {
            $ackReminder = "acknowledged"
        }
    }
}

$summary = [ordered]@{
    format = "qtnetworkchat-automation-task-history-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    runCount = $runs.Count
    failedRunCount = $failedRuns.Count
    latestRun = $latestRun
    acknowledged = $acknowledged
    ackExpired = $ackExpired
    acknowledgedBy = $safeAcknowledgedBy
    acknowledgedAt = $safeAcknowledgedAt
    ackReason = $safeAckReason
    ackExpiryHours = $AckExpiryHours
    ackAgeHours = $ackAgeHours
    ackExpiresAt = $ackExpiresAt
    ackHoursRemaining = $ackHoursRemaining
    ackHoursOverdue = $ackHoursOverdue
    ackReminder = $ackReminder
    sensitiveHits = $safeSensitiveHits
    runs = $safeRuns
}

if (-not [string]::IsNullOrWhiteSpace($JsonPath)) {
    $resolvedJsonPath = Resolve-OptionalPath $JsonPath
    $parent = Split-Path -Parent $resolvedJsonPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedJsonPath -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
    $parent = Split-Path -Parent $resolvedMarkdownPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# QtNetworkChat Automation Task History")
    $lines.Add("")
    $lines.Add(('- Run count: `{0}`' -f $summary.runCount))
    $lines.Add(('- Failed runs: `{0}`' -f $summary.failedRunCount))
    $lines.Add(('- Acknowledged: `{0}`' -f $summary.acknowledged.ToString().ToLowerInvariant()))
    $lines.Add(('- Ack expired: `{0}`' -f $summary.ackExpired.ToString().ToLowerInvariant()))
    $lines.Add(('- Acknowledged by: `{0}`' -f $summary.acknowledgedBy))
    $lines.Add(('- Acknowledged at: `{0}`' -f $summary.acknowledgedAt))
    $lines.Add(('- Ack reminder: `{0}`' -f $summary.ackReminder))
    $lines.Add(('- Ack expiry hours: `{0}`' -f $summary.ackExpiryHours))
    $lines.Add(('- Ack hours remaining: `{0}`' -f $summary.ackHoursRemaining))
    $lines.Add(('- Ack hours overdue: `{0}`' -f $summary.ackHoursOverdue))
    $lines.Add("")
    $lines.Add("| Source | Timestamp | Exit Code | OK |")
    $lines.Add("| --- | --- | ---: | --- |")
    foreach ($run in $runs) {
        $lines.Add(("| {0} | {1} | {2} | {3} |" -f $run.source, $run.timestamp, $run.exitCode, $run.ok.ToString().ToLowerInvariant()))
    }
    $lines | Set-Content -LiteralPath $resolvedMarkdownPath -Encoding UTF8
}

$summary | ConvertTo-Json -Depth 6
if ($FailOnSensitive -and $sensitiveHits.Count -gt 0) {
    exit 2
}
