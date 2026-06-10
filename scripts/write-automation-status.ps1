param(
    [string]$MarkdownPath = "docs\automation-status.md",
    [string]$Head,
    [string]$OriginMain,
    [string]$TrackedRemoteBranch = "origin/main",
    [string]$TrackedRemoteHash,
    [string]$CiStatus = "unknown",
    [string]$CiRunId = "",
    [string]$BuildStatus = "unknown",
    [string]$CTestStatus = "unknown",
    [int]$CTestCount = 0,
    [string]$BuildDir = "build-qt6-mingw",
    [string]$LocalVerificationStatusPath,
    [string]$CTestLogPath,
    [string]$GitHubWorkflow = "Windows Build",
    [string]$GitHubWindowsBuildPolicy = "",
    [string]$AutomationPolicyPath = "",
    [string]$GitHubWindowsBuildStatusPath,
    [string]$GitHubRunListJsonPath,
    [string]$E2ERolloutObservabilityJsonPath,
    [string]$E2ERolloutObservabilityMarkdownPath,
    [string]$E2EReleaseEvidenceManifestPath,
    [string]$E2ELinkedReleaseCandidateManifestPath,
    [string]$DatabaseHealthStatusPath,
    [string]$DatabaseHealthLastRunPath,
    [string]$DatabaseHealthTaskPreviewPath,
    [string]$LargeFileGovernanceStatusPath,
    [string]$LargeFileGovernanceLastRunPath,
    [string]$LargeFileGovernanceTaskPreviewPath,
    [string]$S3RealBackendReadinessPath,
    [string[]]$TaskPreviewPath = @(),
    [string]$ScheduledTaskReadbackJsonPath,
    [string]$AutomationTaskHistoryPath,
    [string]$AutomationTaskAckPath,
    [string]$AutomationAckDrillPath,
    [string]$ScheduledTaskRegistrationAttemptPath,
    [string]$ScheduledTaskRegistrationAckPath,
    [switch]$BootstrapDefaultTasks,
    [string]$DefaultTaskOutputDir = "build-qt6-mingw\automation-tasks",
    [switch]$RegisterDefaultTasks,
    [string]$DefaultTaskUser = "SYSTEM",
    [int]$TaskAckExpiryHours = 72,
    [int]$TaskHistoryRetentionCount = 30,
    [int]$TaskHistoryFreshnessHours = 24,
    [string]$StatusNowUtc = "",
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

function Invoke-ToolText([string]$CommandName, [string[]]$Arguments) {
    try {
        $output = & $CommandName @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) {
            $global:LASTEXITCODE = 0
            return ""
        }
        $global:LASTEXITCODE = 0
        return ((@($output) -join "`n").Trim())
    } catch {
        $global:LASTEXITCODE = 0
        return ""
    }
}

function Is-UnknownStatus([string]$Value) {
    [string]::IsNullOrWhiteSpace($Value) -or $Value.Trim().ToLowerInvariant() -eq "unknown"
}

function Normalize-GitHubWindowsBuildPolicy([string]$Value) {
    $normalized = ([string]$Value).Trim().ToLowerInvariant()
    switch ($normalized) {
        "disabled" { return "disabled" }
        "optional" { return "optional" }
        "required" { return "required" }
        default { return "" }
    }
}

function Get-AutomationPolicyReadback([string]$PathValue) {
    $result = [ordered]@{
        configured = $false
        readable = $false
        valid = $false
        githubWindowsBuildPolicy = ""
        source = "automation-policy"
    }
    if ([string]::IsNullOrWhiteSpace($PathValue) -or $script:PlanOnly.IsPresent) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    try {
        $resolved = Resolve-RepoPath $PathValue
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return [pscustomobject]$result
        }
        $policy = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $result.readable = $true
        if ((Get-JsonValue $policy "format" "") -ne "qtnetworkchat-automation-policy-v1") {
            $result.source = "automation-policy-invalid-format"
            return [pscustomobject]$result
        }
        $policyValue = Normalize-GitHubWindowsBuildPolicy ([string](Get-JsonValue $policy "gitHubWindowsBuildPolicy" ""))
        if ([string]::IsNullOrWhiteSpace($policyValue)) {
            $result.source = "automation-policy-invalid-github-windows-build-policy"
            return [pscustomobject]$result
        }
        $result.valid = $true
        $result.githubWindowsBuildPolicy = $policyValue
    } catch {
        $result.source = "automation-policy-unreadable"
    }
    [pscustomobject]$result
}

function Normalize-HeadValue([object]$Value) {
    if ($null -eq $Value) {
        return ""
    }
    $text = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($text) -or $text.ToLowerInvariant() -eq "unknown") {
        return ""
    }
    $text.ToLowerInvariant()
}

function Test-HeadMatch([string]$Expected, [string]$Actual) {
    $normalizedExpected = Normalize-HeadValue $Expected
    $normalizedActual = Normalize-HeadValue $Actual
    if ([string]::IsNullOrWhiteSpace($normalizedExpected) -or [string]::IsNullOrWhiteSpace($normalizedActual)) {
        return $false
    }
    $normalizedActual -eq $normalizedExpected `
        -or $normalizedActual.StartsWith($normalizedExpected) `
        -or $normalizedExpected.StartsWith($normalizedActual)
}

function Get-GitHubWindowsBuildStatusArtifactReadback([string]$PathValue, [string]$HeadSha) {
    $result = [ordered]@{
        configured = $false
        readable = $false
        valid = $false
        status = ""
        runId = ""
        source = "github-windows-build-status"
        visibility = ""
    }
    if ([string]::IsNullOrWhiteSpace($PathValue) -or $script:PlanOnly.IsPresent) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    try {
        $resolved = Resolve-RepoPath $PathValue
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return [pscustomobject]$result
        }
        $statusArtifact = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $result.readable = $true
        if ((Get-JsonValue $statusArtifact "format" "") -ne "qtnetworkchat-github-windows-build-status-v1") {
            $result.source = "github-windows-build-status-invalid-format"
            return [pscustomobject]$result
        }
        $artifactHead = ([string](Get-JsonValue $statusArtifact "headSha" "")).Trim().ToLowerInvariant()
        $normalizedHead = ([string]$HeadSha).Trim().ToLowerInvariant()
        if (-not [string]::IsNullOrWhiteSpace($artifactHead) `
                -and -not [string]::IsNullOrWhiteSpace($normalizedHead) `
                -and $artifactHead -ne "unknown" `
                -and $normalizedHead -ne "unknown" `
                -and $artifactHead -ne $normalizedHead) {
            $result.source = "github-windows-build-status-stale-head"
            $result.status = "external-visibility-stale"
            $result.visibility = "artifact-head-mismatch"
            return [pscustomobject]$result
        }
        $result.valid = $true
        $result.status = [string](Get-JsonValue $statusArtifact "status" "unknown")
        $result.runId = [string](Get-JsonValue $statusArtifact "runId" "")
        $result.source = [string](Get-JsonValue $statusArtifact "source" "github-windows-build-status")
        if ([string]::IsNullOrWhiteSpace($result.source)) {
            $result.source = "github-windows-build-status"
        }
        $result.visibility = [string](Get-JsonValue $statusArtifact "visibility" "")
    } catch {
        $result.source = "github-windows-build-status-unreadable"
    }
    [pscustomobject]$result
}

function Get-GitHubWindowsBuildReadback([string]$HeadSha) {
    $result = [ordered]@{
        status = "unknown"
        runId = ""
        source = "auto-gh-run-list-unavailable"
    }
    if ($script:PlanOnly.IsPresent) {
        $result.source = "plan-only"
        return [pscustomobject]$result
    }

    $runListJson = ""
    if (-not [string]::IsNullOrWhiteSpace($script:GitHubRunListJsonPath)) {
        try {
            $runListJson = Get-Content -LiteralPath (Resolve-RepoPath $script:GitHubRunListJsonPath) -Raw -Encoding UTF8
            $result.source = "json-artifact"
        } catch {
            $result.status = "unavailable"
            $result.source = "json-artifact-unreadable"
            return [pscustomobject]$result
        }
    } else {
        $statusArtifact = Get-GitHubWindowsBuildStatusArtifactReadback $script:GitHubWindowsBuildStatusPath $HeadSha
        if ($statusArtifact.configured -and $statusArtifact.valid) {
            $result.status = if ([string]::IsNullOrWhiteSpace($statusArtifact.status)) { "unknown" } else { $statusArtifact.status }
            $result.runId = $statusArtifact.runId
            $result.source = $statusArtifact.source
            if (-not [string]::IsNullOrWhiteSpace($statusArtifact.visibility)) {
                $result.source = $result.source + "/" + $statusArtifact.visibility
            }
            return [pscustomobject]$result
        } elseif ($statusArtifact.configured -and $statusArtifact.readable) {
            $result.status = if ([string]::IsNullOrWhiteSpace($statusArtifact.status)) { "unavailable" } else { $statusArtifact.status }
            $result.source = $statusArtifact.source
            return [pscustomobject]$result
        }

        $runListJson = Invoke-ToolText "gh" @(
            "run", "list",
            "--workflow", $script:GitHubWorkflow,
            "--limit", "20",
            "--json", "databaseId,headSha,status,conclusion,createdAt,displayTitle,workflowName"
        )
        if ([string]::IsNullOrWhiteSpace($runListJson)) {
            $result.status = "unavailable"
            return [pscustomobject]$result
        }
        $result.source = "auto-gh-run-list"
    }

    try {
        $runs = @($runListJson | ConvertFrom-Json -ErrorAction Stop)
    } catch {
        $result.status = "unavailable"
        $result.source = $result.source + "-invalid-json"
        return [pscustomobject]$result
    }
    if ($runs.Count -eq 0) {
        $result.status = "external-visibility-stale"
        $result.source = $result.source + "-empty"
        return [pscustomobject]$result
    }

    $normalizedHead = ([string]$HeadSha).Trim().ToLowerInvariant()
    $matchingRun = $null
    if (-not [string]::IsNullOrWhiteSpace($normalizedHead) -and $normalizedHead -ne "unknown") {
        foreach ($run in $runs) {
            $runHead = ([string]$run.headSha).Trim().ToLowerInvariant()
            $headMatches = -not [string]::IsNullOrWhiteSpace($runHead)
            if ($headMatches) {
                $headMatches = $runHead -eq $normalizedHead `
                    -or $runHead.StartsWith($normalizedHead) `
                    -or $normalizedHead.StartsWith($runHead)
            }
            if ($headMatches) {
                $matchingRun = $run
                break
            }
        }
    }

    if ($null -eq $matchingRun) {
        $result.status = "external-visibility-stale"
        return [pscustomobject]$result
    }

    $runStatus = ([string]$matchingRun.status).Trim().ToLowerInvariant()
    $runConclusion = ([string]$matchingRun.conclusion).Trim().ToLowerInvariant()
    if ($runStatus -eq "completed" -and -not [string]::IsNullOrWhiteSpace($runConclusion)) {
        $result.status = $runConclusion
    } elseif (-not [string]::IsNullOrWhiteSpace($runStatus)) {
        $result.status = $runStatus
    } else {
        $result.status = "unknown"
    }
    $result.runId = [string]$matchingRun.databaseId
    [pscustomobject]$result
}

function Get-LocalBuildReadback([string]$BuildDirectory) {
    $result = [ordered]@{
        status = "unknown"
        source = "auto-build-artifact"
    }
    if ($script:PlanOnly.IsPresent) {
        $result.source = "plan-only"
        return [pscustomobject]$result
    }
    try {
        $resolvedBuildDir = Resolve-RepoPath $BuildDirectory
    } catch {
        $result.status = "not-configured"
        return [pscustomobject]$result
    }
    if (-not (Test-Path -LiteralPath $resolvedBuildDir -PathType Container)) {
        $result.status = "not-run"
        return [pscustomobject]$result
    }
    $exePath = Join-Path $resolvedBuildDir "QtNetworkChat.exe"
    if (Test-Path -LiteralPath $exePath -PathType Leaf) {
        $result.status = "artifact-present"
    } else {
        $result.status = "missing-executable"
    }
    [pscustomobject]$result
}

function Get-LocalVerificationStatusReadback([string]$PathValue) {
    $result = [ordered]@{
        configured = $false
        readable = $false
        buildStatus = ""
        ctestStatus = ""
        ctestCount = 0
        source = "local-verification-status"
    }
    if ([string]::IsNullOrWhiteSpace($PathValue) -or $script:PlanOnly.IsPresent) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    try {
        $resolved = Resolve-RepoPath $PathValue
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return [pscustomobject]$result
        }
        $status = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ((Get-JsonValue $status "format" "") -ne "qtnetworkchat-local-verification-status-v1") {
            return [pscustomobject]$result
        }
        $buildNode = Get-JsonValue $status "build" $null
        $ctestNode = Get-JsonValue $status "ctest" $null
        $result.readable = $true
        $result.buildStatus = [string](Get-JsonValue $buildNode "status" "")
        $result.ctestStatus = [string](Get-JsonValue $ctestNode "status" "")
        $result.ctestCount = [int](Get-JsonValue $ctestNode "count" 0)
    } catch {
    }
    [pscustomobject]$result
}

function Get-CTestReadback([string]$BuildDirectory, [string]$ExplicitLogPath) {
    $result = [ordered]@{
        status = "unknown"
        count = 0
        source = "auto-ctest-last-log"
    }
    if ($script:PlanOnly.IsPresent) {
        $result.source = "plan-only"
        return [pscustomobject]$result
    }
    try {
        $logPath = if ([string]::IsNullOrWhiteSpace($ExplicitLogPath)) {
            Join-Path (Resolve-RepoPath $BuildDirectory) "Testing\Temporary\LastTest.log"
        } else {
            Resolve-RepoPath $ExplicitLogPath
        }
    } catch {
        $result.status = "not-configured"
        return [pscustomobject]$result
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        $result.status = "not-run"
        return [pscustomobject]$result
    }
    try {
        $raw = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    } catch {
        $result.status = "unreadable"
        return [pscustomobject]$result
    }
    $matches = [regex]::Matches($raw, '(?m)^\s*(\d+)\/(\d+)\s+Testing:')
    $total = 0
    foreach ($match in $matches) {
        $candidate = [int]$match.Groups[2].Value
        if ($candidate -gt $total) {
            $total = $candidate
        }
    }
    $result.count = $total
    $hasFailure = $raw -match '(?m)^\s*Test Failed\.' -or $raw -match '\*\*\*Failed' -or $raw -match 'Errors while running CTest'
    $hasEnd = $raw -match '(?m)^\s*End testing:'
    if ($total -gt 0 -and -not $hasFailure -and $hasEnd) {
        $result.status = "passed"
    } elseif ($total -gt 0 -and $hasFailure) {
        $result.status = "failed"
    } else {
        $result.status = "incomplete"
    }
    [pscustomobject]$result
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

function Get-JsonValue([object]$ObjectValue, [string]$Name, [object]$DefaultValue = $null) {
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    $property = $ObjectValue.PSObject.Properties[$Name]
    if ($null -ne $property) {
        return $property.Value
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

function Convert-StatusBoolean([object]$Value, [bool]$DefaultValue) {
    if ($null -eq $Value) {
        return $DefaultValue
    }
    if ($Value -is [bool]) {
        return [bool]$Value
    }
    $text = ([string]$Value).Trim().ToLowerInvariant()
    if ($text -eq "true" -or $text -eq "1" -or $text -eq "yes") {
        return $true
    }
    if ($text -eq "false" -or $text -eq "0" -or $text -eq "no") {
        return $false
    }
    $DefaultValue
}

function Convert-ToUtcDateTimeOffset([object]$Value) {
    $text = Format-StatusValue $Value
    if ($text -eq "unknown") {
        return $null
    }
    try {
        return [System.DateTimeOffset]::Parse(
            $text,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AssumeUniversal -bor [System.Globalization.DateTimeStyles]::AdjustToUniversal)
    } catch {
        return $null
    }
}

function Get-AckReminderReadback([object]$HistoryState, [bool]$Configured, [object]$AckState = $null, [int]$DefaultExpiryHours = 0, [object]$NowUtc = $null) {
    $result = [ordered]@{
        configured = $Configured
        state = "not-configured"
        expiryHours = "unknown"
        ageHours = "unknown"
        remainingHours = "unknown"
        overdueHours = "unknown"
        expiresAt = "unknown"
        action = "none"
    }
    if (-not $Configured) {
        return [pscustomobject]$result
    }
    if ($null -eq $HistoryState -or $HistoryState.state -ne "ok") {
        $result.state = "history-unavailable"
        $result.action = "restore automation task history artifact before release"
        return [pscustomobject]$result
    }
    $history = $HistoryState.value
    $result.state = Format-StatusValue (Get-JsonValue $history "ackReminder" "unknown")
    $result.expiryHours = Format-StatusValue (Get-JsonValue $history "ackExpiryHours" "unknown")
    $result.ageHours = Format-StatusValue (Get-JsonValue $history "ackAgeHours" "unknown")
    $result.remainingHours = Format-StatusValue (Get-JsonValue $history "ackHoursRemaining" "unknown")
    $result.overdueHours = Format-StatusValue (Get-JsonValue $history "ackHoursOverdue" "unknown")
    $result.expiresAt = Format-StatusValue (Get-JsonValue $history "ackExpiresAt" "unknown")
    $failedCount = 0
    [void][int]::TryParse((Format-StatusValue (Get-JsonValue $history "failedRunCount" "0")), [ref]$failedCount)
    $historyAckExpired = Convert-StatusBoolean (Get-JsonValue $history "ackExpired" $null) $false
    if ($failedCount -gt 0 -and -not $historyAckExpired -and $null -ne $AckState -and $AckState.state -eq "ok" -and
        (Convert-StatusBoolean (Get-JsonValue $AckState.value "acknowledged" $null) $false)) {
        $result.state = "acknowledged"
        $ackAt = Convert-ToUtcDateTimeOffset (Get-JsonValue $AckState.value "acknowledgedAt" $null)
        $expiryHours = 0
        [void][int]::TryParse((Format-StatusValue $result.expiryHours), [ref]$expiryHours)
        if ($expiryHours -le 0) {
            $expiryHours = $DefaultExpiryHours
            if ($expiryHours -gt 0) {
                $result.expiryHours = $expiryHours
            }
        }
        if ($null -eq $NowUtc) {
            $NowUtc = [System.DateTimeOffset]::UtcNow
        }
        if ($null -ne $ackAt -and $expiryHours -gt 0) {
            $ageHours = ($NowUtc - $ackAt).TotalHours
            if ($ageHours -lt 0) {
                $ageHours = 0
            }
            $expiresAt = $ackAt.AddHours($expiryHours)
            $remainingHours = ($expiresAt - $NowUtc).TotalHours
            $result.ageHours = [math]::Round($ageHours, 2)
            $result.expiresAt = $expiresAt.UtcDateTime.ToString("o")
            if ($remainingHours -lt 0) {
                $result.state = "renew-required"
                $result.remainingHours = 0
                $result.overdueHours = [math]::Round(-$remainingHours, 2)
            } else {
                $result.remainingHours = [math]::Round($remainingHours, 2)
                $result.overdueHours = 0
                if ($remainingHours -le 24) {
                    $result.state = "renew-soon"
                }
            }
        }
    }
    if ($result.state -eq "acknowledge-required") {
        $result.action = "acknowledge failed automation task before release"
    } elseif ($result.state -eq "renew-required") {
        $result.action = "renew expired automation task acknowledgement before release"
    } elseif ($result.state -eq "renew-soon") {
        $result.action = "renew automation task acknowledgement before it expires"
    } elseif ($result.state -eq "history-unavailable") {
        $result.action = "restore automation task history artifact before release"
    } else {
        $result.action = "none"
    }
    [pscustomobject]$result
}

function Get-AckReminderPriority([string]$State) {
    switch (Format-StatusValue $State) {
        "renew-required" { return 60 }
        "acknowledge-required" { return 50 }
        "history-unavailable" { return 40 }
        "renew-soon" { return 30 }
        "acknowledged" { return 20 }
        "not-required" { return 10 }
        default { return 0 }
    }
}

function Get-AutomationTaskAckReminderAggregateReadback([object[]]$TaskPairs, [object]$FallbackReminder, [bool]$Configured) {
    $usableReminders = @($TaskPairs | Where-Object { $null -ne $_ -and $null -ne $_.reminder -and $_.reminder.configured })
    if ($usableReminders.Count -le 0) {
        return $FallbackReminder
    }

    $selected = $null
    $selectedPriority = -1
    foreach ($pair in $usableReminders) {
        $priority = Get-AckReminderPriority $pair.reminder.state
        if ($null -eq $selected -or $priority -gt $selectedPriority) {
            $selected = $pair.reminder
            $selectedPriority = $priority
        }
    }
    if ($null -ne $selected) {
        return $selected
    }
    [pscustomobject]@{
        configured = $Configured
        state = "not-configured"
        expiryHours = "unknown"
        ageHours = "unknown"
        remainingHours = "unknown"
        overdueHours = "unknown"
        expiresAt = "unknown"
        action = "none"
    }
}

function Has-ConfigurationHint([string[]]$Values) {
    foreach ($value in $Values) {
        if (-not [string]::IsNullOrWhiteSpace([string]$value)) {
            return $true
        }
    }
    $false
}

function Normalize-PathList([string[]]$Values) {
    $normalized = New-Object System.Collections.Generic.List[string]
    foreach ($value in $Values) {
        foreach ($part in ([string]$value -split ",")) {
            $trimmed = $part.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                $normalized.Add($trimmed)
            }
        }
    }
    @($normalized)
}

function Get-ArtifactState(
    [string]$PathValue,
    [switch]$ExpectJson
) {
    $result = [ordered]@{
        configured = $false
        resolvedPath = ""
        exists = $false
        readable = $false
        nonEmpty = $false
        jsonValid = $false
        state = "not-configured"
        value = $null
    }
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    try {
        $resolvedPath = Resolve-RepoPath $PathValue
        $result.resolvedPath = $resolvedPath
    } catch {
        $result.state = "invalid-path"
        return [pscustomobject]$result
    }
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        $result.state = "missing"
        return [pscustomobject]$result
    }
    $result.exists = $true
    try {
        $raw = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8 -ErrorAction Stop
        $result.readable = $true
    } catch {
        $result.state = "unreadable"
        return [pscustomobject]$result
    }
    if ([string]::IsNullOrWhiteSpace($raw)) {
        $result.state = "empty"
        return [pscustomobject]$result
    }
    $result.nonEmpty = $true
    if ($ExpectJson) {
        try {
            $result.value = $raw | ConvertFrom-Json -ErrorAction Stop
            $result.jsonValid = $true
            $result.state = "ok"
        } catch {
            $result.state = "invalid-json"
        }
        return [pscustomobject]$result
    }
    $result.value = $raw.Trim()
    $result.state = "ok"
    [pscustomobject]$result
}

function Resolve-PreviewValue([object]$PreviewState, [string]$PropertyName) {
    if ($null -eq $PreviewState -or $PreviewState.state -ne "ok") {
        return ""
    }
    $value = Get-JsonValue $PreviewState.value $PropertyName ""
    if ([string]::IsNullOrWhiteSpace([string]$value)) {
        return ""
    }
    [string]$value
}

function Resolve-PreviewValueAny([object]$PreviewState, [string[]]$PropertyNames) {
    foreach ($propertyName in $PropertyNames) {
        $value = Resolve-PreviewValue $PreviewState $propertyName
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }
    ""
}

function Get-PreviewArtifactConfiguration(
    [object]$PreviewState,
    [string[]]$PreferredPropertyNames,
    [string]$ArtifactLabel
) {
    if ($null -eq $PreviewState -or -not $PreviewState.configured) {
        return [pscustomobject]@{
            configured = $false
            path = ""
            source = "preview-not-configured"
        }
    }
    if ($PreviewState.state -ne "ok") {
        return [pscustomobject]@{
            configured = $false
            path = ""
            source = "preview-" + $PreviewState.state
        }
    }
    $artifactsObject = Get-JsonValue $PreviewState.value "artifacts" $null
    $artifactObject = Get-JsonValue $artifactsObject $ArtifactLabel $null
    $artifactPath = Get-JsonValue $artifactObject "path" ""
    if (-not [string]::IsNullOrWhiteSpace([string]$artifactPath)) {
        return [pscustomobject]@{
            configured = $true
            path = [string]$artifactPath
            source = "artifacts." + $ArtifactLabel + ".path"
        }
    }
    $roleHints = Get-JsonValue $PreviewState.value "artifactRoles" $null
    foreach ($propertyName in $PreferredPropertyNames) {
        $value = Resolve-PreviewValue $PreviewState $propertyName
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return [pscustomobject]@{
                configured = $true
                path = $value
                source = $propertyName
            }
        }
    }
    if ($null -ne $roleHints) {
        $rolePropertyName = Get-JsonValue $roleHints $ArtifactLabel ""
        if (-not [string]::IsNullOrWhiteSpace($rolePropertyName)) {
            return [pscustomobject]@{
                configured = $false
                path = ""
                source = "artifactRoles." + $ArtifactLabel
            }
        }
    }
    [pscustomobject]@{
        configured = $false
        path = ""
        source = "preview-missing-property"
    }
}

