param(
    [string]$MarkdownPath = "docs\automation-status.md",
    [string]$Head,
    [string]$OriginMain,
    [string]$OriginCodexQt,
    [string]$CiStatus = "unknown",
    [string]$CiRunId = "",
    [string]$BuildStatus = "unknown",
    [string]$CTestStatus = "unknown",
    [int]$CTestCount = 0,
    [string]$DatabaseHealthStatusPath,
    [string]$DatabaseHealthLastRunPath,
    [string]$DatabaseHealthTaskPreviewPath,
    [string]$LargeFileGovernanceStatusPath,
    [string]$LargeFileGovernanceLastRunPath,
    [string]$LargeFileGovernanceTaskPreviewPath,
    [string]$AutomationTaskHistoryPath,
    [string[]]$ProtectedUntracked = @(".polaris/", "AGENTS.md"),
    [switch]$PlanOnly,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    'password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'GPG(?:_|-|\s)?(passphrase|password|secret)["'']?\s*[:=]',
    'passphrase["'']?\s*[:=]',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    'secret[-_\s]?key',
    'access[-_\s]?key',
    'session[-_\s]?token',
    'Authorization\s*[:=]',
    'Credential\s*=',
    'Signature\s*='
)

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Invoke-GitText([string[]]$Arguments) {
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & git @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) {
            $global:LASTEXITCODE = 0
            return ""
        }
        $global:LASTEXITCODE = 0
        return ((@($output) -join "`n").Trim())
    } catch {
        $global:LASTEXITCODE = 0
        return ""
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Find-SensitiveHits([string[]]$Lines) {
    $hits = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        foreach ($pattern in $sensitivePatterns) {
            if ($Lines[$i] -match $pattern -and $Lines[$i] -notmatch "<redacted>") {
                $hits.Add(("line {0}: {1}" -f ($i + 1), $pattern))
            }
        }
    }
    $hits
}

function Read-JsonSummary([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }
    $resolvedPath = Resolve-RepoPath $PathValue
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    $raw | ConvertFrom-Json
}

