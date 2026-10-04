param(
    [string]$OutputDir = "build-qt6-mingw\automation-tasks\ack-drill",
    [string]$AckScriptPath,
    [string]$HistoryScriptPath,
    [string]$AcknowledgedBy = "oncall-drill",
    [string]$Reason = "ack-drill",
    [string]$FailedAt,
    [string]$AcknowledgedAt,
    [int]$AckExpiryHours = 72,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-script-common.ps1")

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


function Resolve-OptionalPath([string]$PathValue, [string]$DefaultPath) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return (Resolve-RequiredRepoPath $DefaultPath)
    }
    Resolve-RequiredRepoPath $PathValue
}

function Assert-NoSensitiveValue([string]$Label, [string[]]$Values) {
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($pattern in $sensitivePatterns) {
            if ($value -match $pattern -and $value -notmatch "<redacted>") {
                throw ("{0} contains sensitive-looking text rejected by automation ack drill: {1}" -f $Label, $pattern)
            }
        }
    }
}

function Convert-ToUtcString([string]$Value, [datetimeoffset]$Fallback) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $Fallback.ToUniversalTime().ToString("o")
    }
    $parsed = [datetimeoffset]::MinValue
    if (-not [datetimeoffset]::TryParse($Value, [ref]$parsed)) {
        throw "Timestamp must be a valid date/time value."
    }
    $parsed.ToUniversalTime().ToString("o")
}

function Read-JsonFile([string]$PathValue) {
    $raw = Get-Content -LiteralPath $PathValue -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw "JSON artifact is empty: $PathValue"
    }
    $raw | ConvertFrom-Json -ErrorAction Stop
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
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

function Format-Value([object]$Value) {
    if ($null -eq $Value) {
        return "unknown"
    }
    if ($Value -is [bool]) {
        return $Value.ToString().ToLowerInvariant()
    }
    $text = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        return "unknown"
    }
    $text
}

Assert-NoSensitiveValue "OutputDir" @($OutputDir)
Assert-NoSensitiveValue "AcknowledgedBy" @($AcknowledgedBy)
Assert-NoSensitiveValue "Reason" @($Reason)
Assert-NoSensitiveValue "FailedAt" @($FailedAt)
Assert-NoSensitiveValue "AcknowledgedAt" @($AcknowledgedAt)

$resolvedOutputDir = Resolve-RequiredRepoPath $OutputDir
$resolvedAckScriptPath = Resolve-OptionalPath $AckScriptPath "scripts\write-automation-task-ack.ps1"
$resolvedHistoryScriptPath = Resolve-OptionalPath $HistoryScriptPath "scripts\write-automation-task-history.ps1"
if (-not (Test-Path -LiteralPath $resolvedAckScriptPath -PathType Leaf)) {
    throw "Ack script not found: $resolvedAckScriptPath"
}
if (-not (Test-Path -LiteralPath $resolvedHistoryScriptPath -PathType Leaf)) {
    throw "History script not found: $resolvedHistoryScriptPath"
}

New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null
$lastRunPath = Join-Path $resolvedOutputDir "ack-drill-last-run.log"
$ackPath = Join-Path $resolvedOutputDir "automation-task-ack.json"
$historyJsonPath = Join-Path $resolvedOutputDir "automation-task-history.json"
$historyMarkdownPath = Join-Path $resolvedOutputDir "automation-task-history.md"
$drillJsonPath = Join-Path $resolvedOutputDir "automation-ack-drill.json"
$drillMarkdownPath = Join-Path $resolvedOutputDir "automation-ack-drill.md"

$now = [datetimeoffset]::UtcNow
$safeFailedAt = Convert-ToUtcString $FailedAt $now.AddMinutes(-30)
$safeAcknowledgedAt = Convert-ToUtcString $AcknowledgedAt $now

("{0} exitCode=1 taskKind=ack-drill statusPath=redacted reason=ack-drill-sample-failure" -f $safeFailedAt) |
    Set-Content -LiteralPath $lastRunPath -Encoding UTF8

$ackArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $resolvedAckScriptPath,
    "-AckPath", $ackPath,
    "-AcknowledgedBy", $AcknowledgedBy,
    "-Reason", $Reason,
    "-AcknowledgedAt", $safeAcknowledgedAt
)
if ($FailOnSensitive.IsPresent) {
    $ackArguments += "-FailOnSensitive"
}
& powershell @ackArguments | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Automation ack drill failed to write ack evidence with exit code $LASTEXITCODE"
}

$historyArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $resolvedHistoryScriptPath,
    "-LastRunPath", $lastRunPath,
    "-AckPath", $ackPath,
    "-AckExpiryHours", $AckExpiryHours,
    "-JsonPath", $historyJsonPath,
    "-MarkdownPath", $historyMarkdownPath
)
if ($FailOnSensitive.IsPresent) {
    $historyArguments += "-FailOnSensitive"
}
& powershell @historyArguments | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Automation ack drill failed to write history evidence with exit code $LASTEXITCODE"
}

$ack = Read-JsonFile $ackPath
$history = Read-JsonFile $historyJsonPath
$failedRunCount = [int]$history.failedRunCount
$acknowledged = [bool]$ack.acknowledged
$historyAcknowledged = [bool]$history.acknowledged
$ackExpired = [bool]$history.ackExpired
$ackReminder = Format-Value $history.ackReminder

$state = "exercised"
$releaseGate = "automation-ack-drill-exercised"
$operatorAction = "Keep live task acknowledgements tied to real failures; this drill proves the acknowledgement path without mutating live task history."
if ($failedRunCount -le 0) {
    $state = "no-failure"
    $releaseGate = "blocked-ack-drill-no-failure"
    $operatorAction = "Regenerate ack drill evidence with a failed sample run."
} elseif (-not $acknowledged -or -not $historyAcknowledged) {
    $state = "unacknowledged"
    $releaseGate = "blocked-ack-drill-unacknowledged"
    $operatorAction = "Acknowledge the ack drill sample failure and regenerate evidence."
} elseif ($ackExpired) {
    $state = "expired"
    $releaseGate = "blocked-ack-drill-expired"
    $operatorAction = "Renew the ack drill sample acknowledgement and regenerate evidence."
}

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in @($lastRunPath, $ackPath, $historyJsonPath, $historyMarkdownPath)) {
    Add-SensitiveHits $path $sensitiveHits
}

$summary = [ordered]@{
    format = "qtnetworkchat-automation-ack-drill-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    state = $state
    ok = $state -eq "exercised"
    releaseGate = $releaseGate
    failedRunCount = $failedRunCount
    acknowledged = $acknowledged
    historyAcknowledged = $historyAcknowledged
    ackExpired = $ackExpired
    ackReminder = $ackReminder
    ackExpiryHours = [int]$history.ackExpiryHours
    acknowledgedBy = Format-Value $ack.acknowledgedBy
    acknowledgedAt = Format-Value $ack.acknowledgedAt
    reason = Format-Value $ack.reason
    liveTaskMutation = $false
    operatorAction = $operatorAction
    artifacts = [ordered]@{
        lastRun = Split-Path -Leaf $lastRunPath
        ack = Split-Path -Leaf $ackPath
        historyJson = Split-Path -Leaf $historyJsonPath
        historyMarkdown = Split-Path -Leaf $historyMarkdownPath
    }
    sensitiveHits = @($sensitiveHits.ToArray())
}

$summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $drillJsonPath -Encoding UTF8

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# QtNetworkChat Automation Ack Drill")
$lines.Add("")
$lines.Add(('- State: `{0}`' -f $summary.state))
$lines.Add(('- Release gate: `{0}`' -f $summary.releaseGate))
$lines.Add(('- Failed runs: `{0}`' -f $summary.failedRunCount))
$lines.Add(('- Acknowledged: `{0}`' -f $summary.acknowledged.ToString().ToLowerInvariant()))
$lines.Add(('- Ack expired: `{0}`' -f $summary.ackExpired.ToString().ToLowerInvariant()))
$lines.Add(('- Ack reminder: `{0}`' -f $summary.ackReminder))
$lines.Add(('- Live task mutation: `{0}`' -f $summary.liveTaskMutation.ToString().ToLowerInvariant()))
$lines.Add(('- Operator action: `{0}`' -f $summary.operatorAction))
$lines | Set-Content -LiteralPath $drillMarkdownPath -Encoding UTF8

Add-SensitiveHits $drillJsonPath $sensitiveHits
Add-SensitiveHits $drillMarkdownPath $sensitiveHits
if ($FailOnSensitive.IsPresent -and $sensitiveHits.Count -gt 0) {
    foreach ($hit in $sensitiveHits) {
        Write-Host ("sensitive hit: {0}" -f $hit)
    }
    exit 2
}

Write-Host ("automation ack drill: {0}" -f $drillJsonPath)
$summary | ConvertTo-Json -Depth 6