function Read-LastRunSummary([object]$ArtifactState) {
    if ($null -eq $ArtifactState -or $ArtifactState.state -ne "ok") {
        return $null
    }
    $text = [string]$ArtifactState.value
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

function Get-ArtifactIssueText(
    [string]$ArtifactLabel,
    [object]$ArtifactState,
    [object]$PreviewState,
    [object]$PreviewConfiguration
) {
    if ($null -ne $ArtifactState -and $ArtifactState.state -eq "ok") {
        return $ArtifactLabel + '=ok'
    }
    if ($null -ne $ArtifactState -and $ArtifactState.configured -and $ArtifactState.state -ne "not-configured") {
        return ('{0}={1}' -f $ArtifactLabel, $ArtifactState.state)
    }
    if ($null -eq $PreviewConfiguration -or -not $PreviewConfiguration.configured) {
        if ($null -eq $PreviewState -or -not $PreviewState.configured) {
            return $ArtifactLabel + '=not-configured'
        }
        if ($PreviewState.state -ne "ok") {
            return ('{0}=preview-{1}' -f $ArtifactLabel, $PreviewState.state)
        }
        if ($null -ne $PreviewConfiguration -and $PreviewConfiguration.source -eq "preview-missing-property") {
            return ('{0}=preview-missing-property' -f $ArtifactLabel)
        }
        if ($null -ne $PreviewConfiguration -and $PreviewConfiguration.source -like "artifactRoles.*") {
            return ('{0}=preview-role-without-path' -f $ArtifactLabel)
        }
        return ('{0}=not-configured' -f $ArtifactLabel)
    }
    $ArtifactLabel + '=not-configured'
}

function Get-S3RealBackendReadinessReadback([object]$ArtifactState) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        ok = "unknown"
        status = "not-configured"
        readiness = "not-configured"
        releaseGate = "not-configured"
        operatorAction = "none"
        configuredFlag = "unknown"
        explicitEnabled = "unknown"
        defaultCTestMode = "unknown"
        realBackendDefaultCI = "unknown"
        defaultCIReleaseGate = "unknown"
        s3LineCount = 0
        successCount = 0
        fixedFailureReasonCount = 0
        sensitiveHitCount = 0
    }
    if ($null -eq $ArtifactState -or -not $ArtifactState.configured) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    $result.state = $ArtifactState.state
    if ($ArtifactState.state -ne "ok") {
        $result.status = $ArtifactState.state
        $result.releaseGate = "s3-real-backend-readiness-" + $ArtifactState.state
        return [pscustomobject]$result
    }
    $readiness = $ArtifactState.value
    if ((Get-JsonValue $readiness "format" "") -ne "qtnetworkchat-s3-real-backend-readiness-v1") {
        $result.state = "invalid-format"
        $result.status = "invalid-format"
        $result.releaseGate = "s3-real-backend-readiness-invalid-format"
        return [pscustomobject]$result
    }
    $summary = Get-JsonValue $readiness "summary" $null
    $auditSummary = Get-JsonValue $readiness "auditSummary" $null
    $evidence = Get-JsonValue $readiness "evidence" $null
    $result.ok = Format-StatusValue (Get-JsonValue $readiness "ok" $null)
    $result.status = Format-StatusValue (Get-JsonValue $readiness "status" "unknown")
    $result.readiness = Format-StatusValue (Get-JsonValue $summary "readiness" "unknown")
    $result.releaseGate = Format-StatusValue (Get-JsonValue $auditSummary "releaseGate" "unknown")
    $result.operatorAction = Format-StatusValue (Get-JsonValue $summary "operatorAction" "unknown")
    $result.configuredFlag = Format-StatusValue (Get-JsonValue $readiness "configured" $null)
    $result.explicitEnabled = Format-StatusValue (Get-JsonValue $readiness "explicitEnabled" $null)
    $result.defaultCTestMode = Format-StatusValue (Get-JsonValue $auditSummary "defaultCTestMode" "unknown")
    $result.realBackendDefaultCI = Format-StatusValue (Get-JsonValue $auditSummary "realBackendDefaultCI" "unknown")
    $result.defaultCIReleaseGate = Format-StatusValue (Get-JsonValue $auditSummary "defaultCIReleaseGate" "unknown")
    $result.s3LineCount = [int](Get-JsonValue $evidence "s3LineCount" 0)
    $result.successCount = [int](Get-JsonValue $evidence "successCount" 0)
    $result.fixedFailureReasonCount = [int](Get-JsonValue $evidence "fixedFailureReasonCount" 0)
    $result.sensitiveHitCount = [int](Get-JsonValue $evidence "sensitiveHitCount" 0)
    [pscustomobject]$result
}

function Get-E2ERolloutObservabilityReadback(
    [object]$JsonState,
    [object]$MarkdownState
) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        status = "not-configured"
        ok = $false
        releaseGate = "unknown"
        readiness = "unknown"
        ciStatus = "unknown"
        ciRunId = "unknown"
        ciSource = "unknown"
        localBuildStatus = "unknown"
        localCTestStatus = "unknown"
        localCTestCount = 0
        jsonArtifact = "not-configured"
        markdownArtifact = "not-configured"
        bundle = "not-configured"
        noSensitiveExportProof = "unknown"
        sensitiveFieldsSuppressed = "unknown"
        filesystemReady = "unknown"
        filesystemGate = "unknown"
        offlineReady = "unknown"
        offlineScope = "unknown"
        offlineGate = "unknown"
        offlineAction = "unknown"
        offlineCapturePolicy = "unknown"
        offlineNoSensitiveExportProof = "unknown"
        artifactSource = "e2e-rollout-observability"
    }

    if ($null -ne $JsonState -and $JsonState.configured) {
        $result.configured = $true
        $result.jsonArtifact = $JsonState.state
    }
    if ($null -ne $MarkdownState -and $MarkdownState.configured) {
        $result.configured = $true
        $result.markdownArtifact = $MarkdownState.state
    }
    if (-not $result.configured) {
        return [pscustomobject]$result
    }

    if ($null -eq $JsonState -or $JsonState.state -ne "ok") {
        $result.state = if ($null -ne $JsonState) { $JsonState.state } else { "missing-json" }
        $result.status = "artifact-unavailable"
        $result.bundle = "missing-json"
        return [pscustomobject]$result
    }

    $evidence = $JsonState.value
    if ((Get-JsonValue $evidence "format" "") -ne "qtnetworkchat-e2e-production-rollout-observability-evidence-v1") {
        $result.state = "invalid-format"
        $result.status = "invalid-format"
        $result.bundle = "invalid-json-format"
        return [pscustomobject]$result
    }

    $summary = Get-JsonValue $evidence "summary" $null
    $auditSummary = Get-JsonValue $evidence "auditSummary" $null
    $rolloutObservability = Get-JsonValue $evidence "productionRolloutObservability" $null
    $proof = Get-JsonValue $evidence "sensitiveExportProof" $null
    $releaseCi = Get-JsonValue $evidence "releaseCi" $null
    $releaseLocal = Get-JsonValue $evidence "releaseLocalVerification" $null

    $result.state = "ok"
    $result.status = Format-StatusValue (Get-JsonValue $evidence "status" "unknown")
    $result.ok = [bool](Get-JsonValue $evidence "ok" $false)
    $result.releaseGate = Format-StatusValue (Get-JsonValue $auditSummary "releaseGate" "unknown")
    $result.readiness = Format-StatusValue (Get-JsonValue $summary "readiness" "unknown")
    $result.noSensitiveExportProof =
        Format-StatusValue (Get-JsonValue $proof "noSensitiveExportProof" "unknown")
    $result.sensitiveFieldsSuppressed =
        Format-StatusValue (Get-JsonValue $proof "sensitiveFieldsSuppressed" "unknown")
    $result.filesystemReady =
        Format-StatusValue (Get-JsonValue $summary "filesystemObjectRecoveryReady" "unknown")
    $result.filesystemGate =
        Format-StatusValue (Get-JsonValue $summary "filesystemObjectRecoveryReleaseGate" "unknown")
    $result.offlineReady =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryReady" "unknown")
    $result.offlineScope =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryScope" "unknown")
    $result.offlineGate =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryReleaseGate" "unknown")
    $result.offlineAction =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryAction" `
            (Get-JsonValue $rolloutObservability "offlineObjectRecoveryAction" "unknown"))
    $result.offlineCapturePolicy =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryCapturePolicy" `
            (Get-JsonValue $rolloutObservability "offlineObjectRecoveryCapturePolicy" "unknown"))
    $result.offlineNoSensitiveExportProof =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryNoSensitiveExportProof" `
            (Get-JsonValue $rolloutObservability "offlineObjectRecoveryNoSensitiveExportProof" "unknown"))
    $result.ciStatus = Format-StatusValue (Get-JsonValue $releaseCi "status" "unknown")
    $result.ciRunId = Format-StatusValue (Get-JsonValue $releaseCi "runId" "unknown")
    $result.ciSource = Format-StatusValue (Get-JsonValue $releaseCi "source" "unknown")
    $result.localBuildStatus =
        Format-StatusValue (Get-JsonValue $releaseLocal "buildStatus" "unknown")
    $result.localCTestStatus =
        Format-StatusValue (Get-JsonValue $releaseLocal "ctestStatus" "unknown")
    $result.localCTestCount = [int](Get-JsonValue $releaseLocal "ctestCount" 0)
    $result.bundle = if ($null -ne $MarkdownState -and $MarkdownState.state -eq "ok") {
        "json+markdown"
    } else {
        "json-only"
    }
    [pscustomobject]$result
}

