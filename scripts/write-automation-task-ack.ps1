param(
    [Parameter(Mandatory = $true)]
    [string]$AckPath,

    [string]$AcknowledgedBy,

    [string]$Reason,

    [string]$AcknowledgedAt,

    [switch]$Clear,

    [switch]$PlanOnly,

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

function Assert-NoSensitiveValue([string]$Label, [string[]]$Values) {
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($pattern in $sensitivePatterns) {
            if ($value -match $pattern -and $value -notmatch "<redacted>") {
                throw ("{0} contains sensitive-looking text rejected by automation ack writer: {1}" -f $Label, $pattern)
            }
        }
    }
}

function Parse-UtcTimestamp([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return (Get-Date).ToUniversalTime().ToString("o")
    }
    $parsed = [datetimeoffset]::MinValue
    if (-not [datetimeoffset]::TryParse($Value, [ref]$parsed)) {
        throw "AcknowledgedAt must be a valid timestamp."
    }
    $parsed.ToUniversalTime().ToString("o")
}

Assert-NoSensitiveValue "AckPath" @($AckPath)
Assert-NoSensitiveValue "AcknowledgedBy" @($AcknowledgedBy)
Assert-NoSensitiveValue "Reason" @($Reason)
Assert-NoSensitiveValue "AcknowledgedAt" @($AcknowledgedAt)

if (-not $Clear.IsPresent -and [string]::IsNullOrWhiteSpace($AcknowledgedBy)) {
    throw "AcknowledgedBy is required unless -Clear is specified."
}

$resolvedAckPath = Resolve-OptionalPath $AckPath
$acknowledged = -not $Clear.IsPresent
$safeAcknowledgedBy = if ($acknowledged) { $AcknowledgedBy.Trim() } else { "cleared" }
$safeReason = if ([string]::IsNullOrWhiteSpace($Reason)) {
    if ($acknowledged) { "acknowledged" } else { "cleared" }
} else {
    $Reason.Trim()
}
$safeAcknowledgedAt = if ($acknowledged) { Parse-UtcTimestamp $AcknowledgedAt } else { "" }

$payload = [ordered]@{
    format = "qtnetworkchat-automation-task-ack-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    acknowledged = $acknowledged
    acknowledgedBy = $safeAcknowledgedBy
    acknowledgedAt = $safeAcknowledgedAt
    reason = $safeReason
}

$json = $payload | ConvertTo-Json -Depth 4

if (-not $PlanOnly.IsPresent) {
    $parent = Split-Path -Parent $resolvedAckPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $json | Set-Content -LiteralPath $resolvedAckPath -Encoding UTF8
    Write-Host ("automation task ack: {0}" -f $resolvedAckPath)
} else {
    Write-Output $json
}

if ($FailOnSensitive) {
    Assert-NoSensitiveValue "AckPayload" @($json)
}