function Resolve-PreviewValue([string]$PreviewPath, [string]$PropertyName) {
    $preview = Read-JsonSummary $PreviewPath
    if ($null -eq $preview) {
        return ""
    }
    $value = Get-JsonValue $preview $PropertyName ""
    if ([string]::IsNullOrWhiteSpace([string]$value)) {
        return ""
    }
    [string]$value
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

function Format-StatusValue([object]$Value) {
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

function Read-LastRunSummary([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $null
    }
    $resolvedPath = Resolve-RepoPath $PathValue
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return $null
    }
    $text = (Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        return $null
    }
    $timestamp = "unknown"
    $exitCode = "unknown"
    if ($text -match '^([0-9]{4}-[0-9]{2}-[0-9]{2}T[^ ]+)') {
        $timestamp = $Matches[1]
    }
    if ($text -match '(?:^|\s)exitCode=([0-9]+)') {
        $exitCode = $Matches[1]
    }
    [pscustomobject]@{
        timestamp = $timestamp
        exitCode = $exitCode
    }
}

if ([string]::IsNullOrWhiteSpace($Head)) {
    $Head = if ($PlanOnly) { "unknown" } else { Invoke-GitText @("rev-parse", "--short=12", "HEAD") }
}
if ([string]::IsNullOrWhiteSpace($OriginMain)) {
    $OriginMain = if ($PlanOnly) { "unknown" } else { Invoke-GitText @("rev-parse", "--short=12", "origin/main") }
}
if ([string]::IsNullOrWhiteSpace($OriginCodexQt)) {
    $OriginCodexQt = if ($PlanOnly) { "unknown" } else { Invoke-GitText @("rev-parse", "--short=12", "origin/codex/qt") }
}
if ([string]::IsNullOrWhiteSpace($DatabaseHealthStatusPath)) {
    $DatabaseHealthStatusPath = Resolve-PreviewValue $DatabaseHealthTaskPreviewPath "statusPath"
}
if ([string]::IsNullOrWhiteSpace($DatabaseHealthLastRunPath)) {
    $DatabaseHealthLastRunPath = Resolve-PreviewValue $DatabaseHealthTaskPreviewPath "logPath"
}
if ([string]::IsNullOrWhiteSpace($LargeFileGovernanceStatusPath)) {
    $LargeFileGovernanceStatusPath = Resolve-PreviewValue $LargeFileGovernanceTaskPreviewPath "dashboardPath"
}
if ([string]::IsNullOrWhiteSpace($LargeFileGovernanceLastRunPath)) {
    $previewLogPath = Resolve-PreviewValue $LargeFileGovernanceTaskPreviewPath "logPath"
    if (-not [string]::IsNullOrWhiteSpace($previewLogPath)) {
        $LargeFileGovernanceLastRunPath = $previewLogPath
    } else {
        $LargeFileGovernanceLastRunPath = Resolve-PreviewValue $LargeFileGovernanceTaskPreviewPath "launcherPath"
        if (-not [string]::IsNullOrWhiteSpace($LargeFileGovernanceLastRunPath)) {
            $LargeFileGovernanceLastRunPath = Join-Path (Split-Path -Parent (Resolve-RepoPath $LargeFileGovernanceLastRunPath)) "last-run.log"
        }
    }
}
if ([string]::IsNullOrWhiteSpace($AutomationTaskHistoryPath)) {
    $AutomationTaskHistoryPath = Resolve-PreviewValue $DatabaseHealthTaskPreviewPath "historyPath"
    if ([string]::IsNullOrWhiteSpace($AutomationTaskHistoryPath)) {
        $AutomationTaskHistoryPath = Resolve-PreviewValue $LargeFileGovernanceTaskPreviewPath "historyPath"
    }
}

$normalizedProtectedUntracked = @()
foreach ($entry in $ProtectedUntracked) {
    foreach ($part in ([string]$entry -split ",")) {
        $trimmed = $part.Trim()
        if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
            $normalizedProtectedUntracked += $trimmed
        }
    }
}
$protectedText = if ($normalizedProtectedUntracked.Count -gt 0) { $normalizedProtectedUntracked -join ", " } else { "none" }
$generatedAt = (Get-Date).ToUniversalTime().ToString("o")
$databaseHealthStatus = Read-JsonSummary $DatabaseHealthStatusPath
$databaseHealthLastRun = Read-LastRunSummary $DatabaseHealthLastRunPath
$largeFileGovernanceStatus = Read-JsonSummary $LargeFileGovernanceStatusPath
$largeFileGovernanceLastRun = Read-LastRunSummary $LargeFileGovernanceLastRunPath
$automationTaskHistory = Read-JsonSummary $AutomationTaskHistoryPath

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# QtNetworkChat Automation Status")
$lines.Add("")
$lines.Add('Generated by `scripts/write-automation-status.ps1`. This file is intentionally sanitized; do not add passwords, tokens, GPG passphrases, endpoints with credentials, or local private paths containing secrets.')
$lines.Add("")
$lines.Add("## Current Baseline")
$lines.Add("")
$lines.Add('- Generated at: `' + $generatedAt + '`')
$lines.Add('- HEAD: `' + $Head + '`')
$lines.Add('- origin/main: `' + $OriginMain + '`')
$lines.Add('- origin/codex/qt: `' + $OriginCodexQt + '`')
$lines.Add('- GitHub Windows Build: `' + $CiStatus + '`')
$lines.Add('- GitHub run id: `' + $(if ([string]::IsNullOrWhiteSpace($CiRunId)) { "unknown" } else { $CiRunId }) + '`')
$lines.Add('- Local MinGW build: `' + $BuildStatus + '`')
$lines.Add('- Local CTest: `' + $CTestStatus + '`')
$lines.Add('- Local CTest count: `' + $CTestCount + '`')
$lines.Add('- Protected untracked entries: `' + $protectedText + '`')
$lines.Add("")
$lines.Add("## Automation Guardrails")
$lines.Add("")
$lines.Add('- Start each loop by checking repository build/test processes and `git status`.')
$lines.Add('- Preserve `.polaris/`, `AGENTS.md`, `CLAUDE.md`, and unrelated user changes.')
$lines.Add("- Use large cross-artifact slices; avoid tiny README-only or one-field patches.")
$lines.Add("- Keep PostgreSQL passwords, GPG passphrases, GitHub tokens, S3 credentials, and signed URLs out of source, docs, logs, previews, launchers, commits, and remote URLs.")
$lines.Add("- Verify with the PowerShell timeout wrappers: build 600 seconds, CTest 900 seconds.")
$lines.Add('- Use signed Conventional Commits and push `main`, then fast-forward `codex/qt`.')
$lines.Add("")
$lines.Add("## Scheduled Task Readback")
$lines.Add("")
if ($null -eq $databaseHealthStatus) {
    $lines.Add('- Database health: `not configured`')
} else {
    $dbQueryMetrics = Get-JsonValue $databaseHealthStatus "queryMetrics" $null
    $dbFailedChecks = @((Get-JsonValue $databaseHealthStatus "failedChecks" @()))
    $lines.Add(('- Database health: status=`{0}`, ok=`{1}`, driver=`{2}`, checks=`{3}`, failedChecks=`{4}`, slowQueries=`{5}`, queryFailures=`{6}`' -f
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "status" "unknown")),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "ok" $null)),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "driver" "unknown")),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "checkCount" "unknown")),
            $dbFailedChecks.Count,
            (Format-StatusValue (Get-JsonValue $dbQueryMetrics "slowQueryCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $dbQueryMetrics "queryFailureCount" "unknown"))))
}
if ($null -ne $databaseHealthLastRun) {
    $lines.Add(('- Database health last run: at=`{0}`, exitCode=`{1}`' -f
            (Format-StatusValue $databaseHealthLastRun.timestamp),
            (Format-StatusValue $databaseHealthLastRun.exitCode)))
}
if ($null -eq $largeFileGovernanceStatus) {
    $lines.Add('- Large-file governance: `not configured`')
} else {
    $governanceGapAreas = @((Get-JsonValue $largeFileGovernanceStatus "s3CoverageActionableGapAreas" @()))
    $lines.Add(('- Large-file governance: status=`{0}`, ok=`{1}`, warnings=`{2}`, alerts=`{3}`, actionableS3Gaps=`{4}`' -f
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "status" "unknown")),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "ok" $null)),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "totalWarnings" "unknown")),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "alertCount" "unknown")),
            $governanceGapAreas.Count))
}
if ($null -ne $largeFileGovernanceLastRun) {
    $lines.Add(('- Large-file governance last run: at=`{0}`, exitCode=`{1}`' -f
            (Format-StatusValue $largeFileGovernanceLastRun.timestamp),
            (Format-StatusValue $largeFileGovernanceLastRun.exitCode)))
}
if ($null -ne $automationTaskHistory) {
    $historyLatestRun = Get-JsonValue $automationTaskHistory "latestRun" $null
    $lines.Add(('- Task history: runs=`{0}`, failed=`{1}`, latestAt=`{2}`, latestExitCode=`{3}`, acknowledged=`{4}`, ackExpired=`{5}`' -f
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "runCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "failedRunCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $historyLatestRun "timestamp" "unknown")),
            (Format-StatusValue (Get-JsonValue $historyLatestRun "exitCode" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "acknowledged" $null)),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "ackExpired" $null))))
}
$lines.Add("")
$lines.Add("## Priority Backlog")
$lines.Add("")
$lines.Add("1. Split heavy README sections into focused docs for testing coverage, PostgreSQL operations, large-file governance, and E2E hardening status.")
$lines.Add("2. Extend scheduled-task status readback with automatic history retention and acknowledgement expiry policy.")
$lines.Add("3. Continue real PostgreSQL/QPSQL boundary coverage for file chunk recovery, rollback audit, slow-query/error metrics, and connection-pool threading policy.")
$lines.Add("4. Productize E2E encryption with authenticated key agreement, real rotation, default policy, history migration, and file/chunk encryption.")
$lines.Add("5. Extend release automation with MinGW CI or release artifact path/version governance.")
$lines.Add("")
$lines.Add("## Last Local Verification")
$lines.Add("")
$lines.Add('- Build command: `cmake --build build-qt6-mingw` with 600 second timeout.')
$lines.Add('- Test command: `ctest --test-dir build-qt6-mingw --output-on-failure` with 900 second timeout.')
$lines.Add('- Real PostgreSQL smoke should use `QTNETWORKCHAT_PGPASSWORD` from the environment and `-EnsureDatabase`; generated evidence must remain redacted.')

$sensitiveHits = Find-SensitiveHits $lines
if ($sensitiveHits.Count -gt 0) {
    Write-Host "sensitive hits"
    foreach ($hit in $sensitiveHits) {
        Write-Host ("  {0}" -f $hit)
    }
    if ($FailOnSensitive) {
        exit 2
    }
}

if (-not $PlanOnly) {
    $target = Resolve-RepoPath $MarkdownPath
    $parent = Split-Path -Parent $target
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $target -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
    Write-Host ("automation status: {0}" -f $target)
} else {
    Write-Output ($lines -join [Environment]::NewLine)
}

exit 0