function Get-E2EReleaseEvidenceReadback([object]$ManifestState, [string]$CurrentHeadSha) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        ok = "unknown"
        releaseReady = "unknown"
        releaseGate = "unknown"
        inputCount = 0
        ciStatus = "unknown"
        ciVisibility = "unknown"
        ciSource = "unknown"
        ciHeadSha = "unknown"
        ciCurrentHeadObserved = "unknown"
        ciHeadMatchesReleaseHead = "unknown"
        ciExternalBlocker = "unknown"
        ciReleaseGate = "unknown"
        ciLatestObservedHead = "unknown"
        targetReleaseHead = "unknown"
        localBuildStatus = "unknown"
        localCTestStatus = "unknown"
        localCTestCount = 0
        noSensitiveExportProof = "unknown"
        packageArtifact = "not-configured"
        packageSha256 = "unknown"
        manifestPackagedAs = "unknown"
        manifestEmbedded = "unknown"
        promotionReady = "unknown"
        promotionPromoted = "unknown"
        promotionGate = "unknown"
        promotionBlockers = "unknown"
        promotionOperatorAction = "unknown"
        productionLinkedReady = "unknown"
        productionLinkedGate = "unknown"
        productionLinkedBlockers = "unknown"
        productionLinkedAcceptanceBackend = "unknown"
        productionLinkedRolloutBackend = "unknown"
        productionLinkedReleaseRunBackend = "unknown"
        productionLinkedOperationCountsReady = "unknown"
        productionLinkedNoSensitiveReady = "unknown"
        targetMatchesCurrentHead = "unknown"
        currentHead = "unknown"
        staleReleaseArtifact = "unknown"
        probeFixture = "unknown"
        releaseEligible = "unknown"
        releaseEligibilityGate = "unknown"
    }
    if ($null -ne $ManifestState -and $ManifestState.configured) {
        $result.configured = $true
        $result.packageArtifact = $ManifestState.state
    }
    if (-not $result.configured) {
        return [pscustomobject]$result
    }
    if ($null -eq $ManifestState -or $ManifestState.state -ne "ok") {
        $result.state = if ($null -ne $ManifestState) { $ManifestState.state } else { "missing-manifest" }
        $result.releaseGate = "e2e-release-evidence-unavailable"
        return [pscustomobject]$result
    }

    $manifest = $ManifestState.value
    if ((Get-JsonValue $manifest "format" "") -ne "qtnetworkchat-e2e-release-evidence-package-v1") {
        $result.state = "invalid-format"
        $result.releaseGate = "e2e-release-evidence-invalid-format"
        return [pscustomobject]$result
    }

    $ci = Get-JsonValue $manifest "ci" $null
    $local = Get-JsonValue $manifest "localVerification" $null
    $proof = Get-JsonValue $manifest "sensitiveExportProof" $null
    $promotion = Get-JsonValue $manifest "promotion" $null
    $productionLinked = Get-JsonValue $manifest "productionLinkedEvidence" $null
    $result.state = "ok"
    $result.ok = Format-StatusValue (Get-JsonValue $manifest "ok" "unknown")
    $result.releaseReady = Format-StatusValue (Get-JsonValue $manifest "releaseReady" "unknown")
    $result.releaseGate = Format-StatusValue (Get-JsonValue $manifest "releaseGate" "unknown")
    $result.inputCount = [int](Get-JsonValue $manifest "inputCount" 0)
    $result.packageSha256 = Format-StatusValue (Get-JsonValue $manifest "packageSha256" "unknown")
    $result.targetReleaseHead = Format-StatusValue (Get-JsonValue $manifest "targetReleaseHead" "unknown")
    $result.currentHead = Format-StatusValue $(if ([string]::IsNullOrWhiteSpace($CurrentHeadSha)) { "unknown" } else { $CurrentHeadSha })
    $releaseHeadConfigured = [bool](Get-JsonValue $manifest "releaseHeadConfigured" $false)
    $targetMatchesCurrentHead = (-not $releaseHeadConfigured) -or (Test-HeadMatch $CurrentHeadSha $result.targetReleaseHead)
    $result.targetMatchesCurrentHead = Format-StatusValue $targetMatchesCurrentHead
    $result.staleReleaseArtifact = Format-StatusValue (-not $targetMatchesCurrentHead)
    $result.manifestPackagedAs = Format-StatusValue (Get-JsonValue $manifest "manifestPackagedAs" "unknown")
    $result.manifestEmbedded = Format-StatusValue (Get-JsonValue $manifest "manifestEmbedded" "unknown")
    $result.ciStatus = Format-StatusValue (Get-JsonValue $ci "status" "unknown")
    $result.ciVisibility = Format-StatusValue (Get-JsonValue $ci "visibility" "unknown")
    $result.ciSource = Format-StatusValue (Get-JsonValue $ci "source" "unknown")
    $result.ciHeadSha = Format-StatusValue (Get-JsonValue $ci "headSha" "unknown")
    $result.ciCurrentHeadObserved =
        Format-StatusValue (Get-JsonValue $ci "currentHeadObserved" "unknown")
    $result.ciHeadMatchesReleaseHead =
        Format-StatusValue (Get-JsonValue $ci "headMatchesReleaseHead" "unknown")
    $result.ciExternalBlocker = Format-StatusValue (Get-JsonValue $ci "externalBlocker" "unknown")
    $result.ciReleaseGate = Format-StatusValue (Get-JsonValue $ci "releaseGate" "unknown")
    $result.ciLatestObservedHead =
        Format-StatusValue (Get-JsonValue $ci "latestObservedHead" "unknown")
    $result.localBuildStatus = Format-StatusValue (Get-JsonValue $local "buildStatus" "unknown")
    $result.localCTestStatus = Format-StatusValue (Get-JsonValue $local "ctestStatus" "unknown")
    $result.localCTestCount = [int](Get-JsonValue $local "ctestCount" 0)
    $result.noSensitiveExportProof =
        Format-StatusValue (Get-JsonValue $proof "noSensitiveExportProof" "unknown")
    $result.promotionReady =
        Format-StatusValue (Get-JsonValue $promotion "promotionReady" "unknown")
    $result.promotionPromoted =
        Format-StatusValue (Get-JsonValue $promotion "promoted" "unknown")
    $result.promotionGate =
        Format-StatusValue (Get-JsonValue $promotion "releaseGate" "unknown")
    $result.promotionBlockers =
        Format-StatusValue (@(Get-JsonValue $promotion "blockers" @()) -join ",")
    $result.promotionOperatorAction =
        Format-StatusValue (Get-JsonValue $promotion "operatorAction" "unknown")
    $result.productionLinkedReady =
        Format-StatusValue (Get-JsonValue $productionLinked "ready" "unknown")
    $result.productionLinkedGate =
        Format-StatusValue (Get-JsonValue $productionLinked "releaseGate" "unknown")
    $result.productionLinkedBlockers =
        Format-StatusValue (@(Get-JsonValue $productionLinked "blockers" @()) -join ",")
    $result.productionLinkedAcceptanceBackend =
        Format-StatusValue (Get-JsonValue $productionLinked "acceptanceBackendId" "unknown")
    $result.productionLinkedRolloutBackend =
        Format-StatusValue (Get-JsonValue $productionLinked "rolloutBackendId" "unknown")
    $result.productionLinkedReleaseRunBackend =
        Format-StatusValue (Get-JsonValue $productionLinked "releaseRunSelectedBackendId" "unknown")
    $result.productionLinkedOperationCountsReady =
        Format-StatusValue (Get-JsonValue $productionLinked "operationCountsReady" "unknown")
    $productionLinkedNoSensitiveReady = $false
    if ($null -ne $productionLinked) {
        $productionLinkedNoSensitiveReady =
            [bool](Get-JsonValue $productionLinked "rolloutNoSensitiveExportProof" $false) `
            -and [bool](Get-JsonValue $productionLinked "artifactNoSensitiveExportProof" $false) `
            -and -not [bool](Get-JsonValue $productionLinked "sensitiveMaterialExported" $false)
    }
    $result.productionLinkedNoSensitiveReady = Format-StatusValue $productionLinkedNoSensitiveReady
    $isProbeFixture = $result.ciSource -eq "probe-fixture" -or $result.targetReleaseHead -eq "production-probe-head"
    $result.probeFixture = Format-StatusValue $isProbeFixture
    $releaseEligible = -not $isProbeFixture -and $targetMatchesCurrentHead
    $result.releaseEligible = Format-StatusValue $releaseEligible
    $result.releaseEligibilityGate = if ($isProbeFixture) {
        "not-release-eligible-probe-fixture"
    } elseif (-not $targetMatchesCurrentHead) {
        "not-release-eligible-stale-head"
    } else {
        "release-eligible-current-head"
    }
    if ($script:GitHubWindowsBuildPolicyResolved -eq "disabled") {
        $result.ciStatus = "disabled-by-policy"
        $result.ciVisibility = "not-required"
        $result.ciSource = "automation-policy"
        $result.ciCurrentHeadObserved = "not-required"
        $result.ciExternalBlocker = "waived-by-policy"
        $result.ciReleaseGate = "not-required"
        $result.ciLatestObservedHead = "not-required"
        $policyBlockers = @($result.promotionBlockers -split "," | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) `
                    -and $_ -ne "unknown" `
                    -and $_ -ne "ci-status-external-visibility-stale" `
                    -and $_ -ne "ci-current-head-not-observed"
            })
        $result.promotionBlockers = Format-StatusValue ($policyBlockers -join ",")
        $policyReady = (Format-StatusValue $result.productionLinkedReady) -eq "true" `
            -and (Format-StatusValue $result.releaseEligible) -eq "true" `
            -and (Format-StatusValue $result.probeFixture) -eq "false" `
            -and (Format-StatusValue $result.targetMatchesCurrentHead) -eq "true" `
            -and (Format-StatusValue $result.staleReleaseArtifact) -eq "false"
        if ($policyReady) {
            $result.releaseReady = "true"
            $result.releaseGate = "ready-local-verification-only"
            $result.promotionReady = "true"
            $result.promotionGate = "ready-local-verification-only"
            $result.promotionOperatorAction =
                "GitHub Windows Build is disabled by repo policy; use local build/CTest and linked production evidence for release review."
        }
    }
    if (-not $targetMatchesCurrentHead) {
        $result.releaseReady = "false"
        $result.promotionReady = "false"
        $result.promotionPromoted = "false"
        $result.promotionGate = "blocked-e2e-release-artifact-promotion"
        $blockers = @($result.promotionBlockers -split "," | Where-Object {
                -not [string]::IsNullOrWhiteSpace($_) -and $_ -ne "unknown"
            })
        if ($script:GitHubWindowsBuildPolicyResolved -eq "disabled") {
            $result.ciCurrentHeadObserved = "not-required"
            $result.ciExternalBlocker = "waived-by-policy"
            $result.ciReleaseGate = "not-required"
            if ((Format-StatusValue $result.releaseGate) -eq "ready-local-verification-only") {
                $result.releaseGate = "blocked-local-release-evidence-refresh-needed"
                if ($blockers -notcontains "release-artifact-refresh-needed") {
                    $blockers += "release-artifact-refresh-needed"
                }
            }
            $result.promotionBlockers = Format-StatusValue ($blockers -join ",")
            $result.promotionOperatorAction =
                "Refresh the E2E release evidence for the current HEAD after resolving any remaining local verification or production-linked blockers."
        } else {
            $result.releaseGate = "blocked-release-artifact-stale-head"
            $result.ciCurrentHeadObserved = "false"
            $result.ciExternalBlocker = "release-artifact-target-head-mismatch"
            $result.ciReleaseGate = "blocked-release-artifact-stale-head"
            if ($blockers -notcontains "release-artifact-stale-head") {
                $blockers += "release-artifact-stale-head"
            }
            if ($blockers -notcontains "ci-current-head-not-observed") {
                $blockers += "ci-current-head-not-observed"
            }
            $result.promotionBlockers = Format-StatusValue ($blockers -join ",")
            $result.promotionOperatorAction =
                "Regenerate E2E release evidence for the current HEAD before promotion."
        }
    }
    [pscustomobject]$result
}

function New-PreviewRecord([string]$Label, [string]$PathValue) {
    $state = Get-ArtifactState -PathValue $PathValue -ExpectJson
    $taskKind = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "taskKind" "unknown") } else { "unknown" }
    $taskName = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "taskName" "unknown") } else { "unknown" }
    $taskDisplayName = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "taskDisplayName" "unknown") } else { "unknown" }
    $previewFormat = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "format" "unknown") } else { "unknown" }
    $taskSummary = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "taskSummary" "unknown") } else { "unknown" }
    $readOnly = if ($state.state -eq "ok") { Get-JsonValue $state.value "readOnly" $null } else { $null }
    $register = if ($state.state -eq "ok") { Get-JsonValue $state.value "register" $null } else { $null }
    $schedule = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "schedule" "unknown") } else { "unknown" }
    $at = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "at" "unknown") } else { "unknown" }
    $everyHours = if ($state.state -eq "ok") { Format-StatusValue (Get-JsonValue $state.value "everyHours" "unknown") } else { "unknown" }
    $scheduleSummary = if ($schedule -eq "Hourly" -and $everyHours -ne "unknown") {
        "$schedule/$everyHours h@$at"
    } elseif ($schedule -ne "unknown" -and $at -ne "unknown") {
        "$schedule@$at"
    } else {
        $schedule
    }
    [pscustomobject]@{
        label = $Label
        path = $PathValue
        state = $state
        taskKind = $taskKind
        taskName = $taskName
        taskDisplayName = $taskDisplayName
        previewFormat = $previewFormat
        taskSummary = $taskSummary
        readOnly = $readOnly
        register = $register
        scheduleSummary = $scheduleSummary
    }
}

function Resolve-PreviewCollectionValueAny([object[]]$PreviewRecords, [string[]]$PropertyNames) {
    foreach ($previewRecord in $PreviewRecords) {
        $value = Resolve-PreviewValueAny $previewRecord.state $PropertyNames
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }
    ""
}

function Find-PreviewRecordForArtifact([object[]]$PreviewRecords, [string[]]$PropertyNames, [string]$ArtifactLabel) {
    foreach ($previewRecord in $PreviewRecords) {
        $configuration = Get-PreviewArtifactConfiguration $previewRecord.state $PropertyNames $ArtifactLabel
        if ($configuration.configured -or $configuration.source -ne "preview-missing-property") {
            return [pscustomobject]@{
                record = $previewRecord
                configuration = $configuration
            }
        }
    }
    if ($PreviewRecords.Count -gt 0) {
        return [pscustomobject]@{
            record = $PreviewRecords[0]
            configuration = Get-PreviewArtifactConfiguration $PreviewRecords[0].state $PropertyNames $ArtifactLabel
        }
    }
    [pscustomobject]@{
        record = $null
        configuration = [pscustomobject]@{
            configured = $false
            path = ""
            source = "preview-not-configured"
        }
    }
}

function Get-GenericTaskReadback([object]$PreviewRecord) {
    $statusConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("statusArtifactPath", "statusPath", "dashboardPath") "status"
    $lastRunConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("lastRunPath", "logPath") "lastRun"
    $historyConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("historyArtifactPath", "historyPath") "history"
    $ackConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("ackArtifactPath", "ackPath") "ack"

    $statusState = Get-ArtifactState -PathValue $statusConfig.path -ExpectJson
    $lastRunState = Get-ArtifactState -PathValue $lastRunConfig.path
    $historyState = Get-ArtifactState -PathValue $historyConfig.path -ExpectJson
    $ackState = Get-ArtifactState -PathValue $ackConfig.path -ExpectJson
    $lastRunSummary = Read-LastRunSummary $lastRunState

    $statusSummary = "unavailable"
    $readinessSummary = "unknown"
    $releaseGateSummary = "unknown"
    $operatorActionSummary = "unknown"
    $auditFocusSummary = "unknown"
    $releaseDetailsSummary = "unknown"
    $planOnlySummary = $false
    if ($statusState.state -eq "ok") {
        $statusValue = Get-JsonValue $statusState.value "status" ""
        $okValue = Get-JsonValue $statusState.value "ok" $null
        $planOnlyValue = Get-JsonValue $statusState.value "planOnly" $null
        if ($null -ne $planOnlyValue) {
            $planOnlySummary = ((Format-StatusValue $planOnlyValue).ToLowerInvariant() -eq "true")
        }
        $statusSummaryNode = Get-JsonValue $statusState.value "summary" $null
        $reportSummaryNode = Get-JsonValue $statusState.value "reportSummary" $null
        $auditSummaryNode = Get-JsonValue $statusState.value "auditSummary" $null
        $recoverySummaryNode = Get-JsonValue $statusState.value "recoverySummary" $null
        if (-not [string]::IsNullOrWhiteSpace([string]$statusValue)) {
            $statusSummary = [string]$statusValue
            if ($null -ne $okValue) {
                $statusSummary = $statusSummary + "/ok=" + (Format-StatusValue $okValue)
            }
        } elseif ($null -ne $okValue) {
            $statusSummary = "ok=" + (Format-StatusValue $okValue)
        } else {
            $statusSummary = "ok"
        }
        if ($planOnlySummary) {
            $statusSummary = $statusSummary + "/planOnly=true"
        }
        $readinessValue = Get-JsonValue $statusSummaryNode "readiness" ""
        if ([string]::IsNullOrWhiteSpace([string]$readinessValue)) {
            $readinessValue = Get-JsonValue $reportSummaryNode "executionReadiness" ""
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$readinessValue)) {
            $readinessSummary = [string]$readinessValue
        }
        $operatorActionValue = Get-JsonValue $statusSummaryNode "operatorAction" ""
        if ([string]::IsNullOrWhiteSpace([string]$operatorActionValue)) {
            $operatorActionValue = Get-JsonValue $reportSummaryNode "operatorAction" ""
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$operatorActionValue)) {
            $operatorActionSummary = [string]$operatorActionValue
        }
        $releaseGateValue = Get-JsonValue $auditSummaryNode "releaseGate" ""
        if (-not [string]::IsNullOrWhiteSpace([string]$releaseGateValue)) {
            $releaseGateSummary = [string]$releaseGateValue
        }
        $auditFocusValues = @((Get-JsonValue $auditSummaryNode "auditFocus" @()))
        if ($auditFocusValues.Count -gt 0) {
            $auditFocusSummary = ($auditFocusValues | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }) -join ", "
            if ([string]::IsNullOrWhiteSpace($auditFocusSummary)) {
                $auditFocusSummary = "unknown"
            }
        }
        $releaseDetails = New-Object System.Collections.Generic.List[string]
        $bootstrapRequired = Get-JsonValue $auditSummaryNode "bootstrapRequired" $null
        if ($null -ne $bootstrapRequired) {
            $releaseDetails.Add('bootstrapRequired=' + (Format-StatusValue $bootstrapRequired))
        }
        $writeIntent = Get-JsonValue $auditSummaryNode "writeIntent" ""
        if (-not [string]::IsNullOrWhiteSpace([string]$writeIntent)) {
            $releaseDetails.Add('writeIntent=' + [string]$writeIntent)
        }
        $backupRequired = Get-JsonValue $auditSummaryNode "backupRequired" $null
        if ($null -ne $backupRequired) {
            $releaseDetails.Add('backupRequired=' + (Format-StatusValue $backupRequired))
        }
        $rollbackPreviewAvailable = Get-JsonValue $auditSummaryNode "rollbackPreviewAvailable" $null
        if ($null -ne $rollbackPreviewAvailable) {
            $releaseDetails.Add('rollbackPreview=' + (Format-StatusValue $rollbackPreviewAvailable))
        }
        $releaseHint = Get-JsonValue $recoverySummaryNode "releaseHint" ""
        if (-not [string]::IsNullOrWhiteSpace([string]$releaseHint)) {
            $releaseDetails.Add('releaseHint=' + [string]$releaseHint)
        }
        $evidenceBundle = @((Get-JsonValue $auditSummaryNode "evidenceBundle" @()))
        if ($evidenceBundle.Count -gt 0) {
            $evidenceSummary = ($evidenceBundle | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }) -join ", "
            if (-not [string]::IsNullOrWhiteSpace($evidenceSummary)) {
                $releaseDetails.Add('evidence=' + $evidenceSummary)
            }
        }
        if ($releaseDetails.Count -gt 0) {
            $releaseDetailsSummary = $releaseDetails -join "; "
        }
        if ($planOnlySummary -and $releaseDetailsSummary -eq "unknown") {
            $releaseDetailsSummary = "planOnly=true"
        } elseif ($planOnlySummary) {
            $releaseDetailsSummary = $releaseDetailsSummary + "; planOnly=true"
        }
    } else {
        $statusSummary = $statusState.state
        $readinessSummary = $statusState.state
        $releaseGateSummary = $statusState.state
        $operatorActionSummary = $statusState.state
        $auditFocusSummary = $statusState.state
        $releaseDetailsSummary = $statusState.state
    }

    $lastRunExitCode = if ($null -ne $lastRunSummary) { Format-StatusValue $lastRunSummary.exitCode } else { Format-StatusValue $lastRunState.state }
    $historySummary = if ($historyState.state -eq "ok") {
        'runs=' + (Format-StatusValue (Get-JsonValue $historyState.value "runCount" "unknown"))
    } else {
        $historyState.state
    }
    $ackSummary = if ($ackState.state -eq "ok") {
        'ack=' + (Format-StatusValue (Get-JsonValue $ackState.value "acknowledged" $null))
    } else {
        $ackState.state
    }

    [pscustomobject]@{
        previewRecord = $PreviewRecord
        statusState = $statusState
        lastRunState = $lastRunState
        historyState = $historyState
        ackState = $ackState
        statusSummary = $statusSummary
        readinessSummary = $readinessSummary
        releaseGateSummary = $releaseGateSummary
        operatorActionSummary = $operatorActionSummary
        auditFocusSummary = $auditFocusSummary
        releaseDetailsSummary = $releaseDetailsSummary
        planOnlySummary = $planOnlySummary
        lastRunExitCode = $lastRunExitCode
        historySummary = $historySummary
        ackSummary = $ackSummary
    }
}

function Test-IsPreviewTaskEvidence([object]$Readback) {
    if ($null -eq $Readback) {
        return $false
    }
    $statusSummary = (Format-StatusValue $Readback.statusSummary).ToLowerInvariant()
    $readinessSummary = (Format-StatusValue $Readback.readinessSummary).ToLowerInvariant()
    $releaseGateSummary = (Format-StatusValue $Readback.releaseGateSummary).ToLowerInvariant()
    if ($statusSummary -eq "configured" -or $statusSummary.StartsWith("configured/")) {
        return $true
    }
    if ($readinessSummary -eq "preview-registered") {
        return $true
    }
    if ($releaseGateSummary -match '(^|-)preview(-|$)' -or $releaseGateSummary -match 'preview-registered') {
        return $true
    }
    if ($releaseGateSummary -eq "await-live-health-check" -or $Readback.planOnlySummary) {
        return $true
    }
    $false
}

function Get-AutomationTaskEvidenceGateReadback([object[]]$TaskReadbacks) {
    $usableReadbacks = @($TaskReadbacks | Where-Object { $null -ne $_ })
    $result = [ordered]@{
        configured = $usableReadbacks.Count -gt 0
        state = "not-configured"
        taskCount = $usableReadbacks.Count
        previewEvidenceCount = 0
        liveEvidenceCount = 0
        missingEvidenceCount = 0
        releaseGate = "not-configured"
        action = "none"
        details = @()
    }
    if ($usableReadbacks.Count -le 0) {
        return [pscustomobject]$result
    }

    $details = New-Object System.Collections.Generic.List[object]
    foreach ($readback in $usableReadbacks) {
        $preview = $readback.previewRecord
        if (Test-IsPreviewTaskEvidence $readback) {
            $result.previewEvidenceCount++
            $details.Add([pscustomobject]@{
                    taskKind = Format-StatusValue $preview.taskKind
                    taskName = Format-StatusValue $preview.taskName
                    state = "preview-evidence"
                    status = Format-StatusValue $readback.statusSummary
                    readiness = Format-StatusValue $readback.readinessSummary
                    releaseGate = Format-StatusValue $readback.releaseGateSummary
                })
            continue
        }

        $statusState = Format-StatusValue $readback.statusState.state
        if ($statusState -eq "ok") {
            $result.liveEvidenceCount++
            $state = "live-evidence"
        } else {
            $result.missingEvidenceCount++
            $state = "missing-evidence"
        }
        $details.Add([pscustomobject]@{
                taskKind = Format-StatusValue $preview.taskKind
                taskName = Format-StatusValue $preview.taskName
                state = $state
                status = Format-StatusValue $readback.statusSummary
                readiness = Format-StatusValue $readback.readinessSummary
                releaseGate = Format-StatusValue $readback.releaseGateSummary
            })
    }

    $result.details = [object[]]$details.ToArray()
    if ($result.previewEvidenceCount -gt 0) {
        $result.state = "preview-evidence"
        $result.releaseGate = "blocked-preview-task-evidence"
        $result.action = "run registered automation tasks to refresh live status evidence before release"
    } else {
        $result.state = "passing"
        $result.releaseGate = "passing"
        $result.action = "none"
    }
    [pscustomobject]$result
}

function Get-AutomationTaskAckGateReadback([object]$HistoryState, [object]$AckState, [bool]$Configured) {
    $result = [ordered]@{
        configured = $Configured
        state = "not-configured"
        failedRunCount = "unknown"
        acknowledged = "unknown"
        ackExpired = "unknown"
        releaseGate = "not-configured"
        action = "none"
    }
    if (-not $Configured) {
        return [pscustomobject]$result
    }
    if ($null -eq $HistoryState -or $HistoryState.state -ne "ok") {
        $result.state = "history-unavailable"
        $result.releaseGate = "automation-task-history-unavailable"
        $result.action = "restore automation task history artifact before release"
        return [pscustomobject]$result
    }

    $failedRaw = Get-JsonValue $HistoryState.value "failedRunCount" "unknown"
    $failedText = Format-StatusValue $failedRaw
    $failedCount = 0
    if (-not [int]::TryParse($failedText, [ref]$failedCount)) {
        $result.state = "history-unparseable"
        $result.failedRunCount = $failedText
        $result.releaseGate = "automation-task-history-unparseable"
        $result.action = "regenerate automation task history with failedRunCount before release"
        return [pscustomobject]$result
    }

    $ackExpiredRaw = Get-JsonValue $HistoryState.value "ackExpired" $null
    $ackExpired = Convert-StatusBoolean $ackExpiredRaw $false
    $acknowledgedRaw = Get-JsonValue $HistoryState.value "acknowledged" $null
    if ($null -ne $AckState -and $AckState.state -eq "ok") {
        $ackStateAcknowledgedRaw = Get-JsonValue $AckState.value "acknowledged" $null
        if ($null -ne $ackStateAcknowledgedRaw -and -not $ackExpired) {
            $acknowledgedRaw = $ackStateAcknowledgedRaw
        } elseif ($null -eq $acknowledgedRaw) {
            $acknowledgedRaw = $ackStateAcknowledgedRaw
        }
    }

    $acknowledged = Convert-StatusBoolean $acknowledgedRaw $false
    $result.failedRunCount = $failedCount
    $result.acknowledged = $acknowledged.ToString().ToLowerInvariant()
    $result.ackExpired = $ackExpired.ToString().ToLowerInvariant()

    if ($failedCount -le 0) {
        $result.state = "passing"
        $result.releaseGate = "passing"
        $result.action = "none"
    } elseif ($ackExpired) {
        $result.state = "failed-ack-expired"
        $result.releaseGate = "blocked-ack-expired"
        $result.action = "renew task acknowledgement before release"
    } elseif (-not $acknowledged) {
        $result.state = "failed-unacknowledged"
        $result.releaseGate = "blocked-unacknowledged-failure"
        $result.action = "acknowledge failed automation task before release"
    } else {
        $result.state = "failed-acknowledged"
        $result.releaseGate = "acknowledged-failure-review-gated"
        $result.action = "continue remediation; keep release review gate until failures clear"
    }
    [pscustomobject]$result
}

function Get-AutomationTaskHistoryAckPairReadback([object]$PreviewRecord) {
    $historyConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("historyArtifactPath", "historyPath") "history"
    $ackConfig = Get-PreviewArtifactConfiguration $PreviewRecord.state @("ackArtifactPath", "ackPath") "ack"
    $historyState = Get-ArtifactState -PathValue $historyConfig.path -ExpectJson
    $ackState = Get-ArtifactState -PathValue $ackConfig.path -ExpectJson
    $configured = -not [string]::IsNullOrWhiteSpace($historyConfig.path) `
        -or -not [string]::IsNullOrWhiteSpace($ackConfig.path)
    $gate = Get-AutomationTaskAckGateReadback $historyState $ackState $configured
    $reminder = Get-AckReminderReadback $historyState $configured $ackState $script:TaskAckExpiryHours $script:AutomationStatusNowUtc
    [pscustomobject]@{
        taskKind = Format-StatusValue $PreviewRecord.taskKind
        taskName = Format-StatusValue $PreviewRecord.taskName
        historyPath = $historyConfig.path
        ackPath = $ackConfig.path
        historyState = $historyState
        ackState = $ackState
        gate = $gate
        reminder = $reminder
    }
}

function Get-AutomationTaskAckGateAggregateReadback([object[]]$TaskPairs, [object]$FallbackGate, [bool]$Configured) {
    $result = [ordered]@{
        configured = $Configured
        state = "not-configured"
        failedRunCount = "unknown"
        acknowledged = "unknown"
        ackExpired = "unknown"
        releaseGate = "not-configured"
        action = "none"
        taskCount = 0
        blockedTaskCount = 0
        source = "aggregate"
        details = @()
    }
    $usablePairs = @($TaskPairs | Where-Object { $null -ne $_ -and $null -ne $_.gate -and $_.gate.configured })
    if ($usablePairs.Count -le 0) {
        if ($null -ne $FallbackGate) {
            $result.configured = $FallbackGate.configured
            $result.state = $FallbackGate.state
            $result.failedRunCount = $FallbackGate.failedRunCount
            $result.acknowledged = $FallbackGate.acknowledged
            $result.ackExpired = $FallbackGate.ackExpired
            $result.releaseGate = $FallbackGate.releaseGate
            $result.action = $FallbackGate.action
            $result.taskCount = if ($FallbackGate.configured) { 1 } else { 0 }
            $result.blockedTaskCount = if ($FallbackGate.configured -and $FallbackGate.releaseGate -ne "passing") { 1 } else { 0 }
            $result.source = "single"
        }
        return [pscustomobject]$result
    }

    $failedTotal = 0
    $anyExpired = $false
    $anyUnacknowledged = $false
    $anyAcknowledgedReview = $false
    $anyUnavailable = $false
    $anyUnparseable = $false
    $allAcknowledged = $true
    $blockedCount = 0
    $details = New-Object System.Collections.Generic.List[object]
    foreach ($pair in $usablePairs) {
        $gate = $pair.gate
        $failed = 0
        [void][int]::TryParse((Format-StatusValue $gate.failedRunCount), [ref]$failed)
        $failedTotal += $failed
        if ($gate.releaseGate -ne "passing") {
            $blockedCount++
        }
        if ($gate.releaseGate -eq "blocked-ack-expired") {
            $anyExpired = $true
        } elseif ($gate.releaseGate -eq "blocked-unacknowledged-failure") {
            $anyUnacknowledged = $true
        } elseif ($gate.releaseGate -eq "acknowledged-failure-review-gated") {
            $anyAcknowledgedReview = $true
        } elseif ($gate.releaseGate -eq "automation-task-history-unparseable") {
            $anyUnparseable = $true
        } elseif ($gate.releaseGate -ne "passing") {
            $anyUnavailable = $true
        }
        if (-not (Convert-StatusBoolean $gate.acknowledged $false)) {
            $allAcknowledged = $false
        }
        $details.Add([pscustomobject]@{
                taskKind = $pair.taskKind
                taskName = $pair.taskName
                state = Format-StatusValue $gate.state
                failedRunCount = Format-StatusValue $gate.failedRunCount
                acknowledged = Format-StatusValue $gate.acknowledged
                ackExpired = Format-StatusValue $gate.ackExpired
                releaseGate = Format-StatusValue $gate.releaseGate
            })
    }

    $result.taskCount = $usablePairs.Count
    $result.blockedTaskCount = $blockedCount
    $result.failedRunCount = $failedTotal
    $result.acknowledged = if ($failedTotal -le 0) { $allAcknowledged.ToString().ToLowerInvariant() } else { (-not ($anyExpired -or $anyUnacknowledged -or $anyUnavailable -or $anyUnparseable)).ToString().ToLowerInvariant() }
    $result.ackExpired = $anyExpired.ToString().ToLowerInvariant()
    $result.details = [object[]]$details.ToArray()
    if ($anyUnavailable) {
        $result.state = "history-unavailable"
        $result.releaseGate = "automation-task-history-unavailable"
        $result.action = "restore automation task history artifact before release"
    } elseif ($anyUnparseable) {
        $result.state = "history-unparseable"
        $result.releaseGate = "automation-task-history-unparseable"
        $result.action = "regenerate automation task history with failedRunCount before release"
    } elseif ($anyExpired) {
        $result.state = "failed-ack-expired"
        $result.releaseGate = "blocked-ack-expired"
        $result.action = "renew task acknowledgement before release"
    } elseif ($anyUnacknowledged) {
        $result.state = "failed-unacknowledged"
        $result.releaseGate = "blocked-unacknowledged-failure"
        $result.action = "acknowledge failed automation task before release"
    } elseif ($anyAcknowledgedReview) {
        $result.state = "failed-acknowledged"
        $result.releaseGate = "acknowledged-failure-review-gated"
        $result.action = "continue remediation; keep release review gate until failures clear"
    } else {
        $result.state = "passing"
        $result.releaseGate = "passing"
        $result.action = "none"
    }
    [pscustomobject]$result
}

function Get-AutomationTaskHistoryFreshnessReadback([object[]]$TaskPairs, [object]$SchedulerReadback, [int]$FreshnessHours, [System.DateTimeOffset]$NowUtc) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        taskCount = 0
        freshTaskCount = 0
        staleTaskCount = 0
        unavailableTaskCount = 0
        unparseableTaskCount = 0
        thresholdHours = $FreshnessHours
        releaseGate = "not-configured"
        action = "none"
        details = @()
    }
    if ($FreshnessHours -le 0) {
        $result.state = "disabled"
        $result.releaseGate = "disabled"
        return [pscustomobject]$result
    }
    if ($null -eq $SchedulerReadback -or -not $SchedulerReadback.configured -or
        $SchedulerReadback.releaseGate -ne "scheduled-task-readback-registered") {
        return [pscustomobject]$result
    }

    $registeredNames = @{}
    foreach ($taskReadback in @($SchedulerReadback.details)) {
        if ($taskReadback.expectedRegistered -eq "true" -and $taskReadback.readback -eq "registered") {
            $registeredNames[(Format-StatusValue $taskReadback.taskName)] = $true
        }
    }
    $registeredPairs = @($TaskPairs | Where-Object {
            $null -ne $_ -and $registeredNames.ContainsKey((Format-StatusValue $_.taskName))
        })
    if ($registeredPairs.Count -le 0) {
        return [pscustomobject]$result
    }

    $result.configured = $true
    $details = New-Object System.Collections.Generic.List[object]
    foreach ($pair in $registeredPairs) {
        $state = "fresh"
        $latestAt = "unknown"
        $ageHours = "unknown"
        $releaseGate = "fresh"
        if ($null -eq $pair.historyState -or $pair.historyState.state -ne "ok") {
            $state = "history-unavailable"
            $releaseGate = "automation-task-history-unavailable"
            $result.unavailableTaskCount++
        } else {
            $latestRun = Get-JsonValue $pair.historyState.value "latestRun" $null
            $latestTime = Convert-ToUtcDateTimeOffset (Get-JsonValue $latestRun "timestamp" $null)
            if ($null -eq $latestTime) {
                $state = "history-unparseable"
                $releaseGate = "automation-task-history-timestamp-unparseable"
                $result.unparseableTaskCount++
            } else {
                $ageValue = ($NowUtc - $latestTime).TotalHours
                if ($ageValue -lt 0) {
                    $ageValue = 0
                }
                $ageHours = [math]::Round($ageValue, 1)
                $latestAt = $latestTime.UtcDateTime.ToString("o")
                if ($ageValue -gt $FreshnessHours) {
                    $state = "history-stale"
                    $releaseGate = "blocked-stale-automation-task-history"
                    $result.staleTaskCount++
                } else {
                    $result.freshTaskCount++
                }
            }
        }
        $details.Add([pscustomobject]@{
                taskKind = $pair.taskKind
                taskName = $pair.taskName
                state = $state
                latestAt = $latestAt
                ageHours = $ageHours
                thresholdHours = $FreshnessHours
                releaseGate = $releaseGate
            })
    }

    $result.taskCount = $registeredPairs.Count
    $result.details = [object[]]$details.ToArray()
    if ($result.unavailableTaskCount -gt 0) {
        $result.state = "history-unavailable"
        $result.releaseGate = "automation-task-history-unavailable"
        $result.action = "restore registered automation task history artifacts before release"
    } elseif ($result.unparseableTaskCount -gt 0) {
        $result.state = "history-unparseable"
        $result.releaseGate = "automation-task-history-timestamp-unparseable"
        $result.action = "regenerate automation task history with latestRun.timestamp before release"
    } elseif ($result.staleTaskCount -gt 0) {
        $result.state = "history-stale"
        $result.releaseGate = "blocked-stale-automation-task-history"
        $result.action = "run registered automation tasks and refresh history before release"
    } else {
        $result.state = "fresh"
        $result.releaseGate = "fresh"
        $result.action = "none"
    }
    [pscustomobject]$result
}

function Read-ScheduledTaskReadbackArtifact([string]$PathValue) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        tasks = @{}
    }
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    $state = Get-ArtifactState -PathValue $PathValue -ExpectJson
    if ($state.state -ne "ok") {
        $result.state = $state.state
        return [pscustomobject]$result
    }

    $taskMap = @{}
    $entries = @()
    $artifactTasks = Get-JsonValue $state.value "tasks" $null
    if ($null -ne $artifactTasks) {
        if ($artifactTasks -is [array]) {
            $entries = @($artifactTasks)
        } else {
            $entries = @($artifactTasks)
        }
    } else {
        $entries = @($state.value)
    }
    foreach ($entry in $entries) {
        $taskName = Format-StatusValue (Get-JsonValue $entry "taskName" (Get-JsonValue $entry "name" "unknown"))
        if ($taskName -eq "unknown") {
            continue
        }
        $registered = Convert-StatusBoolean (Get-JsonValue $entry "registered" (Get-JsonValue $entry "found" $null)) $false
        $readbackState = Format-StatusValue (Get-JsonValue $entry "state" (Get-JsonValue $entry "status" $(if ($registered) { "registered" } else { "missing" })))
        $schedulerState = Format-StatusValue (Get-JsonValue $entry "schedulerState" (Get-JsonValue $entry "taskState" "unknown"))
        $taskPath = Format-StatusValue (Get-JsonValue $entry "taskPath" "unknown")
        $taskMap[$taskName] = [pscustomobject]@{
            taskName = $taskName
            registered = $registered
            state = $readbackState
            schedulerState = $schedulerState
            taskPath = $taskPath
            source = Format-StatusValue (Get-JsonValue $entry "source" "artifact")
        }
    }
    $result.state = "ok"
    $result.tasks = $taskMap
    [pscustomobject]$result
}

function Get-ScheduledTaskRegistrationAttemptReadback([string]$PathValue) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        registrationRequested = "unknown"
        user = "unknown"
        taskCount = 0
        failedCount = 0
        releaseGate = "not-configured"
        action = "none"
        details = @()
    }
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    $state = Get-ArtifactState -PathValue $PathValue -ExpectJson
    if ($state.state -ne "ok") {
        $result.state = $state.state
        $result.releaseGate = "scheduled-task-registration-attempt-unavailable"
        $result.action = "restore scheduled task registration attempt artifact before release"
        return [pscustomobject]$result
    }
    $payload = $state.value
    $result.registrationRequested = Format-StatusValue (Get-JsonValue $payload "registrationRequested" "unknown")
    $result.user = Format-StatusValue (Get-JsonValue $payload "user" "unknown")
    $taskCountText = Format-StatusValue (Get-JsonValue $payload "taskCount" "0")
    $failedCountText = Format-StatusValue (Get-JsonValue $payload "failedCount" "0")
    $taskCount = 0
    $failedCount = 0
    [void][int]::TryParse($taskCountText, [ref]$taskCount)
    [void][int]::TryParse($failedCountText, [ref]$failedCount)
    $result.taskCount = $taskCount
    $result.failedCount = $failedCount

    $details = New-Object System.Collections.Generic.List[object]
    foreach ($task in @((Get-JsonValue $payload "tasks" @()))) {
        $details.Add([pscustomobject]@{
                taskKind = Format-StatusValue (Get-JsonValue $task "taskKind" "unknown")
                taskName = Format-StatusValue (Get-JsonValue $task "taskName" "unknown")
                registrationRequested = Format-StatusValue (Get-JsonValue $task "registrationRequested" $result.registrationRequested)
                status = Format-StatusValue (Get-JsonValue $task "status" "unknown")
                exitCode = Format-StatusValue (Get-JsonValue $task "exitCode" "unknown")
                failureClass = Format-StatusValue (Get-JsonValue $task "failureClass" "unknown")
                outputLineCount = Format-StatusValue (Get-JsonValue $task "outputLineCount" "unknown")
            })
    }
    $result.details = [object[]]$details.ToArray()
    if ($result.failedCount -gt 0) {
        $result.state = "failed"
        $result.releaseGate = "blocked-scheduled-task-registration-attempt-failed"
        $result.action = "review scheduled task registration attempt failures before release"
    } elseif (Convert-StatusBoolean $result.registrationRequested $false) {
        $result.state = "requested"
        $result.releaseGate = "scheduled-task-registration-attempt-requested"
        $result.action = "verify scheduler readback and task history after registration"
    } else {
        $result.state = "preview"
        $result.releaseGate = "scheduled-task-registration-preview"
        $result.action = "run bootstrap with -Register to create or update scheduled tasks"
    }
    [pscustomobject]$result
}

function Get-ScheduledTaskRegistrationAckGateReadback([object]$RegistrationAttempt, [object]$AckState, [int]$AckExpiryHours, [System.DateTimeOffset]$NowUtc) {
    $result = [ordered]@{
        configured = $false
        state = "not-configured"
        failedCount = "unknown"
        acknowledged = "unknown"
        ackExpired = "unknown"
        ageHours = "unknown"
        remainingHours = "unknown"
        overdueHours = "unknown"
        expiresAt = "unknown"
        releaseGate = "not-configured"
        action = "none"
    }
    if ($null -eq $RegistrationAttempt -or -not $RegistrationAttempt.configured) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    $result.failedCount = Format-StatusValue $RegistrationAttempt.failedCount

    $acknowledged = $false
    $ackExpired = $false
    $ackAt = $null
    if ($null -ne $AckState -and $AckState.state -eq "ok") {
        $acknowledged = Convert-StatusBoolean (Get-JsonValue $AckState.value "acknowledged" $null) $false
        $ackAt = Convert-ToUtcDateTimeOffset (Get-JsonValue $AckState.value "acknowledgedAt" $null)
    }
    if ($acknowledged) {
        if ($null -eq $ackAt) {
            $ackExpired = $true
        } else {
            $ageValue = ($NowUtc - $ackAt).TotalHours
            if ($ageValue -lt 0) {
                $ageValue = 0
            }
            $result.ageHours = [math]::Round($ageValue, 1)
            $expiresAt = $ackAt.AddHours($AckExpiryHours)
            $result.expiresAt = $expiresAt.UtcDateTime.ToString("o")
            $remainingValue = ($expiresAt - $NowUtc).TotalHours
            if ($remainingValue -lt 0) {
                $ackExpired = $true
                $result.remainingHours = 0
                $result.overdueHours = [math]::Round(-$remainingValue, 1)
            } else {
                $result.remainingHours = [math]::Round($remainingValue, 1)
                $result.overdueHours = 0
            }
        }
    }
    if ($ackExpired) {
        $acknowledged = $false
    }
    $result.acknowledged = $acknowledged.ToString().ToLowerInvariant()
    $result.ackExpired = $ackExpired.ToString().ToLowerInvariant()
    if ($RegistrationAttempt.failedCount -le 0) {
        $result.state = "passing"
        $result.releaseGate = "passing"
        return [pscustomobject]$result
    }
    if ($ackExpired) {
        $result.state = "failed-ack-expired"
        $result.releaseGate = "blocked-registration-ack-expired"
        $result.action = "renew scheduled task registration failure acknowledgement before release"
    } elseif (-not $acknowledged) {
        $result.state = "failed-unacknowledged"
        $result.releaseGate = "blocked-registration-unacknowledged-failure"
        $result.action = "acknowledge scheduled task registration failures before release"
    } else {
        $result.state = "failed-acknowledged"
        $result.releaseGate = "acknowledged-registration-failure-review-gated"
        $result.action = "continue scheduled task registration remediation before release"
    }
    [pscustomobject]$result
}

function Read-ScheduledTaskState([string]$TaskName, [object]$InjectedReadback) {
    if ($null -ne $InjectedReadback -and $InjectedReadback.configured) {
        if ($InjectedReadback.state -ne "ok") {
            return [pscustomobject]@{
                taskName = $TaskName
                registered = $false
                state = "readback-unavailable"
                schedulerState = "unknown"
                taskPath = "unknown"
                source = $InjectedReadback.state
            }
        }
        if ($InjectedReadback.tasks.ContainsKey($TaskName)) {
            return $InjectedReadback.tasks[$TaskName]
        }
        return [pscustomobject]@{
            taskName = $TaskName
            registered = $false
            state = "missing"
            schedulerState = "unknown"
            taskPath = "unknown"
            source = "artifact"
        }
    }

    if ($script:PlanOnly.IsPresent) {
        return [pscustomobject]@{
            taskName = $TaskName
            registered = $false
            state = "plan-only"
            schedulerState = "unknown"
            taskPath = "unknown"
            source = "plan-only"
        }
    }

    $command = Get-Command Get-ScheduledTask -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return [pscustomobject]@{
            taskName = $TaskName
            registered = $false
            state = "scheduler-readback-unavailable"
            schedulerState = "unknown"
            taskPath = "unknown"
            source = "Get-ScheduledTask-unavailable"
        }
    }

    try {
        $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $task) {
            return [pscustomobject]@{
                taskName = $TaskName
                registered = $false
                state = "missing"
                schedulerState = "unknown"
                taskPath = "unknown"
                source = "Get-ScheduledTask"
            }
        }
        return [pscustomobject]@{
            taskName = $TaskName
            registered = $true
            state = "registered"
            schedulerState = Format-StatusValue $task.State
            taskPath = Format-StatusValue $task.TaskPath
            source = "Get-ScheduledTask"
        }
    } catch {
        return [pscustomobject]@{
            taskName = $TaskName
            registered = $false
            state = "scheduler-readback-unavailable"
            schedulerState = "unknown"
            taskPath = "unknown"
            source = "Get-ScheduledTask-error"
        }
    }
}

function Get-ScheduledTaskRegistryReadback([object[]]$PreviewRecords, [string]$ReadbackJsonPath) {
    $totalCount = @($PreviewRecords).Count
    $injectedReadback = Read-ScheduledTaskReadbackArtifact $ReadbackJsonPath
    $details = New-Object System.Collections.Generic.List[object]
    $result = [ordered]@{
        configured = $totalCount -gt 0
        state = "not-configured"
        taskCount = $totalCount
        expectedRegisteredCount = 0
        registeredFoundCount = 0
        registeredMissingCount = 0
        registrationFailedCount = 0
        previewOnlyCount = 0
        unreadableCount = 0
        releaseGate = "scheduled-task-readback-not-configured"
        action = "configure automation task previews before release"
        source = if ($injectedReadback.configured) { "artifact" } else { "Get-ScheduledTask" }
        details = $null
    }
    if ($totalCount -le 0) {
        return [pscustomobject]$result
    }

    foreach ($previewRecord in $PreviewRecords) {
        if ($null -eq $previewRecord -or $null -eq $previewRecord.state -or $previewRecord.state.state -ne "ok") {
            continue
        }

        $previewRegisteredExpected = Convert-StatusBoolean $previewRecord.register $false
        $registeredExpected = $previewRegisteredExpected
        $taskName = Format-StatusValue $previewRecord.taskName
        $taskReadback = $null
        if ($taskName -ne "unknown") {
            $taskReadback = Read-ScheduledTaskState $taskName $injectedReadback
            if (-not $previewRegisteredExpected -and (Convert-StatusBoolean $taskReadback.registered $false)) {
                $registeredExpected = $true
            }
        }
        if (-not $registeredExpected) {
            $result.previewOnlyCount++
            $details.Add([pscustomobject]@{
                    taskName = $taskName
                    taskKind = Format-StatusValue $previewRecord.taskKind
                    expectedRegistered = "false"
                    readback = "preview-only"
                    schedulerState = "unknown"
                    taskPath = "unknown"
                    source = "preview"
            })
            continue
        }

        $result.expectedRegisteredCount++
        if ($taskName -eq "unknown") {
            $result.unreadableCount++
            $details.Add([pscustomobject]@{
                    taskName = $taskName
                    taskKind = Format-StatusValue $previewRecord.taskKind
                    expectedRegistered = "true"
                    readback = "missing-task-name"
                    schedulerState = "unknown"
                    taskPath = "unknown"
                    source = "preview"
            })
            continue
        }

        if ($null -eq $taskReadback) {
            $taskReadback = Read-ScheduledTaskState $taskName $injectedReadback
        }
        $taskState = Format-StatusValue $taskReadback.state
        if (Convert-StatusBoolean $taskReadback.registered $false) {
            $result.registeredFoundCount++
        } elseif ($taskState -eq "registration-failed") {
            $result.registrationFailedCount++
        } elseif ($taskState -eq "missing") {
            $result.registeredMissingCount++
        } else {
            $result.unreadableCount++
        }
        $details.Add([pscustomobject]@{
                taskName = $taskName
                taskKind = Format-StatusValue $previewRecord.taskKind
                expectedRegistered = "true"
                readback = $taskState
                schedulerState = Format-StatusValue $taskReadback.schedulerState
                taskPath = Format-StatusValue $taskReadback.taskPath
                source = Format-StatusValue $taskReadback.source
            })
    }

    $result["details"] = [object[]]$details.ToArray()
    if ($result.previewOnlyCount -gt 0) {
        $result.state = "preview-only"
        $result.releaseGate = "blocked-preview-only-automation-watch"
        $result.action = "register scheduled tasks with -Register or provide registered task artifacts before release"
    } elseif ($result.expectedRegisteredCount -le 0) {
        $result.state = "no-registered-tasks"
        $result.releaseGate = "automation-watch-not-registered"
        $result.action = "register at least one automation task before release"
    } elseif ($result.registrationFailedCount -gt 0) {
        $result.state = "registration-failed"
        $result.releaseGate = "blocked-scheduled-task-registration-failed"
        $result.action = "review scheduled task registration attempt evidence before release"
    } elseif ($result.registeredMissingCount -gt 0) {
        $result.state = "registered-missing"
        $result.releaseGate = "blocked-scheduled-task-missing"
        $result.action = "restore missing scheduled tasks before release"
    } elseif ($result.unreadableCount -gt 0) {
        $result.state = "scheduler-readback-unavailable"
        $result.releaseGate = "blocked-scheduler-readback-unavailable"
        $result.action = "provide scheduler readback evidence before release"
    } elseif ($result.registeredFoundCount -eq $result.expectedRegisteredCount) {
        $result.state = "registered"
        $result.releaseGate = "scheduled-task-readback-registered"
        $result.action = "verify scheduler run history stays fresh before release"
    } else {
        $result.state = "partial"
        $result.releaseGate = "blocked-scheduled-task-readback-partial"
        $result.action = "reconcile scheduled task registration evidence before release"
    }
    [pscustomobject]$result
}

function Get-AutomationTaskWatchGateReadback([object[]]$PreviewRecords, [object]$AckGate, [object]$SchedulerReadback, [object]$RegistrationAckGate, [object]$FreshnessGate, [object]$EvidenceGate) {
    $totalCount = @($PreviewRecords).Count
    $result = [ordered]@{
        configured = $totalCount -gt 0
        state = "not-configured"
        taskCount = $totalCount
        registeredCount = 0
        previewOnlyCount = 0
        invalidPreviewCount = 0
        releaseGate = "automation-watch-not-configured"
        action = "configure automation task previews before release"
    }
    if ($totalCount -le 0) {
        return [pscustomobject]$result
    }

    foreach ($previewRecord in $PreviewRecords) {
        if ($null -eq $previewRecord -or $null -eq $previewRecord.state -or $previewRecord.state.state -ne "ok") {
            $result.invalidPreviewCount++
            continue
        }
        if (Convert-StatusBoolean $previewRecord.register $false) {
            $result.registeredCount++
        } else {
            $result.previewOnlyCount++
        }
    }
    if ($null -ne $SchedulerReadback -and $SchedulerReadback.configured) {
        $result.registeredCount = $SchedulerReadback.expectedRegisteredCount
        $result.previewOnlyCount = $SchedulerReadback.previewOnlyCount
    }

    if ($result.invalidPreviewCount -gt 0) {
        $result.state = "preview-unavailable"
        $result.releaseGate = "automation-watch-preview-unavailable"
        $result.action = "regenerate automation task previews before release"
    } elseif ($result.previewOnlyCount -gt 0) {
        $result.state = "preview-only"
        $result.releaseGate = "blocked-preview-only-automation-watch"
        $result.action = "register scheduled tasks with -Register or provide registered task artifacts before release"
    } elseif ($result.registeredCount -le 0) {
        $result.state = "no-registered-tasks"
        $result.releaseGate = "automation-watch-not-registered"
        $result.action = "register at least one automation task before release"
    } elseif ($null -ne $SchedulerReadback -and $SchedulerReadback.configured -and $SchedulerReadback.releaseGate -ne "scheduled-task-readback-registered") {
        if ($SchedulerReadback.releaseGate -eq "blocked-scheduled-task-registration-failed" -and
            $null -ne $RegistrationAckGate -and $RegistrationAckGate.configured -and
            $RegistrationAckGate.releaseGate -ne "passing") {
            $result.state = "registration-failed-ack-gated"
            $result.releaseGate = $RegistrationAckGate.releaseGate
            $result.action = $RegistrationAckGate.action
        } else {
            $result.state = $SchedulerReadback.state
            $result.releaseGate = $SchedulerReadback.releaseGate
            $result.action = $SchedulerReadback.action
        }
    } elseif ($null -ne $AckGate -and $AckGate.configured -and $AckGate.releaseGate -ne "passing") {
        $result.state = "registered-ack-gated"
        $result.releaseGate = $AckGate.releaseGate
        $result.action = $AckGate.action
    } elseif ($null -ne $EvidenceGate -and $EvidenceGate.configured -and $EvidenceGate.releaseGate -ne "passing") {
        $result.state = "registered-evidence-gated"
        $result.releaseGate = $EvidenceGate.releaseGate
        $result.action = $EvidenceGate.action
    } elseif ($null -ne $FreshnessGate -and $FreshnessGate.configured -and $FreshnessGate.releaseGate -ne "fresh") {
        $result.state = "registered-history-gated"
        $result.releaseGate = $FreshnessGate.releaseGate
        $result.action = $FreshnessGate.action
    } else {
        $result.state = "registered"
        $result.releaseGate = "automation-watch-registered"
        $result.action = "none"
    }
    [pscustomobject]$result
}

function Find-SchedulerReadbackDetail([object]$SchedulerReadback, [string]$TaskName, [string]$TaskKind = "") {
    if ($null -eq $SchedulerReadback -or -not $SchedulerReadback.configured) {
        return $null
    }
    $normalizedTaskName = Format-StatusValue $TaskName
    $normalizedTaskKind = Format-StatusValue $TaskKind
    $fallbackByName = $null
    foreach ($detail in @($SchedulerReadback.details)) {
        $detailName = Format-StatusValue $detail.taskName
        $detailKind = Format-StatusValue $detail.taskKind
        if ($detailName -eq $normalizedTaskName) {
            if ($normalizedTaskName -eq "unknown") {
                if ($detailKind -eq $normalizedTaskKind) {
                    return $detail
                }
            } elseif ($normalizedTaskKind -eq "unknown" -or $detailKind -eq $normalizedTaskKind) {
                return $detail
            } elseif ($null -eq $fallbackByName) {
                $fallbackByName = $detail
            }
        }
    }
    $fallbackByName
}

function Format-PreviewDescriptor([object]$PreviewRecord, [object]$SchedulerReadbackDetail = $null) {
    $parts = New-Object System.Collections.Generic.List[string]
    $parts.Add(('label=`{0}`' -f (Format-StatusValue $PreviewRecord.label)))
    $parts.Add(('kind=`{0}`' -f (Format-StatusValue $PreviewRecord.taskKind)))
    $parts.Add(('name=`{0}`' -f (Format-StatusValue $PreviewRecord.taskName)))
    $parts.Add(('display=`{0}`' -f (Format-StatusValue $PreviewRecord.taskDisplayName)))
    $parts.Add(('state=`{0}`' -f (Format-StatusValue $PreviewRecord.state.state)))
    $parts.Add(('format=`{0}`' -f (Format-StatusValue $PreviewRecord.previewFormat)))
    $parts.Add(('readOnly=`{0}`' -f (Format-StatusValue $PreviewRecord.readOnly)))
    $parts.Add(('register=`{0}`' -f (Format-StatusValue $PreviewRecord.register)))
    if ($null -ne $SchedulerReadbackDetail) {
        $effectiveRegistered = "false"
        if ((Format-StatusValue $SchedulerReadbackDetail.expectedRegistered) -eq "true" -and
            (Format-StatusValue $SchedulerReadbackDetail.readback) -eq "registered") {
            $effectiveRegistered = "true"
        }
        $parts.Add(('schedulerReadback=`{0}`' -f (Format-StatusValue $SchedulerReadbackDetail.readback)))
        $parts.Add(('effectiveRegistered=`{0}`' -f $effectiveRegistered))
    }
    $parts.Add(('schedule=`{0}`' -f (Format-StatusValue $PreviewRecord.scheduleSummary)))
    $parts.Add(('path=`{0}`' -f (Format-StatusValue (Format-RepoRelativePath $PreviewRecord.path))))
    $parts -join ", "
}

function Resolve-DefaultTaskPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Format-RepoRelativePath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return "unknown"
    }
    try {
        $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
        $resolvedPath = if ([System.IO.Path]::IsPathRooted($PathValue)) {
            $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
        } else {
            $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath((Join-Path $repoRoot $PathValue))
        }
        if ($resolvedPath.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $resolvedPath.Substring($repoRoot.Length).TrimStart('\', '/')
        }
    } catch {
    }
    $PathValue
}

function Initialize-DefaultAutomationTasksIfNeeded {
    $hasExplicitTaskConfiguration = Has-ConfigurationHint @(
        $script:DatabaseHealthStatusPath,
        $script:DatabaseHealthLastRunPath,
        $script:DatabaseHealthTaskPreviewPath,
        $script:LargeFileGovernanceStatusPath,
        $script:LargeFileGovernanceLastRunPath,
        $script:LargeFileGovernanceTaskPreviewPath,
        $script:AutomationTaskHistoryPath,
        $script:AutomationTaskAckPath,
        $script:TaskPreviewPath
    )
    if ($hasExplicitTaskConfiguration -and -not $script:BootstrapDefaultTasks.IsPresent) {
        return
    }

    $bootstrapScript = Join-Path $PSScriptRoot "bootstrap-automation-tasks.ps1"
    if (-not (Test-Path -LiteralPath $bootstrapScript -PathType Leaf)) {
        throw "Default automation task bootstrap script not found: $bootstrapScript"
    }
    $bootstrapOutputDir = Resolve-DefaultTaskPath $script:DefaultTaskOutputDir
    $defaultScheduledTaskReadbackPath = Join-Path $bootstrapOutputDir "scheduled-task-readback.json"
    $defaultRegistrationAttemptPath = Join-Path $bootstrapOutputDir "scheduled-task-registration-attempt.json"
    $defaultRegistrationAckPath = Join-Path $bootstrapOutputDir "scheduled-task-registration-ack.json"
    if (-not $script:PlanOnly.IsPresent) {
        $bootstrapArguments = @(
            "-ExecutionPolicy", "Bypass",
            "-File", $bootstrapScript,
            "-OutputDir", $bootstrapOutputDir,
            "-AckExpiryHours", $script:TaskAckExpiryHours,
            "-HistoryRetentionCount", $script:TaskHistoryRetentionCount,
            "-RegistrationAckPath", $defaultRegistrationAckPath
        )
        if ($script:RegisterDefaultTasks.IsPresent) {
            $bootstrapArguments += @("-Register", "-User", $script:DefaultTaskUser)
        }
        if ($script:FailOnSensitive.IsPresent) {
            $bootstrapArguments += "-FailOnSensitive"
        }
        & powershell @bootstrapArguments | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Default automation task bootstrap failed with exit code $LASTEXITCODE"
        }
    }

    if ([string]::IsNullOrWhiteSpace($script:DatabaseHealthTaskPreviewPath)) {
        $script:DatabaseHealthTaskPreviewPath = Join-Path $bootstrapOutputDir "database-health\database-health-task\database-health-task-preview.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:DatabaseHealthStatusPath)) {
        $script:DatabaseHealthStatusPath = Join-Path $bootstrapOutputDir "database-health\database-health-status.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:DatabaseHealthLastRunPath)) {
        $script:DatabaseHealthLastRunPath = Join-Path $bootstrapOutputDir "database-health\database-health-task\last-run.log"
    }
    if ([string]::IsNullOrWhiteSpace($script:LargeFileGovernanceTaskPreviewPath)) {
        $script:LargeFileGovernanceTaskPreviewPath = Join-Path $bootstrapOutputDir "large-file-governance\scheduled-task\scheduled-task-preview.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:LargeFileGovernanceStatusPath)) {
        $script:LargeFileGovernanceStatusPath = Join-Path $bootstrapOutputDir "large-file-governance\large-file-governance-dashboard.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:LargeFileGovernanceLastRunPath)) {
        $script:LargeFileGovernanceLastRunPath = Join-Path $bootstrapOutputDir "large-file-governance\scheduled-task\last-run.log"
    }
    $pgsqlPreviewPath = Join-Path $bootstrapOutputDir "pgsql-release-acceptance\pgsql-release-acceptance-task\pgsql-release-acceptance-task-preview.json"
    $hasPgsqlPreview = @($script:TaskPreviewPath | Where-Object { [string]$_ -eq $pgsqlPreviewPath }).Count -gt 0
    if (-not $hasPgsqlPreview) {
        $script:TaskPreviewPath += $pgsqlPreviewPath
    }
    if ([string]::IsNullOrWhiteSpace($script:AutomationTaskHistoryPath)) {
        $script:AutomationTaskHistoryPath = Join-Path $bootstrapOutputDir "database-health\database-health-task\automation-task-history.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:AutomationTaskAckPath)) {
        $script:AutomationTaskAckPath = Join-Path $bootstrapOutputDir "database-health\database-health-task\automation-task-ack.json"
    }
    if ([string]::IsNullOrWhiteSpace($script:ScheduledTaskReadbackJsonPath)) {
        $script:ScheduledTaskReadbackJsonPath = $defaultScheduledTaskReadbackPath
    }
    if ([string]::IsNullOrWhiteSpace($script:ScheduledTaskRegistrationAttemptPath)) {
        $script:ScheduledTaskRegistrationAttemptPath = $defaultRegistrationAttemptPath
    }
    if ([string]::IsNullOrWhiteSpace($script:ScheduledTaskRegistrationAckPath)) {
        $script:ScheduledTaskRegistrationAckPath = $defaultRegistrationAckPath
    }
}

if ([string]::IsNullOrWhiteSpace($Head)) {
    $Head = if ($PlanOnly) { "unknown" } else { Invoke-GitText @("rev-parse", "HEAD") }
}
if ([string]::IsNullOrWhiteSpace($TrackedRemoteBranch)) {
    $TrackedRemoteBranch = "origin/main"
}
if ([string]::IsNullOrWhiteSpace($TrackedRemoteHash)) {
    $TrackedRemoteHash = if (-not [string]::IsNullOrWhiteSpace($OriginMain)) {
        $OriginMain
    } elseif ($PlanOnly) {
        "unknown"
    } else {
        Invoke-GitText @("rev-parse", $TrackedRemoteBranch)
    }
}
if ([string]::IsNullOrWhiteSpace($OriginMain)) {
    $OriginMain = $TrackedRemoteHash
}

if ([string]::IsNullOrWhiteSpace($LocalVerificationStatusPath)) {
    $LocalVerificationStatusPath = Join-Path $BuildDir "local-verification-status.json"
}
if ([string]::IsNullOrWhiteSpace($AutomationPolicyPath)) {
    $defaultAutomationPolicyPath = "docs\automation-policy.json"
    if (Test-Path -LiteralPath (Resolve-RepoPath $defaultAutomationPolicyPath) -PathType Leaf) {
        $AutomationPolicyPath = $defaultAutomationPolicyPath
    }
}
if ([string]::IsNullOrWhiteSpace($GitHubWindowsBuildStatusPath)) {
    $GitHubWindowsBuildStatusPath = Join-Path $BuildDir "github-windows-build-status.json"
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityJsonPath)) {
    $E2ERolloutObservabilityJsonPath = Join-Path $BuildDir "e2e_rollout_observability_evidence\e2e-rollout-observability.json"
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityMarkdownPath)) {
    $E2ERolloutObservabilityMarkdownPath = Join-Path $BuildDir "e2e_rollout_observability_evidence\e2e-rollout-observability.md"
}
if ([string]::IsNullOrWhiteSpace($E2EReleaseEvidenceManifestPath)) {
    $E2EReleaseEvidenceManifestPath = Join-Path $BuildDir "e2e_release_evidence\e2e-release-evidence-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($E2ELinkedReleaseCandidateManifestPath)) {
    $defaultLinkedReleaseCandidateManifestPath =
        Join-Path $BuildDir "e2e_release_evidence_linked_candidate\e2e-release-evidence-manifest.json"
    if (Test-Path -LiteralPath (Resolve-RepoPath $defaultLinkedReleaseCandidateManifestPath) -PathType Leaf) {
        $E2ELinkedReleaseCandidateManifestPath = $defaultLinkedReleaseCandidateManifestPath
    }
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityJsonPath)) {
    $packagedRolloutJsonPath =
        Join-Path $BuildDir "e2e_release_evidence\e2e-release-evidence\e2e-rollout-observability.json"
    if (Test-Path -LiteralPath (Resolve-RepoPath $packagedRolloutJsonPath) -PathType Leaf) {
        $E2ERolloutObservabilityJsonPath = $packagedRolloutJsonPath
    }
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityMarkdownPath)) {
    $packagedRolloutMarkdownPath =
        Join-Path $BuildDir "e2e_release_evidence\e2e-release-evidence\e2e-rollout-observability.md"
    if (Test-Path -LiteralPath (Resolve-RepoPath $packagedRolloutMarkdownPath) -PathType Leaf) {
        $E2ERolloutObservabilityMarkdownPath = $packagedRolloutMarkdownPath
    }
}
if ([string]::IsNullOrWhiteSpace($AutomationAckDrillPath)) {
    $defaultAutomationAckDrillPath =
        Join-Path $BuildDir "automation-tasks\ack-drill\automation-ack-drill.json"
    if (Test-Path -LiteralPath (Resolve-RepoPath $defaultAutomationAckDrillPath) -PathType Leaf) {
        $AutomationAckDrillPath = $defaultAutomationAckDrillPath
    }
}
if ([string]::IsNullOrWhiteSpace($S3RealBackendReadinessPath) `
        -and -not $PlanOnly.IsPresent `
        -and -not $BootstrapDefaultTasks.IsPresent) {
    $defaultS3RealBackendReadinessPath =
        Join-Path $BuildDir "manual-s3-real-backend\s3-real-backend-readiness.json"
    if (Test-Path -LiteralPath (Resolve-RepoPath $defaultS3RealBackendReadinessPath) -PathType Leaf) {
        $S3RealBackendReadinessPath = $defaultS3RealBackendReadinessPath
    }
}
$localVerificationReadback = Get-LocalVerificationStatusReadback $LocalVerificationStatusPath
$automationPolicyReadback = Get-AutomationPolicyReadback $AutomationPolicyPath
$script:GitHubWindowsBuildPolicyResolved = Normalize-GitHubWindowsBuildPolicy $GitHubWindowsBuildPolicy
$gitHubWindowsBuildPolicySource = if (-not [string]::IsNullOrWhiteSpace($script:GitHubWindowsBuildPolicyResolved)) {
    "parameter"
} else {
    "default-required"
}
if ([string]::IsNullOrWhiteSpace($script:GitHubWindowsBuildPolicyResolved)) {
    if ($automationPolicyReadback.valid) {
        $script:GitHubWindowsBuildPolicyResolved = $automationPolicyReadback.githubWindowsBuildPolicy
        $gitHubWindowsBuildPolicySource = $automationPolicyReadback.source
    } else {
        $script:GitHubWindowsBuildPolicyResolved = "required"
        if ($automationPolicyReadback.configured) {
            $gitHubWindowsBuildPolicySource = $automationPolicyReadback.source
        }
    }
}

$ciReadbackSource = if (Is-UnknownStatus $CiStatus) { "auto" } else { "parameter" }
if (Is-UnknownStatus $CiStatus) {
    if ($script:GitHubWindowsBuildPolicyResolved -eq "disabled") {
        $CiStatus = "disabled-by-policy"
        if ([string]::IsNullOrWhiteSpace($CiRunId)) {
            $CiRunId = "not-required"
        }
        $ciReadbackSource = $gitHubWindowsBuildPolicySource
    } else {
        $ciReadback = Get-GitHubWindowsBuildReadback $Head
        $CiStatus = $ciReadback.status
        if ([string]::IsNullOrWhiteSpace($CiRunId)) {
            $CiRunId = $ciReadback.runId
        }
        $ciReadbackSource = $ciReadback.source
    }
}

$buildReadbackSource = if (Is-UnknownStatus $BuildStatus) { "auto" } else { "parameter" }
if (Is-UnknownStatus $BuildStatus) {
    if ($localVerificationReadback.readable -and -not [string]::IsNullOrWhiteSpace($localVerificationReadback.buildStatus)) {
        $BuildStatus = $localVerificationReadback.buildStatus
        $buildReadbackSource = $localVerificationReadback.source
    } else {
        $buildReadback = Get-LocalBuildReadback $BuildDir
        $BuildStatus = $buildReadback.status
        $buildReadbackSource = $buildReadback.source
    }
}

$ctestReadbackSource = if ((Is-UnknownStatus $CTestStatus) -or $CTestCount -le 0) { "auto" } else { "parameter" }
if ((Is-UnknownStatus $CTestStatus) -or $CTestCount -le 0) {
    $hasLocalVerificationCTestStatus = -not [string]::IsNullOrWhiteSpace($localVerificationReadback.ctestStatus)
    $hasLocalVerificationCTestCount = $localVerificationReadback.ctestCount -gt 0
    $hasLocalVerificationCTest = $localVerificationReadback.readable -and ($hasLocalVerificationCTestStatus -or $hasLocalVerificationCTestCount)
    if ($hasLocalVerificationCTest) {
        $ctestStatusUnknown = Is-UnknownStatus $CTestStatus
        if ($ctestStatusUnknown -and $hasLocalVerificationCTestStatus) {
            $CTestStatus = $localVerificationReadback.ctestStatus
        }
        if ($CTestCount -le 0 -and $hasLocalVerificationCTestCount) {
            $CTestCount = [int]$localVerificationReadback.ctestCount
        }
        $ctestReadbackSource = $localVerificationReadback.source
    } else {
        $ctestReadback = Get-CTestReadback $BuildDir $CTestLogPath
        if (Is-UnknownStatus $CTestStatus) {
            $CTestStatus = $ctestReadback.status
        }
        if ($CTestCount -le 0) {
            $CTestCount = [int]$ctestReadback.count
    }
    $ctestReadbackSource = $ctestReadback.source
    }
}

$e2eRolloutJsonState = Get-ArtifactState -PathValue $E2ERolloutObservabilityJsonPath -ExpectJson
$e2eRolloutMarkdownState = Get-ArtifactState -PathValue $E2ERolloutObservabilityMarkdownPath
$e2eRolloutReadback = Get-E2ERolloutObservabilityReadback $e2eRolloutJsonState $e2eRolloutMarkdownState
$e2eReleaseEvidenceManifestState = Get-ArtifactState -PathValue $E2EReleaseEvidenceManifestPath -ExpectJson
$e2eReleaseEvidenceReadback = Get-E2EReleaseEvidenceReadback $e2eReleaseEvidenceManifestState $Head
$e2eLinkedReleaseCandidateManifestState =
    Get-ArtifactState -PathValue $E2ELinkedReleaseCandidateManifestPath -ExpectJson
$e2eLinkedReleaseCandidateReadback =
    Get-E2EReleaseEvidenceReadback $e2eLinkedReleaseCandidateManifestState $Head

if (($e2eRolloutReadback.state -eq "ok") `
        -and ((Format-StatusValue $e2eRolloutReadback.releaseGate) -eq "production-rollout-observability-blocked-not-linked") `
        -and ($e2eReleaseEvidenceReadback.state -eq "ok") `
        -and ((Format-StatusValue $e2eReleaseEvidenceReadback.releaseReady) -eq "true") `
        -and ((Format-StatusValue $e2eReleaseEvidenceReadback.targetMatchesCurrentHead) -eq "true")) {
    $packagedRolloutJsonPath =
        Join-Path $BuildDir "e2e_release_evidence\e2e-release-evidence\e2e-rollout-observability.json"
    $packagedRolloutMarkdownPath =
        Join-Path $BuildDir "e2e_release_evidence\e2e-release-evidence\e2e-rollout-observability.md"
    $packagedRolloutJsonState = Get-ArtifactState -PathValue $packagedRolloutJsonPath -ExpectJson
    $packagedRolloutMarkdownState = Get-ArtifactState -PathValue $packagedRolloutMarkdownPath
    $packagedRolloutReadback =
        Get-E2ERolloutObservabilityReadback $packagedRolloutJsonState $packagedRolloutMarkdownState
    if (($packagedRolloutReadback.state -eq "ok") `
            -and ((Format-StatusValue $packagedRolloutReadback.releaseGate) -eq "production-rollout-observability-ready")) {
        $e2eRolloutJsonState = $packagedRolloutJsonState
        $e2eRolloutMarkdownState = $packagedRolloutMarkdownState
        $e2eRolloutReadback = $packagedRolloutReadback
    }
}
$automationAckDrillState = Get-ArtifactState -PathValue $AutomationAckDrillPath -ExpectJson
$s3RealBackendReadinessState = Get-ArtifactState -PathValue $S3RealBackendReadinessPath -ExpectJson
$s3RealBackendReadinessReadback =
    Get-S3RealBackendReadinessReadback $s3RealBackendReadinessState
if ($e2eRolloutReadback.state -eq "ok") {
    $e2eRolloutReadback.ciStatus = Format-StatusValue $CiStatus
    $e2eRolloutReadback.ciRunId = Format-StatusValue $(if ([string]::IsNullOrWhiteSpace($CiRunId)) { "unknown" } else { $CiRunId })
    $e2eRolloutReadback.ciSource = Format-StatusValue $ciReadbackSource
    $e2eRolloutReadback.localBuildStatus = Format-StatusValue $BuildStatus
    $e2eRolloutReadback.localCTestStatus = Format-StatusValue $CTestStatus
    $e2eRolloutReadback.localCTestCount = [int]$CTestCount
}

Initialize-DefaultAutomationTasksIfNeeded

$normalizedTaskPreviewPaths = Normalize-PathList $TaskPreviewPath
$databaseHealthPreview = Get-ArtifactState -PathValue $DatabaseHealthTaskPreviewPath -ExpectJson
$largeFileGovernancePreview = Get-ArtifactState -PathValue $LargeFileGovernanceTaskPreviewPath -ExpectJson
$previewRecords = New-Object System.Collections.Generic.List[object]
if (-not [string]::IsNullOrWhiteSpace($DatabaseHealthTaskPreviewPath)) {
    $previewRecords.Add((New-PreviewRecord "database-health" $DatabaseHealthTaskPreviewPath))
}
if (-not [string]::IsNullOrWhiteSpace($LargeFileGovernanceTaskPreviewPath)) {
    $previewRecords.Add((New-PreviewRecord "large-file-governance" $LargeFileGovernanceTaskPreviewPath))
}
foreach ($previewPath in $normalizedTaskPreviewPaths) {
    if ($previewPath -ne $DatabaseHealthTaskPreviewPath -and $previewPath -ne $LargeFileGovernanceTaskPreviewPath) {
        $previewRecords.Add((New-PreviewRecord "generic" $previewPath))
    }
}
$previewRecords = $previewRecords.ToArray()
$databaseHealthPreviewCandidates = @($previewRecords | Where-Object { $_.label -eq "database-health" -or $_.taskKind -eq "database-health" })
$largeFileGovernancePreviewCandidates = @($previewRecords | Where-Object { $_.label -eq "large-file-governance" -or $_.taskKind -eq "large-file-governance" })
$pgsqlReleaseAcceptancePreviewCandidates = @($previewRecords | Where-Object { $_.taskKind -eq "pgsql-release-acceptance" })
$genericPreviewCandidates = @($previewRecords | Where-Object { $_.taskKind -ne "database-health" -and $_.taskKind -ne "large-file-governance" -and $_.taskKind -ne "pgsql-release-acceptance" })

if ([string]::IsNullOrWhiteSpace($DatabaseHealthStatusPath)) {
    $DatabaseHealthStatusPath = Resolve-PreviewCollectionValueAny $databaseHealthPreviewCandidates @("statusArtifactPath", "statusPath")
}
if ([string]::IsNullOrWhiteSpace($DatabaseHealthLastRunPath)) {
    $DatabaseHealthLastRunPath = Resolve-PreviewCollectionValueAny $databaseHealthPreviewCandidates @("lastRunPath", "logPath")
}
if ([string]::IsNullOrWhiteSpace($LargeFileGovernanceStatusPath)) {
    $LargeFileGovernanceStatusPath = Resolve-PreviewCollectionValueAny $largeFileGovernancePreviewCandidates @("statusArtifactPath", "dashboardPath")
}
if ([string]::IsNullOrWhiteSpace($LargeFileGovernanceLastRunPath)) {
    $previewLogPath = Resolve-PreviewCollectionValueAny $largeFileGovernancePreviewCandidates @("lastRunPath", "logPath")
    if (-not [string]::IsNullOrWhiteSpace($previewLogPath)) {
        $LargeFileGovernanceLastRunPath = $previewLogPath
    } else {
        $LargeFileGovernanceLastRunPath = Resolve-PreviewValue $largeFileGovernancePreview "launcherPath"
        if (-not [string]::IsNullOrWhiteSpace($LargeFileGovernanceLastRunPath)) {
            $LargeFileGovernanceLastRunPath = Join-Path (Split-Path -Parent (Resolve-RepoPath $LargeFileGovernanceLastRunPath)) "last-run.log"
        }
    }
}
if ([string]::IsNullOrWhiteSpace($AutomationTaskHistoryPath)) {
    $AutomationTaskHistoryPath = Resolve-PreviewCollectionValueAny @($previewRecords) @("historyArtifactPath", "historyPath")
}
if ([string]::IsNullOrWhiteSpace($AutomationTaskAckPath)) {
    $AutomationTaskAckPath = Resolve-PreviewCollectionValueAny @($previewRecords) @("ackArtifactPath", "ackPath")
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
$statusNow = Convert-ToUtcDateTimeOffset $StatusNowUtc
if ($null -eq $statusNow) {
    $statusNow = [System.DateTimeOffset]::UtcNow
}
$script:AutomationStatusNowUtc = $statusNow
$generatedAt = $statusNow.UtcDateTime.ToString("o")
$databaseHealthPreviewRecord = @($databaseHealthPreviewCandidates | Select-Object -First 1)[0]
$largeFileGovernancePreviewRecord = @($largeFileGovernancePreviewCandidates | Select-Object -First 1)[0]
$databaseHealthStatusConfigMatch = Find-PreviewRecordForArtifact $databaseHealthPreviewCandidates @("statusArtifactPath", "statusPath") "status"
$databaseHealthStatusConfig = $databaseHealthStatusConfigMatch.configuration
$databaseHealthStatusPreviewState = if ($null -ne $databaseHealthStatusConfigMatch.record) { $databaseHealthStatusConfigMatch.record.state } else { $null }
$databaseHealthLastRunConfigMatch = Find-PreviewRecordForArtifact $databaseHealthPreviewCandidates @("lastRunPath", "logPath") "lastRun"
$databaseHealthLastRunConfig = $databaseHealthLastRunConfigMatch.configuration
$databaseHealthLastRunPreviewState = if ($null -ne $databaseHealthLastRunConfigMatch.record) { $databaseHealthLastRunConfigMatch.record.state } else { $null }
$largeFileGovernanceStatusConfigMatch = Find-PreviewRecordForArtifact $largeFileGovernancePreviewCandidates @("statusArtifactPath", "dashboardPath") "status"
$largeFileGovernanceStatusConfig = $largeFileGovernanceStatusConfigMatch.configuration
$largeFileGovernanceStatusPreviewState = if ($null -ne $largeFileGovernanceStatusConfigMatch.record) { $largeFileGovernanceStatusConfigMatch.record.state } else { $null }
$largeFileGovernanceLastRunConfigMatch = Find-PreviewRecordForArtifact $largeFileGovernancePreviewCandidates @("lastRunPath", "logPath") "lastRun"
$largeFileGovernanceLastRunConfig = $largeFileGovernanceLastRunConfigMatch.configuration
$largeFileGovernanceLastRunPreviewState = if ($null -ne $largeFileGovernanceLastRunConfigMatch.record) { $largeFileGovernanceLastRunConfigMatch.record.state } else { $null }
$automationTaskEvidenceReadbacks = @($previewRecords | ForEach-Object { Get-GenericTaskReadback $_ })
$pgsqlReleaseAcceptanceReadbacks = @($automationTaskEvidenceReadbacks | Where-Object {
        (Format-StatusValue $_.previewRecord.taskKind) -eq "pgsql-release-acceptance"
    })
$automationTaskHistoryConfigMatch = Find-PreviewRecordForArtifact @($previewRecords) @("historyArtifactPath", "historyPath") "history"
$automationTaskHistoryConfig = $automationTaskHistoryConfigMatch.configuration
$automationTaskHistoryPreviewState = if ($null -ne $automationTaskHistoryConfigMatch.record) { $automationTaskHistoryConfigMatch.record.state } else { $null }
$automationTaskAckConfigMatch = Find-PreviewRecordForArtifact @($previewRecords) @("ackArtifactPath", "ackPath") "ack"
$automationTaskAckConfig = $automationTaskAckConfigMatch.configuration
$automationTaskAckPreviewState = if ($null -ne $automationTaskAckConfigMatch.record) { $automationTaskAckConfigMatch.record.state } else { $null }

$databaseHealthStatusState = Get-ArtifactState -PathValue $DatabaseHealthStatusPath -ExpectJson
$databaseHealthLastRunState = Get-ArtifactState -PathValue $DatabaseHealthLastRunPath
$largeFileGovernanceStatusState = Get-ArtifactState -PathValue $LargeFileGovernanceStatusPath -ExpectJson
$largeFileGovernanceLastRunState = Get-ArtifactState -PathValue $LargeFileGovernanceLastRunPath
$automationTaskHistoryState = Get-ArtifactState -PathValue $AutomationTaskHistoryPath -ExpectJson
$automationTaskAckState = Get-ArtifactState -PathValue $AutomationTaskAckPath -ExpectJson
$scheduledTaskRegistrationAckState = Get-ArtifactState -PathValue $ScheduledTaskRegistrationAckPath -ExpectJson
$automationTaskHistoryAckPairs = @($previewRecords | ForEach-Object { Get-AutomationTaskHistoryAckPairReadback $_ })
$automationTaskAckGateConfigured = Has-ConfigurationHint @(
    $AutomationTaskHistoryPath,
    $AutomationTaskAckPath,
    $DatabaseHealthTaskPreviewPath,
    $LargeFileGovernanceTaskPreviewPath,
    $normalizedTaskPreviewPaths
)
$automationTaskAckSingleGate = Get-AutomationTaskAckGateReadback $automationTaskHistoryState $automationTaskAckState $automationTaskAckGateConfigured
$automationTaskAckGate = Get-AutomationTaskAckGateAggregateReadback $automationTaskHistoryAckPairs $automationTaskAckSingleGate $automationTaskAckGateConfigured
$scheduledTaskRegistryReadback = Get-ScheduledTaskRegistryReadback @($previewRecords) $ScheduledTaskReadbackJsonPath
$scheduledTaskRegistrationAttemptReadback = Get-ScheduledTaskRegistrationAttemptReadback $ScheduledTaskRegistrationAttemptPath
$scheduledTaskRegistrationAckGate = Get-ScheduledTaskRegistrationAckGateReadback $scheduledTaskRegistrationAttemptReadback $scheduledTaskRegistrationAckState $TaskAckExpiryHours $statusNow
$automationTaskHistoryFreshnessGate = Get-AutomationTaskHistoryFreshnessReadback $automationTaskHistoryAckPairs $scheduledTaskRegistryReadback $TaskHistoryFreshnessHours $statusNow
$automationTaskEvidenceGate = Get-AutomationTaskEvidenceGateReadback $automationTaskEvidenceReadbacks
$automationTaskWatchGate = Get-AutomationTaskWatchGateReadback @($previewRecords) $automationTaskAckGate $scheduledTaskRegistryReadback $scheduledTaskRegistrationAckGate $automationTaskHistoryFreshnessGate $automationTaskEvidenceGate
$automationTaskAckSingleReminder = Get-AckReminderReadback $automationTaskHistoryState $automationTaskAckGateConfigured $automationTaskAckState $TaskAckExpiryHours $statusNow
$automationTaskAckReminder = Get-AutomationTaskAckReminderAggregateReadback $automationTaskHistoryAckPairs $automationTaskAckSingleReminder $automationTaskAckGateConfigured

$databaseHealthStatus = $databaseHealthStatusState.value
$databaseHealthLastRun = Read-LastRunSummary $databaseHealthLastRunState
$largeFileGovernanceStatus = $largeFileGovernanceStatusState.value
$largeFileGovernanceLastRun = Read-LastRunSummary $largeFileGovernanceLastRunState
$automationTaskHistory = $automationTaskHistoryState.value
$automationTaskAck = $automationTaskAckState.value

$e2eReleaseTail = if ($script:GitHubWindowsBuildPolicyResolved -eq "disabled") {
    "Automation status now consumes the persisted rollout observability JSON/Markdown artifact together with repo automation policy and local build/CTest readback; GitHub Windows Build is disabled by repo policy, so the remaining E2E release work is final production-linked release artifact promotion plus local release review."
} else {
    "Automation status now consumes the persisted rollout observability JSON/Markdown artifact together with current GitHub Windows Build visibility and local build/CTest readback; remaining E2E release work is external Windows Build visibility recovery and final production-linked release artifact promotion."
}
$e2eProductionBacklog = @(
    "1. E2E production crypto is the active automation lane again. Linked OpenSSL builds now run the reviewed provider table through public API dispatch. The default status surface keeps the callable manifest, sanitized execution result contract, explicit reviewed runtime-preflight/arming/execution-acceptance probe readiness, and production rotation dry-run/execute evidence, promotes sanitized provider invocation probe evidence through reviewed candidate, call handoff, stub, callable bridge/interface, runtime preflight, arming, execution acceptance, data-plane bridge, and public primitive execution, and linked reviewed builds can pass the early operation, provider control, and explicit reviewed tail probe evidence gates to reach productionAcceptance.accepted=true / releaseGate=production-crypto-accepted."
    "The linked runtime gate now also drives the real public API chain directly for identity generation, public derivation, agreement sign/verify, session derivation, payload encrypt/decrypt, and tamper rejection instead of relying only on probe summaries. Normal provider probe fixtures now use 32-byte valid production material handles, while runtime self-test plus explicit round-trip/public-primitive probes reject malformed identity handles, malformed verification public keys, malformed session-derive keys, and malformed payload keys as invalid-input without hashing arbitrary material into usable keys. productionRolloutObservability now summarizes acceptance, material/export proof counts, public primitive readiness, filesystem object ciphertext readback readiness, operator recovery prompts, user recovery prompts, and no-sensitive-export proof; it stays fail-closed until linked acceptance passes, then reports releaseGate=production-rollout-observability-ready without exporting key/session/private identity/plaintext/ciphertext bytes."
    "e2e_rollout_observability_exporter now persists sanitized rollout observability JSON/Markdown with filesystem object readback and reviewed offline mirror opt-in gates; default CTest verifies the unlinked fail-closed artifact and the linked OpenSSL runtime gate requires accepted evidence before promotion. The runtime gate also verifies client production rotation beyond local rebind: when every provider gate is ready, executeE2EProductionRotation generates production local identity material, persists the production backend id, clears draft sessions/pending agreements without exporting sensitive material, then Alice/Bob/Carol re-announce production identities, re-verify trust pins, derive independent signed production sessions, and send encrypted private text/file payloads on openssl-reviewed-adapter-v1."
    "The same gate restarts all three clients, restores production identities/trust pins from disk, refuses to reuse memory-only sessions, derives fresh independent production sessions, and repeats encrypted text/file delivery. Encrypted private file recovery now has sender-local same-wire cache, verified filesystem object readback, explicit reviewed S3 object readback, and explicit reviewed offline mirror readback paths: auto-resume is allowed only when the local ciphertext cache, filesystem object ciphertext, reviewed S3 ciphertext object, or reviewed offline mirror ciphertext object matches the sanitized envelope header, key id/fingerprint, plaintext/wire hashes, envelope ciphertextSha256, and server resume metadata."
    "S3 remains fail-closed by default; only QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED=1 plus normal S3 configuration enables reviewed HEAD/GET ciphertext readback, and status/resume evidence must not export endpoint, bucket, credentials, ciphertext, session keys, private material, or local plaintext paths. Offline mirror readback is also explicit: only QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED=1 plus QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT enables canonical safe-token ciphertext readback, and status/resume evidence must not export mirror roots, local paths, ciphertext, session keys, or private material. S3/offline without reviewed opt-ins expose only safe object-key-token evidence and fixed not-reviewed gates, legacy URL/path-like object locators are suppressed from recovery status, and cache loss, missing object root, object loss, hash/header/session mismatch, unsafe locator, or unsupported auto-readback fails closed to resend/clear. Offline attachment replay preserves that header so receivers can decrypt queued ciphertext locally. $e2eReleaseTail"
) -join " "

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# QtNetworkChat Automation Status")
$lines.Add("")
$lines.Add('Generated by `scripts/write-automation-status.ps1`. This file is intentionally sanitized; do not add passwords, tokens, GPG passphrases, endpoints with credentials, or local private paths containing secrets.')
$lines.Add("")
$lines.Add("## Current Baseline")
$lines.Add("")
$lines.Add('- Generated at: `' + $generatedAt + '`')
$lines.Add('- HEAD: `' + $Head + '`')
$lines.Add('- HEAD note: `status source/evidence head; the commit containing this generated status file may be newer`')
$lines.Add('- Tracked remote branch: `' + $TrackedRemoteBranch + '`')
$lines.Add('- Tracked remote hash: `' + $TrackedRemoteHash + '`')
$lines.Add('- GitHub Windows Build policy: `' + $script:GitHubWindowsBuildPolicyResolved + '`')
$lines.Add('- GitHub Windows Build: `' + $CiStatus + '`')
$lines.Add('- GitHub run id: `' + $(if ([string]::IsNullOrWhiteSpace($CiRunId)) { "unknown" } else { $CiRunId }) + '`')
$lines.Add('- Local MinGW build: `' + $BuildStatus + '`')
$lines.Add('- Local CTest: `' + $CTestStatus + '`')
$lines.Add('- Local CTest count: `' + $CTestCount + '`')
$lines.Add('- Protected untracked entries: `' + $protectedText + '`')
$lines.Add('- Status readback: `ci=' + (Format-StatusValue $ciReadbackSource) +
    '; build=' + (Format-StatusValue $buildReadbackSource) +
    '; ctest=' + (Format-StatusValue $ctestReadbackSource) + '`')
$lines.Add("")
$lines.Add("## E2E Rollout Observability Readback")
$lines.Add("")
if (-not $e2eRolloutReadback.configured) {
    $lines.Add('- E2E rollout observability: `not configured`')
} elseif ($e2eRolloutReadback.state -ne "ok") {
    $lines.Add(('- E2E rollout observability: status=`{0}`, json=`{1}`, markdown=`{2}`' -f `
            (Format-StatusValue $e2eRolloutReadback.status), `
            (Format-StatusValue $e2eRolloutReadback.jsonArtifact), `
            (Format-StatusValue $e2eRolloutReadback.markdownArtifact)))
} else {
    $lines.Add(('- E2E rollout observability: status=`{0}`, ok=`{1}`, readiness=`{2}`, releaseGate=`{3}`, bundle=`{4}`' -f `
            (Format-StatusValue $e2eRolloutReadback.status), `
            (Format-StatusValue $e2eRolloutReadback.ok), `
            (Format-StatusValue $e2eRolloutReadback.readiness), `
            (Format-StatusValue $e2eRolloutReadback.releaseGate), `
            (Format-StatusValue $e2eRolloutReadback.bundle)))
    $lines.Add(('  CI: status=`{0}`, runId=`{1}`, source=`{2}`; localBuild=`{3}`, localCTest=`{4}`, count=`{5}`' -f `
            (Format-StatusValue $e2eRolloutReadback.ciStatus), `
            (Format-StatusValue $e2eRolloutReadback.ciRunId), `
            (Format-StatusValue $e2eRolloutReadback.ciSource), `
            (Format-StatusValue $e2eRolloutReadback.localBuildStatus), `
            (Format-StatusValue $e2eRolloutReadback.localCTestStatus), `
            (Format-StatusValue $e2eRolloutReadback.localCTestCount)))
    $lines.Add(('  Recovery gates: filesystemReady=`{0}`, filesystemGate=`{1}`, offlineReady=`{2}`, offlineScope=`{3}`, offlineGate=`{4}`' -f `
            (Format-StatusValue $e2eRolloutReadback.filesystemReady), `
            (Format-StatusValue $e2eRolloutReadback.filesystemGate), `
            (Format-StatusValue $e2eRolloutReadback.offlineReady), `
            (Format-StatusValue $e2eRolloutReadback.offlineScope), `
            (Format-StatusValue $e2eRolloutReadback.offlineGate)))
    $lines.Add(('  Offline recovery evidence: action=`{0}`, capturePolicy=`{1}`, noSensitiveExport=`{2}`' -f `
            (Format-StatusValue $e2eRolloutReadback.offlineAction), `
            (Format-StatusValue $e2eRolloutReadback.offlineCapturePolicy), `
            (Format-StatusValue $e2eRolloutReadback.offlineNoSensitiveExportProof)))
    $lines.Add(('  Sensitive export proof: noSensitiveExport=`{0}`, suppressed=`{1}`' -f `
            (Format-StatusValue $e2eRolloutReadback.noSensitiveExportProof), `
            (Format-StatusValue $e2eRolloutReadback.sensitiveFieldsSuppressed)))
}
if (-not $e2eReleaseEvidenceReadback.configured) {
    $lines.Add('- E2E release evidence package: `not configured`')
} elseif ($e2eReleaseEvidenceReadback.state -ne "ok") {
    $lines.Add(('- E2E release evidence package: state=`{0}`, releaseGate=`{1}`, artifact=`{2}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.state), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.releaseGate), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.packageArtifact)))
} else {
    $lines.Add(('- E2E release evidence package: ok=`{0}`, releaseReady=`{1}`, releaseGate=`{2}`, inputs=`{3}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ok), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.releaseReady), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.releaseGate), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.inputCount)))
    $lines.Add(('  Evidence CI/local: ciStatus=`{0}`, ciVisibility=`{1}`, localBuild=`{2}`, localCTest=`{3}`, count=`{4}`, noSensitiveExport=`{5}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciStatus), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciVisibility), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.localBuildStatus), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.localCTestStatus), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.localCTestCount), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.noSensitiveExportProof)))
    $lines.Add(('  Evidence artifact: manifestPackagedAs=`{0}`, manifestEmbedded=`{1}`, packageSha256=`{2}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.manifestPackagedAs), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.manifestEmbedded), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.packageSha256)))
    $lines.Add(('  Production-linked release: ready=`{0}`, releaseGate=`{1}`, blockers=`{2}`, acceptanceBackend=`{3}`, rolloutBackend=`{4}`, releaseRunBackend=`{5}`, operationCountsReady=`{6}`, noSensitiveReady=`{7}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedReady), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedGate), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedBlockers), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedAcceptanceBackend), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedRolloutBackend), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedReleaseRunBackend), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedOperationCountsReady), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.productionLinkedNoSensitiveReady)))
    $lines.Add(('  Promotion decision: promoted=`{0}`, ready=`{1}`, releaseGate=`{2}`, blockers=`{3}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.promotionPromoted), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.promotionReady), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.promotionGate), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.promotionBlockers)))
    $lines.Add(('  Promotion action: `{0}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.promotionOperatorAction)))
    $lines.Add(('  Evidence CI gate: currentHeadObserved=`{0}`, externalBlocker=`{1}`, releaseGate=`{2}`, latestObservedHead=`{3}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciCurrentHeadObserved), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciExternalBlocker), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciReleaseGate), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciLatestObservedHead)))
    $lines.Add(('  Evidence CI head match: targetReleaseHead=`{0}`, ciHead=`{1}`, matches=`{2}`, currentHead=`{3}`, targetMatchesCurrentHead=`{4}`, stale=`{5}`' -f `
            (Format-StatusValue $e2eReleaseEvidenceReadback.targetReleaseHead), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciHeadSha), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.ciHeadMatchesReleaseHead), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.currentHead), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.targetMatchesCurrentHead), `
            (Format-StatusValue $e2eReleaseEvidenceReadback.staleReleaseArtifact)))
}
if ($e2eLinkedReleaseCandidateReadback.configured) {
    if ($script:GitHubWindowsBuildPolicyResolved -eq "disabled") {
        $lines.Add('  Linked runtime candidate: `informational-only while GitHub Windows Build is disabled by policy; current release review follows the main E2E release evidence artifact plus local build/CTest.`')
    } elseif ($e2eLinkedReleaseCandidateReadback.state -ne "ok") {
        $lines.Add(('  Linked runtime candidate: state=`{0}`, releaseGate=`{1}`, artifact=`{2}`' -f `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.state), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseGate), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.packageArtifact)))
    } else {
        $lines.Add(('  Linked runtime candidate: releaseReady=`{0}`, promoted=`{1}`, releaseGate=`{2}`, productionLinked=`{3}`, ci=`{4}/{5}`, local=`{6}/{7}`, blockers=`{8}`, probeFixture=`{9}`, releaseEligible=`{10}`, eligibilityGate=`{11}`' -f `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseReady), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionPromoted), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionGate), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.productionLinkedReady), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.ciStatus), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.ciVisibility), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.localBuildStatus), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.localCTestStatus), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionBlockers), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.probeFixture), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseEligible), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseEligibilityGate)))
        $lines.Add(('  Linked runtime candidate head match: targetReleaseHead=`{0}`, currentHead=`{1}`, targetMatchesCurrentHead=`{2}`, stale=`{3}`' -f `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.targetReleaseHead), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.currentHead), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.targetMatchesCurrentHead), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.staleReleaseArtifact)))
        $linkedPromotionBlockers = [System.Collections.Generic.List[string]]::new()
        foreach ($blocker in @(([string]$e2eLinkedReleaseCandidateReadback.promotionBlockers) -split ",")) {
            $trimmedBlocker = $blocker.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmedBlocker) -and $trimmedBlocker -ne "unknown") {
                $linkedPromotionBlockers.Add($trimmedBlocker)
            }
        }
        $nonCiLinkedPromotionBlockers = @($linkedPromotionBlockers | Where-Object {
            $_ -ne "ci-status-external-visibility-stale" -and $_ -ne "ci-current-head-not-observed"
        })
        $linkedProductionReady =
            (Format-StatusValue $e2eLinkedReleaseCandidateReadback.productionLinkedReady) -eq "true" `
            -and (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseEligible) -eq "true" `
            -and (Format-StatusValue $e2eLinkedReleaseCandidateReadback.probeFixture) -eq "false" `
            -and (Format-StatusValue $e2eLinkedReleaseCandidateReadback.targetMatchesCurrentHead) -eq "true" `
            -and (Format-StatusValue $e2eLinkedReleaseCandidateReadback.staleReleaseArtifact) -eq "false"
        $linkedOnlyCiBlocked = $linkedProductionReady `
            -and $linkedPromotionBlockers.Count -gt 0 `
            -and $nonCiLinkedPromotionBlockers.Count -eq 0
        $finalLinkedPromotionGate = if ((Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionPromoted) -eq "true") {
            "e2e-release-artifact-promoted"
        } elseif ($linkedOnlyCiBlocked) {
            "ci-visibility-informational-only"
        } elseif ($linkedProductionReady) {
            "blocked-final-promotion-review"
        } else {
            "blocked-production-linked-candidate-not-ready"
        }
        $finalLinkedPromotionAction = if ($finalLinkedPromotionGate -eq "ci-visibility-informational-only") {
            "Record GitHub Windows Build visibility lag as informational only; release readiness stays on production-linked evidence and local verification."
        } elseif ($finalLinkedPromotionGate -eq "e2e-release-artifact-promoted") {
            "Archive the promoted production-linked E2E release artifact."
        } else {
            "Regenerate production-linked candidate evidence and resolve non-CI blockers before release promotion."
        }
        $lines.Add(('  Final production-linked promotion gate: productionLinkedReady=`{0}`, releaseEligible=`{1}`, ciOnlyBlocked=`{2}`, releaseGate=`{3}`, action=`{4}`' -f `
                (Format-StatusValue $linkedProductionReady), `
                (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseEligible), `
                (Format-StatusValue $linkedOnlyCiBlocked), `
                $finalLinkedPromotionGate, `
                $finalLinkedPromotionAction))
    }
}
$lines.Add("")
$lines.Add("## Automation Guardrails")
$lines.Add("")
$lines.Add('- Start each loop by checking repository build/test processes and `git status`.')
$lines.Add('- Preserve `.polaris/`, `AGENTS.md`, `CLAUDE.md`, and unrelated user changes.')
$lines.Add("- Use large cross-artifact slices; avoid tiny README-only or one-field patches.")
$lines.Add("- Keep PostgreSQL passwords, GPG passphrases, GitHub tokens, S3 credentials, and signed URLs out of source, docs, logs, previews, launchers, commits, and remote URLs.")
$lines.Add("- Verify with the PowerShell timeout wrappers: build 600 seconds, CTest 900 seconds.")
$lines.Add('- Use signed Conventional Commits and push `main` after verification.')
$lines.Add('- Treat mirror branch pushes as explicit per-run opt-ins; the automation status has no fixed secondary branch target.')
$lines.Add("")
$lines.Add("## Registered Preview Tasks")
$lines.Add("")
if ($previewRecords.Count -eq 0) {
    $lines.Add('- Registered preview tasks: `none`')
} else {
    foreach ($previewRecord in $previewRecords) {
        $schedulerDetail = Find-SchedulerReadbackDetail $scheduledTaskRegistryReadback $previewRecord.taskName $previewRecord.taskKind
        $lines.Add('- Preview task: ' + (Format-PreviewDescriptor $previewRecord $schedulerDetail))
        if ($previewRecord.taskSummary -ne "unknown") {
            $lines.Add('  Summary: `' + $previewRecord.taskSummary + '`')
        }
    }
}
if ($automationTaskWatchGate.configured) {
    $lines.Add(('- Automation watch gate: state=`{0}`, tasks=`{1}`, registered=`{2}`, previewOnly=`{3}`, invalid=`{4}`, releaseGate=`{5}`, action=`{6}`' -f `
            (Format-StatusValue $automationTaskWatchGate.state), `
            (Format-StatusValue $automationTaskWatchGate.taskCount), `
            (Format-StatusValue $automationTaskWatchGate.registeredCount), `
            (Format-StatusValue $automationTaskWatchGate.previewOnlyCount), `
            (Format-StatusValue $automationTaskWatchGate.invalidPreviewCount), `
            (Format-StatusValue $automationTaskWatchGate.releaseGate), `
            (Format-StatusValue $automationTaskWatchGate.action)))
}
if ($automationTaskEvidenceGate.configured) {
    $lines.Add(('- Task evidence gate: state=`{0}`, tasks=`{1}`, previewEvidence=`{2}`, liveEvidence=`{3}`, missingEvidence=`{4}`, releaseGate=`{5}`, action=`{6}`' -f `
            (Format-StatusValue $automationTaskEvidenceGate.state), `
            (Format-StatusValue $automationTaskEvidenceGate.taskCount), `
            (Format-StatusValue $automationTaskEvidenceGate.previewEvidenceCount), `
            (Format-StatusValue $automationTaskEvidenceGate.liveEvidenceCount), `
            (Format-StatusValue $automationTaskEvidenceGate.missingEvidenceCount), `
            (Format-StatusValue $automationTaskEvidenceGate.releaseGate), `
            (Format-StatusValue $automationTaskEvidenceGate.action)))
    foreach ($evidenceDetail in @($automationTaskEvidenceGate.details)) {
        $lines.Add(('  Task evidence: kind=`{0}`, name=`{1}`, state=`{2}`, status=`{3}`, readiness=`{4}`, releaseGate=`{5}`' -f `
                (Format-StatusValue $evidenceDetail.taskKind), `
                (Format-StatusValue $evidenceDetail.taskName), `
                (Format-StatusValue $evidenceDetail.state), `
                (Format-StatusValue $evidenceDetail.status), `
                (Format-StatusValue $evidenceDetail.readiness), `
                (Format-StatusValue $evidenceDetail.releaseGate)))
    }
}
if ($scheduledTaskRegistryReadback.configured) {
    $lines.Add(('- Scheduled task registry readback: state=`{0}`, tasks=`{1}`, expectedRegistered=`{2}`, found=`{3}`, missing=`{4}`, registrationFailed=`{5}`, previewOnly=`{6}`, unreadable=`{7}`, source=`{8}`, releaseGate=`{9}`, action=`{10}`' -f `
            (Format-StatusValue $scheduledTaskRegistryReadback.state), `
            (Format-StatusValue $scheduledTaskRegistryReadback.taskCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.expectedRegisteredCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.registeredFoundCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.registeredMissingCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.registrationFailedCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.previewOnlyCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.unreadableCount), `
            (Format-StatusValue $scheduledTaskRegistryReadback.source), `
            (Format-StatusValue $scheduledTaskRegistryReadback.releaseGate), `
            (Format-StatusValue $scheduledTaskRegistryReadback.action)))
    foreach ($taskReadback in @($scheduledTaskRegistryReadback.details)) {
        $lines.Add(('  Scheduler task: kind=`{0}`, name=`{1}`, expectedRegistered=`{2}`, readback=`{3}`, schedulerState=`{4}`, taskPath=`{5}`, source=`{6}`' -f `
                (Format-StatusValue $taskReadback.taskKind), `
                (Format-StatusValue $taskReadback.taskName), `
                (Format-StatusValue $taskReadback.expectedRegistered), `
                (Format-StatusValue $taskReadback.readback), `
                (Format-StatusValue $taskReadback.schedulerState), `
                (Format-StatusValue $taskReadback.taskPath), `
                (Format-StatusValue $taskReadback.source)))
    }
}
if ($scheduledTaskRegistrationAttemptReadback.configured) {
    $lines.Add(('- Scheduled task registration attempt: state=`{0}`, requested=`{1}`, user=`{2}`, tasks=`{3}`, failed=`{4}`, releaseGate=`{5}`, action=`{6}`' -f `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.state), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.registrationRequested), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.user), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.taskCount), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.failedCount), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.releaseGate), `
            (Format-StatusValue $scheduledTaskRegistrationAttemptReadback.action)))
    foreach ($registrationAttempt in @($scheduledTaskRegistrationAttemptReadback.details)) {
        $lines.Add(('  Registration attempt task: kind=`{0}`, name=`{1}`, requested=`{2}`, status=`{3}`, exitCode=`{4}`, failureClass=`{5}`, outputLines=`{6}`' -f `
                (Format-StatusValue $registrationAttempt.taskKind), `
                (Format-StatusValue $registrationAttempt.taskName), `
                (Format-StatusValue $registrationAttempt.registrationRequested), `
                (Format-StatusValue $registrationAttempt.status), `
                (Format-StatusValue $registrationAttempt.exitCode), `
                (Format-StatusValue $registrationAttempt.failureClass), `
                (Format-StatusValue $registrationAttempt.outputLineCount)))
    }
}
if ($scheduledTaskRegistrationAckState.configured) {
    if ($scheduledTaskRegistrationAckState.state -eq "ok") {
        $lines.Add(('- Scheduled task registration acknowledgement: acknowledged=`{0}`, by=`{1}`, at=`{2}`, reason=`{3}`' -f `
                (Format-StatusValue (Get-JsonValue $scheduledTaskRegistrationAckState.value "acknowledged" $null)), `
                (Format-StatusValue (Get-JsonValue $scheduledTaskRegistrationAckState.value "acknowledgedBy" "unknown")), `
                (Format-StatusValue (Get-JsonValue $scheduledTaskRegistrationAckState.value "acknowledgedAt" "unknown")), `
                (Format-StatusValue (Get-JsonValue $scheduledTaskRegistrationAckState.value "reason" "unknown"))))
    } else {
        $lines.Add(('- Scheduled task registration acknowledgement: state=`{0}`' -f `
                (Format-StatusValue $scheduledTaskRegistrationAckState.state)))
    }
}
if ($scheduledTaskRegistrationAckGate.configured) {
    $lines.Add(('- Scheduled task registration ack gate: state=`{0}`, failed=`{1}`, acknowledged=`{2}`, ackExpired=`{3}`, ageHours=`{4}`, remainingHours=`{5}`, overdueHours=`{6}`, expiresAt=`{7}`, releaseGate=`{8}`, action=`{9}`' -f `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.state), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.failedCount), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.acknowledged), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.ackExpired), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.ageHours), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.remainingHours), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.overdueHours), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.expiresAt), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.releaseGate), `
            (Format-StatusValue $scheduledTaskRegistrationAckGate.action)))
}
if ($automationTaskHistoryFreshnessGate.configured) {
    $lines.Add(('- Task history freshness gate: state=`{0}`, tasks=`{1}`, fresh=`{2}`, stale=`{3}`, unavailable=`{4}`, unparseable=`{5}`, thresholdHours=`{6}`, releaseGate=`{7}`, action=`{8}`' -f `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.state), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.taskCount), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.freshTaskCount), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.staleTaskCount), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.unavailableTaskCount), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.unparseableTaskCount), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.thresholdHours), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.releaseGate), `
            (Format-StatusValue $automationTaskHistoryFreshnessGate.action)))
    foreach ($freshnessDetail in @($automationTaskHistoryFreshnessGate.details)) {
        $lines.Add(('  Task history freshness: kind=`{0}`, name=`{1}`, state=`{2}`, latestAt=`{3}`, ageHours=`{4}`, thresholdHours=`{5}`, releaseGate=`{6}`' -f `
                (Format-StatusValue $freshnessDetail.taskKind), `
                (Format-StatusValue $freshnessDetail.taskName), `
                (Format-StatusValue $freshnessDetail.state), `
                (Format-StatusValue $freshnessDetail.latestAt), `
                (Format-StatusValue $freshnessDetail.ageHours), `
                (Format-StatusValue $freshnessDetail.thresholdHours), `
                (Format-StatusValue $freshnessDetail.releaseGate)))
    }
}
$lines.Add("")
$lines.Add("## Generic Task Readback")
$lines.Add("")
if ($genericPreviewCandidates.Count -eq 0) {
    if ($previewRecords.Count -gt 0) {
        $lines.Add('- Generic task readback: `typed task readback active; no unclassified generic tasks`')
    } else {
        $lines.Add('- Generic task readback: `none`')
    }
} else {
    foreach ($genericPreview in $genericPreviewCandidates) {
        $genericReadback = Get-GenericTaskReadback $genericPreview
        $genericLine = '- Generic task: kind=`{0}`, name=`{1}`, display=`{2}`, status=`{3}`, lastRun=`{4}`, history=`{5}`, ack=`{6}`' -f `
            (Format-StatusValue $genericPreview.taskKind), `
            (Format-StatusValue $genericPreview.taskName), `
            (Format-StatusValue $genericPreview.taskDisplayName), `
            (Format-StatusValue $genericReadback.statusSummary), `
            (Format-StatusValue $genericReadback.lastRunExitCode), `
            (Format-StatusValue $genericReadback.historySummary), `
            (Format-StatusValue $genericReadback.ackSummary)
        $lines.Add($genericLine)
        if ($genericReadback.readinessSummary -ne "unknown" -or $genericReadback.releaseGateSummary -ne "unknown" -or $genericReadback.operatorActionSummary -ne "unknown" -or $genericReadback.auditFocusSummary -ne "unknown") {
            $lines.Add(('  Gate: readiness=`{0}`, releaseGate=`{1}`, action=`{2}`, auditFocus=`{3}`' -f `
                    (Format-StatusValue $genericReadback.readinessSummary), `
                    (Format-StatusValue $genericReadback.releaseGateSummary), `
                    (Format-StatusValue $genericReadback.operatorActionSummary), `
                    (Format-StatusValue $genericReadback.auditFocusSummary)))
        }
        if ($genericReadback.releaseDetailsSummary -ne "unknown") {
            $lines.Add('  Release details: `' + $genericReadback.releaseDetailsSummary + '`')
        }
        if ($genericPreview.taskSummary -ne "unknown") {
            $lines.Add('  Summary: `' + $genericPreview.taskSummary + '`')
        }
    }
}
$lines.Add("")
$lines.Add("## PostgreSQL Release Acceptance Readback")
$lines.Add("")
if ($pgsqlReleaseAcceptancePreviewCandidates.Count -eq 0) {
    $lines.Add('- PostgreSQL release acceptance: `not configured`')
} else {
    foreach ($readback in $pgsqlReleaseAcceptanceReadbacks) {
        $preview = $readback.previewRecord
        $evidenceConfig = Get-PreviewArtifactConfiguration $preview.state @("evidencePackagePath") "evidence"
        $evidenceState = Get-ArtifactState -PathValue $evidenceConfig.path
        $evidenceSummary = if ($evidenceState.state -eq "ok") { "ok" } else { $evidenceState.state }
        $lines.Add(('- PostgreSQL release acceptance: name=`{0}`, status=`{1}`, lastRun=`{2}`, history=`{3}`, ack=`{4}`, evidence=`{5}`' -f `
                (Format-StatusValue $preview.taskName), `
                (Format-StatusValue $readback.statusSummary), `
                (Format-StatusValue $readback.lastRunExitCode), `
                (Format-StatusValue $readback.historySummary), `
                (Format-StatusValue $readback.ackSummary), `
                (Format-StatusValue $evidenceSummary)))
        $lines.Add(('  Gate: readiness=`{0}`, releaseGate=`{1}`, action=`{2}`, auditFocus=`{3}`' -f `
                (Format-StatusValue $readback.readinessSummary), `
                (Format-StatusValue $readback.releaseGateSummary), `
                (Format-StatusValue $readback.operatorActionSummary), `
                (Format-StatusValue $readback.auditFocusSummary)))
        if ($readback.releaseDetailsSummary -ne "unknown") {
            $lines.Add('  Release details: `' + $readback.releaseDetailsSummary + '`')
        }
    }
}
$lines.Add("")
$lines.Add("## Scheduled Task Readback")
$lines.Add("")
if ($databaseHealthStatusState.state -ne "ok") {
    if (Has-ConfigurationHint @($DatabaseHealthStatusPath, $DatabaseHealthLastRunPath, $DatabaseHealthTaskPreviewPath, $normalizedTaskPreviewPaths)) {
        $lines.Add('- Database health: `configured but status artifact unavailable`')
    } else {
        $lines.Add('- Database health: `not configured`')
    }
} else {
    $dbQueryMetrics = Get-JsonValue $databaseHealthStatus "queryMetrics" $null
    $dbFailedChecks = @((Get-JsonValue $databaseHealthStatus "failedChecks" @()))
    $dbPlanOnly = Get-JsonValue $databaseHealthStatus "planOnly" $null
    $dbSummary = Get-JsonValue $databaseHealthStatus "summary" $null
    $dbAuditSummary = Get-JsonValue $databaseHealthStatus "auditSummary" $null
    $dbAuditFocus = @((Get-JsonValue $dbAuditSummary "auditFocus" @()))
    $lines.Add(('- Database health: status=`{0}`, ok=`{1}`, driver=`{2}`, checks=`{3}`, failedChecks=`{4}`, slowQueries=`{5}`, queryFailures=`{6}`' -f
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "status" "unknown")),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "ok" $null)),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "driver" "unknown")),
            (Format-StatusValue (Get-JsonValue $databaseHealthStatus "checkCount" "unknown")),
            $dbFailedChecks.Count,
            (Format-StatusValue (Get-JsonValue $dbQueryMetrics "slowQueryCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $dbQueryMetrics "queryFailureCount" "unknown"))))
    if ($null -ne $dbPlanOnly) {
        $lines.Add(('  Evidence mode: planOnly=`{0}`' -f (Format-StatusValue $dbPlanOnly)))
    }
    $lines.Add(('  Gate: readiness=`{0}`, releaseGate=`{1}`, action=`{2}`, auditFocus=`{3}`' -f
            (Format-StatusValue (Get-JsonValue $dbSummary "readiness" "unknown")),
            (Format-StatusValue (Get-JsonValue $dbAuditSummary "releaseGate" "unknown")),
            (Format-StatusValue (Get-JsonValue $dbSummary "operatorAction" "unknown")),
            (Format-StatusValue ($(if ($dbAuditFocus.Count -gt 0) { $dbAuditFocus -join ", " } else { "none" })))))
}
if ($null -ne $databaseHealthLastRun) {
    $lines.Add(('- Database health last run: at=`{0}`, exitCode=`{1}`' -f
            (Format-StatusValue $databaseHealthLastRun.timestamp),
            (Format-StatusValue $databaseHealthLastRun.exitCode)))
}
if ($largeFileGovernanceStatusState.state -ne "ok") {
    if (Has-ConfigurationHint @($LargeFileGovernanceStatusPath, $LargeFileGovernanceLastRunPath, $LargeFileGovernanceTaskPreviewPath, $normalizedTaskPreviewPaths)) {
        $lines.Add('- Large-file governance: `configured but status artifact unavailable`')
    } else {
        $lines.Add('- Large-file governance: `not configured`')
    }
} else {
    $governanceGapAreas = @((Get-JsonValue $largeFileGovernanceStatus "s3CoverageActionableGapAreas" @()))
    $governanceSummary = Get-JsonValue $largeFileGovernanceStatus "summary" $null
    $governanceAuditSummary = Get-JsonValue $largeFileGovernanceStatus "auditSummary" $null
    $governanceAuditFocus = @((Get-JsonValue $governanceAuditSummary "auditFocus" @()))
    $lines.Add(('- Large-file governance: status=`{0}`, ok=`{1}`, warnings=`{2}`, alerts=`{3}`, actionableS3Gaps=`{4}`' -f
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "status" "unknown")),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "ok" $null)),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "totalWarnings" "unknown")),
            (Format-StatusValue (Get-JsonValue $largeFileGovernanceStatus "alertCount" "unknown")),
            $governanceGapAreas.Count))
    $lines.Add(('  Gate: readiness=`{0}`, releaseGate=`{1}`, action=`{2}`, auditFocus=`{3}`' -f
            (Format-StatusValue (Get-JsonValue $governanceSummary "readiness" "unknown")),
            (Format-StatusValue (Get-JsonValue $governanceAuditSummary "releaseGate" "unknown")),
            (Format-StatusValue (Get-JsonValue $governanceSummary "operatorAction" "unknown")),
            (Format-StatusValue ($(if ($governanceAuditFocus.Count -gt 0) { $governanceAuditFocus -join ", " } else { "none" })))))
}
if ($null -ne $largeFileGovernanceLastRun) {
    $lines.Add(('- Large-file governance last run: at=`{0}`, exitCode=`{1}`' -f
            (Format-StatusValue $largeFileGovernanceLastRun.timestamp),
            (Format-StatusValue $largeFileGovernanceLastRun.exitCode)))
}
if ($automationTaskHistoryState.state -ne "ok") {
    if (Has-ConfigurationHint @($AutomationTaskHistoryPath, $AutomationTaskAckPath, $DatabaseHealthTaskPreviewPath, $LargeFileGovernanceTaskPreviewPath, $normalizedTaskPreviewPaths)) {
        $lines.Add('- Task history: `configured but history artifact unavailable`')
    } else {
        $lines.Add('- Task history: `not configured`')
    }
} else {
    $historyLatestRun = Get-JsonValue $automationTaskHistory "latestRun" $null
    $lines.Add(('- Task history: runs=`{0}`, failed=`{1}`, latestAt=`{2}`, latestExitCode=`{3}`, acknowledged=`{4}`, ackExpired=`{5}`' -f
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "runCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "failedRunCount" "unknown")),
            (Format-StatusValue (Get-JsonValue $historyLatestRun "timestamp" "unknown")),
            (Format-StatusValue (Get-JsonValue $historyLatestRun "exitCode" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "acknowledged" $null)),
            (Format-StatusValue (Get-JsonValue $automationTaskHistory "ackExpired" $null))))
}
if ($automationTaskAckState.state -ne "ok") {
    if (Has-ConfigurationHint @($AutomationTaskAckPath, $DatabaseHealthTaskPreviewPath, $LargeFileGovernanceTaskPreviewPath, $normalizedTaskPreviewPaths)) {
        $lines.Add('- Task acknowledgement: `configured but ack artifact unavailable`')
    }
} else {
    $lines.Add(('- Task acknowledgement: acknowledged=`{0}`, by=`{1}`, at=`{2}`, reason=`{3}`' -f
            (Format-StatusValue (Get-JsonValue $automationTaskAck "acknowledged" $null)),
            (Format-StatusValue (Get-JsonValue $automationTaskAck "acknowledgedBy" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskAck "acknowledgedAt" "unknown")),
            (Format-StatusValue (Get-JsonValue $automationTaskAck "reason" "unknown"))))
}
if ($automationTaskAckGate.configured) {
    $lines.Add(('- Task acknowledgement aggregate: acknowledged=`{0}`, failed=`{1}`, blocked=`{2}`, source=`{3}`, releaseGate=`{4}`' -f
            (Format-StatusValue $automationTaskAckGate.acknowledged),
            (Format-StatusValue $automationTaskAckGate.failedRunCount),
            (Format-StatusValue $automationTaskAckGate.blockedTaskCount),
            (Format-StatusValue $automationTaskAckGate.source),
            (Format-StatusValue $automationTaskAckGate.releaseGate)))
    $lines.Add(('- Task acknowledgement gate: state=`{0}`, failed=`{1}`, acknowledged=`{2}`, ackExpired=`{3}`, tasks=`{4}`, blocked=`{5}`, source=`{6}`, releaseGate=`{7}`, action=`{8}`' -f
            (Format-StatusValue $automationTaskAckGate.state),
            (Format-StatusValue $automationTaskAckGate.failedRunCount),
            (Format-StatusValue $automationTaskAckGate.acknowledged),
            (Format-StatusValue $automationTaskAckGate.ackExpired),
            (Format-StatusValue $automationTaskAckGate.taskCount),
            (Format-StatusValue $automationTaskAckGate.blockedTaskCount),
            (Format-StatusValue $automationTaskAckGate.source),
            (Format-StatusValue $automationTaskAckGate.releaseGate),
            (Format-StatusValue $automationTaskAckGate.action)))
    foreach ($ackDetail in @($automationTaskAckGate.details)) {
        $lines.Add(('  Task ack gate: kind=`{0}`, name=`{1}`, state=`{2}`, failed=`{3}`, acknowledged=`{4}`, ackExpired=`{5}`, releaseGate=`{6}`' -f
                (Format-StatusValue $ackDetail.taskKind),
                (Format-StatusValue $ackDetail.taskName),
                (Format-StatusValue $ackDetail.state),
                (Format-StatusValue $ackDetail.failedRunCount),
                (Format-StatusValue $ackDetail.acknowledged),
                (Format-StatusValue $ackDetail.ackExpired),
                (Format-StatusValue $ackDetail.releaseGate)))
    }
}
if ($automationTaskAckReminder.configured) {
    $lines.Add(('- Task acknowledgement reminder: state=`{0}`, expiryHours=`{1}`, ageHours=`{2}`, remainingHours=`{3}`, overdueHours=`{4}`, expiresAt=`{5}`, action=`{6}`' -f
            (Format-StatusValue $automationTaskAckReminder.state),
            (Format-StatusValue $automationTaskAckReminder.expiryHours),
            (Format-StatusValue $automationTaskAckReminder.ageHours),
            (Format-StatusValue $automationTaskAckReminder.remainingHours),
            (Format-StatusValue $automationTaskAckReminder.overdueHours),
            (Format-StatusValue $automationTaskAckReminder.expiresAt),
            (Format-StatusValue $automationTaskAckReminder.action)))
}
if ($automationAckDrillState.configured) {
    if ($automationAckDrillState.state -eq "ok" -and
            (Get-JsonValue $automationAckDrillState.value "format" "") -eq "qtnetworkchat-automation-ack-drill-v1") {
        $lines.Add(('- Task acknowledgement drill: state=`{0}`, ok=`{1}`, failed=`{2}`, acknowledged=`{3}`, ackExpired=`{4}`, releaseGate=`{5}`, liveTaskMutation=`{6}`, action=`{7}`' -f
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "state" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "ok" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "failedRunCount" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "acknowledged" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "ackExpired" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "releaseGate" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "liveTaskMutation" "unknown")),
                (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "operatorAction" "unknown"))))
    } else {
        $lines.Add(('- Task acknowledgement drill: state=`{0}`, releaseGate=`automation-ack-drill-unavailable`' -f
                (Format-StatusValue $automationAckDrillState.state)))
    }
}
$lines.Add("")
$lines.Add("## S3 Real Backend Readiness")
$lines.Add("")
if (-not $s3RealBackendReadinessReadback.configured) {
    $lines.Add('- S3 real backend readiness: `not configured`')
} elseif ($s3RealBackendReadinessReadback.state -ne "ok" -and $s3RealBackendReadinessReadback.state -ne "invalid-format") {
    $lines.Add(('- S3 real backend readiness: state=`{0}`, releaseGate=`{1}`' -f
            (Format-StatusValue $s3RealBackendReadinessReadback.state),
            (Format-StatusValue $s3RealBackendReadinessReadback.releaseGate)))
} else {
    $lines.Add(('- S3 real backend readiness: status=`{0}`, ok=`{1}`, configured=`{2}`, explicitEnabled=`{3}`, readiness=`{4}`, releaseGate=`{5}`' -f
            (Format-StatusValue $s3RealBackendReadinessReadback.status),
            (Format-StatusValue $s3RealBackendReadinessReadback.ok),
            (Format-StatusValue $s3RealBackendReadinessReadback.configuredFlag),
            (Format-StatusValue $s3RealBackendReadinessReadback.explicitEnabled),
            (Format-StatusValue $s3RealBackendReadinessReadback.readiness),
            (Format-StatusValue $s3RealBackendReadinessReadback.releaseGate)))
    $lines.Add(('  Evidence: s3Lines=`{0}`, success=`{1}`, fixedFailureReasons=`{2}`, sensitiveHits=`{3}`, defaultCTestMode=`{4}`, realBackendDefaultCI=`{5}`, defaultCIGate=`{6}`' -f
            (Format-StatusValue $s3RealBackendReadinessReadback.s3LineCount),
            (Format-StatusValue $s3RealBackendReadinessReadback.successCount),
            (Format-StatusValue $s3RealBackendReadinessReadback.fixedFailureReasonCount),
            (Format-StatusValue $s3RealBackendReadinessReadback.sensitiveHitCount),
            (Format-StatusValue $s3RealBackendReadinessReadback.defaultCTestMode),
            (Format-StatusValue $s3RealBackendReadinessReadback.realBackendDefaultCI),
            (Format-StatusValue $s3RealBackendReadinessReadback.defaultCIReleaseGate)))
    $lines.Add(('  Action: `{0}`' -f
            (Format-StatusValue $s3RealBackendReadinessReadback.operatorAction)))
}
$lines.Add("")
$lines.Add("## Artifact Diagnostics")
$lines.Add("")
$databaseHealthDiagnostics = @(
    (Get-ArtifactIssueText "preview" $databaseHealthPreview $null $null),
    (Get-ArtifactIssueText "status" $databaseHealthStatusState $databaseHealthStatusPreviewState $databaseHealthStatusConfig),
    (Get-ArtifactIssueText "lastRun" $databaseHealthLastRunState $databaseHealthLastRunPreviewState $databaseHealthLastRunConfig)
) -join "; "
$largeFileGovernanceDiagnostics = @(
    (Get-ArtifactIssueText "preview" $largeFileGovernancePreview $null $null),
    (Get-ArtifactIssueText "status" $largeFileGovernanceStatusState $largeFileGovernanceStatusPreviewState $largeFileGovernanceStatusConfig),
    (Get-ArtifactIssueText "lastRun" $largeFileGovernanceLastRunState $largeFileGovernanceLastRunPreviewState $largeFileGovernanceLastRunConfig)
) -join "; "
$automationHistoryDiagnostics = @(
    (Get-ArtifactIssueText "history" $automationTaskHistoryState $automationTaskHistoryPreviewState $automationTaskHistoryConfig),
    (Get-ArtifactIssueText "ack" $automationTaskAckState $automationTaskAckPreviewState $automationTaskAckConfig),
    (Get-ArtifactIssueText "registrationAck" $scheduledTaskRegistrationAckState $null $null)
) -join "; "
$automationAckDrillDiagnostics = if ($automationAckDrillState.configured) {
    if ($automationAckDrillState.state -eq "ok") {
        @(
            ('state={0}' -f (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "state" "unknown"))),
            ('ok={0}' -f (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "ok" "unknown"))),
            ('acknowledged={0}' -f (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "acknowledged" "unknown"))),
            ('releaseGate={0}' -f (Format-StatusValue (Get-JsonValue $automationAckDrillState.value "releaseGate" "unknown")))
        ) -join "; "
    } else {
        ('state={0}' -f (Format-StatusValue $automationAckDrillState.state))
    }
} else {
    ""
}
$e2eRolloutDiagnostics = @(
    ('json={0}' -f (Format-StatusValue $e2eRolloutReadback.jsonArtifact)),
    ('markdown={0}' -f (Format-StatusValue $e2eRolloutReadback.markdownArtifact)),
    ('bundle={0}' -f (Format-StatusValue $e2eRolloutReadback.bundle))
) -join "; "
$e2eReleaseEvidenceDiagnostics = @(
    ('manifest={0}' -f (Format-StatusValue $e2eReleaseEvidenceReadback.packageArtifact)),
    ('manifestEmbedded={0}' -f (Format-StatusValue $e2eReleaseEvidenceReadback.manifestEmbedded)),
    ('packageSha256={0}' -f (Format-StatusValue $e2eReleaseEvidenceReadback.packageSha256)),
    ('releaseGate={0}' -f (Format-StatusValue $e2eReleaseEvidenceReadback.releaseGate))
) -join "; "
$e2eLinkedReleaseCandidateDiagnostics = @(
    ('manifest={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.packageArtifact)),
    ('releaseReady={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseReady)),
    ('promoted={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionPromoted)),
    ('releaseGate={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.promotionGate)),
    ('targetMatchesCurrentHead={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.targetMatchesCurrentHead)),
    ('stale={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.staleReleaseArtifact)),
    ('probeFixture={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.probeFixture)),
    ('releaseEligible={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.releaseEligible)),
    ('packageSha256={0}' -f (Format-StatusValue $e2eLinkedReleaseCandidateReadback.packageSha256))
) -join "; "
$s3RealBackendReadinessDiagnostics = @(
    ('readiness={0}' -f (Format-StatusValue $s3RealBackendReadinessState.state)),
    ('status={0}' -f (Format-StatusValue $s3RealBackendReadinessReadback.status)),
    ('releaseGate={0}' -f (Format-StatusValue $s3RealBackendReadinessReadback.releaseGate)),
    ('defaultCI={0}' -f (Format-StatusValue $s3RealBackendReadinessReadback.realBackendDefaultCI)),
    ('defaultCIGate={0}' -f (Format-StatusValue $s3RealBackendReadinessReadback.defaultCIReleaseGate))
) -join "; "
$lines.Add('- Database health artifacts: `' + $databaseHealthDiagnostics + '`')
$lines.Add('- Large-file governance artifacts: `' + $largeFileGovernanceDiagnostics + '`')
$lines.Add('- S3 real backend readiness artifacts: `' + $s3RealBackendReadinessDiagnostics + '`')
$lines.Add('- Automation history artifacts: `' + $automationHistoryDiagnostics + '`')
if ($automationAckDrillState.configured) {
    $lines.Add('- Automation ack drill artifacts: `' + $automationAckDrillDiagnostics + '`')
}
$lines.Add('- E2E rollout observability artifacts: `' + $e2eRolloutDiagnostics + '`')
$lines.Add('- E2E release evidence artifacts: `' + $e2eReleaseEvidenceDiagnostics + '`')
if ($e2eLinkedReleaseCandidateReadback.configured) {
    $lines.Add('- E2E linked release candidate artifacts: `' + $e2eLinkedReleaseCandidateDiagnostics + '`')
}
$lines.Add("")
$lines.Add("## Priority Backlog")
$lines.Add("")
$lines.Add($e2eProductionBacklog)
$lines.Add("2. Group productization is closed for the current automation lane: private group creation/invitation/removal, private scoped messages/files, non-member and removed-member fail-closed behavior, snapshot permission fields, removed-member read-only history markers, history visibility policy fields, and send/reject audit evidence are implemented.")
$lines.Add("3. README information architecture is closed for now: keep README as the quick-start/index surface, keep testing coverage, PostgreSQL operations, large-file governance, and E2E hardening status in focused docs, and keep CTest/automation-status references pointed at those docs so long paragraphs do not return.")
$lines.Add("4. Mainwindow structure split is no longer the active lane but remains partially complete: HistoryService, TransferManager, FriendManager, GroupManager, ClientStorage, LocalFileManager, ChatContextManager, and NotificationPanelManager are extracted with focused CTest coverage; continue only after the current E2E production crypto lane or if explicitly redirected.")
$lines.Add("5. PostgreSQL productization is closed for the current automation mainline: QPSQL smoke boundary evidence and rollback live evidence are both covered. Only fix PostgreSQL regressions or CI failures; do not keep adding PostgreSQL polish before the requested README/group/mainwindow work.")
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
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($target, ($lines -join [Environment]::NewLine), $utf8NoBom)
    Write-Host ("automation status: {0}" -f $target)
} else {
    Write-Output ($lines -join [Environment]::NewLine)
}

exit 0


