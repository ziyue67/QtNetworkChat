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
    [string]$GitHubWindowsBuildStatusPath,
    [string]$GitHubRunListJsonPath,
    [string]$E2ERolloutObservabilityJsonPath,
    [string]$E2ERolloutObservabilityMarkdownPath,
    [string]$DatabaseHealthStatusPath,
    [string]$DatabaseHealthLastRunPath,
    [string]$DatabaseHealthTaskPreviewPath,
    [string]$LargeFileGovernanceStatusPath,
    [string]$LargeFileGovernanceLastRunPath,
    [string]$LargeFileGovernanceTaskPreviewPath,
    [string[]]$TaskPreviewPath = @(),
    [string]$AutomationTaskHistoryPath,
    [string]$AutomationTaskAckPath,
    [switch]$BootstrapDefaultTasks,
    [string]$DefaultTaskOutputDir = "build-qt6-mingw\automation-tasks",
    [int]$TaskAckExpiryHours = 72,
    [int]$TaskHistoryRetentionCount = 30,
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
        offlineGate = "unknown"
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
    $result.offlineGate =
        Format-StatusValue (Get-JsonValue $summary "offlineObjectRecoveryReleaseGate" "unknown")
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
    if ($statusState.state -eq "ok") {
        $statusValue = Get-JsonValue $statusState.value "status" ""
        $okValue = Get-JsonValue $statusState.value "ok" $null
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
        lastRunExitCode = $lastRunExitCode
        historySummary = $historySummary
        ackSummary = $ackSummary
    }
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

    $acknowledgedRaw = Get-JsonValue $HistoryState.value "acknowledged" $null
    if ($null -eq $acknowledgedRaw -and $null -ne $AckState -and $AckState.state -eq "ok") {
        $acknowledgedRaw = Get-JsonValue $AckState.value "acknowledged" $null
    }
    $ackExpiredRaw = Get-JsonValue $HistoryState.value "ackExpired" $null

    $acknowledged = Convert-StatusBoolean $acknowledgedRaw $false
    $ackExpired = Convert-StatusBoolean $ackExpiredRaw $false
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

function Format-PreviewDescriptor([object]$PreviewRecord) {
    $parts = New-Object System.Collections.Generic.List[string]
    $parts.Add(('label=`{0}`' -f (Format-StatusValue $PreviewRecord.label)))
    $parts.Add(('kind=`{0}`' -f (Format-StatusValue $PreviewRecord.taskKind)))
    $parts.Add(('name=`{0}`' -f (Format-StatusValue $PreviewRecord.taskName)))
    $parts.Add(('display=`{0}`' -f (Format-StatusValue $PreviewRecord.taskDisplayName)))
    $parts.Add(('state=`{0}`' -f (Format-StatusValue $PreviewRecord.state.state)))
    $parts.Add(('format=`{0}`' -f (Format-StatusValue $PreviewRecord.previewFormat)))
    $parts.Add(('readOnly=`{0}`' -f (Format-StatusValue $PreviewRecord.readOnly)))
    $parts.Add(('register=`{0}`' -f (Format-StatusValue $PreviewRecord.register)))
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
    if (-not $script:PlanOnly.IsPresent) {
        $bootstrapArguments = @(
            "-ExecutionPolicy", "Bypass",
            "-File", $bootstrapScript,
            "-OutputDir", $bootstrapOutputDir,
            "-AckExpiryHours", $script:TaskAckExpiryHours,
            "-HistoryRetentionCount", $script:TaskHistoryRetentionCount
        )
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
if ([string]::IsNullOrWhiteSpace($GitHubWindowsBuildStatusPath)) {
    $GitHubWindowsBuildStatusPath = Join-Path $BuildDir "github-windows-build-status.json"
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityJsonPath)) {
    $E2ERolloutObservabilityJsonPath = Join-Path $BuildDir "e2e_rollout_observability_evidence\e2e-rollout-observability.json"
}
if ([string]::IsNullOrWhiteSpace($E2ERolloutObservabilityMarkdownPath)) {
    $E2ERolloutObservabilityMarkdownPath = Join-Path $BuildDir "e2e_rollout_observability_evidence\e2e-rollout-observability.md"
}
$localVerificationReadback = Get-LocalVerificationStatusReadback $LocalVerificationStatusPath

$ciReadbackSource = if (Is-UnknownStatus $CiStatus) { "auto" } else { "parameter" }
if (Is-UnknownStatus $CiStatus) {
    $ciReadback = Get-GitHubWindowsBuildReadback $Head
    $CiStatus = $ciReadback.status
    if ([string]::IsNullOrWhiteSpace($CiRunId)) {
        $CiRunId = $ciReadback.runId
    }
    $ciReadbackSource = $ciReadback.source
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
$generatedAt = (Get-Date).ToUniversalTime().ToString("o")
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
$pgsqlReleaseAcceptanceReadbacks = @($pgsqlReleaseAcceptancePreviewCandidates | ForEach-Object { Get-GenericTaskReadback $_ })
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
$automationTaskAckGateConfigured = Has-ConfigurationHint @(
    $AutomationTaskHistoryPath,
    $AutomationTaskAckPath,
    $DatabaseHealthTaskPreviewPath,
    $LargeFileGovernanceTaskPreviewPath,
    $normalizedTaskPreviewPaths
)
$automationTaskAckGate = Get-AutomationTaskAckGateReadback $automationTaskHistoryState $automationTaskAckState $automationTaskAckGateConfigured

$databaseHealthStatus = $databaseHealthStatusState.value
$databaseHealthLastRun = Read-LastRunSummary $databaseHealthLastRunState
$largeFileGovernanceStatus = $largeFileGovernanceStatusState.value
$largeFileGovernanceLastRun = Read-LastRunSummary $largeFileGovernanceLastRunState
$automationTaskHistory = $automationTaskHistoryState.value
$automationTaskAck = $automationTaskAckState.value

$e2eProductionBacklog = @(
    "1. E2E production crypto is the active automation lane again. Linked OpenSSL builds now run the reviewed provider table through public API dispatch. The default status surface keeps the callable manifest, sanitized execution result contract, explicit reviewed runtime-preflight/arming/execution-acceptance probe readiness, and production rotation dry-run/execute evidence, promotes sanitized provider invocation probe evidence through reviewed candidate, call handoff, stub, callable bridge/interface, runtime preflight, arming, execution acceptance, data-plane bridge, and public primitive execution, and linked reviewed builds can pass the early operation, provider control, and explicit reviewed tail probe evidence gates to reach productionAcceptance.accepted=true / releaseGate=production-crypto-accepted."
    "The linked runtime gate now also drives the real public API chain directly for identity generation, public derivation, agreement sign/verify, session derivation, payload encrypt/decrypt, and tamper rejection instead of relying only on probe summaries. Normal provider probe fixtures now use 32-byte valid production material handles, while runtime self-test plus explicit round-trip/public-primitive probes reject malformed identity handles, malformed verification public keys, and malformed payload keys as invalid-input without hashing arbitrary material into usable keys. productionRolloutObservability now summarizes acceptance, material/export proof counts, public primitive readiness, filesystem object ciphertext readback readiness, operator recovery prompts, user recovery prompts, and no-sensitive-export proof; it stays fail-closed until linked acceptance passes, then reports releaseGate=production-rollout-observability-ready without exporting key/session/private identity/plaintext/ciphertext bytes."
    "e2e_rollout_observability_exporter now persists sanitized rollout observability JSON/Markdown with filesystem object readback and reviewed offline mirror opt-in gates; default CTest verifies the unlinked fail-closed artifact and the linked OpenSSL runtime gate requires accepted evidence before promotion. The runtime gate also verifies client production rotation beyond local rebind: when every provider gate is ready, executeE2EProductionRotation generates production local identity material, persists the production backend id, clears draft sessions/pending agreements without exporting sensitive material, then Alice/Bob/Carol re-announce production identities, re-verify trust pins, derive independent signed production sessions, and send encrypted private text/file payloads on openssl-reviewed-adapter-v1."
    "The same gate restarts all three clients, restores production identities/trust pins from disk, refuses to reuse memory-only sessions, derives fresh independent production sessions, and repeats encrypted text/file delivery. Encrypted private file recovery now has sender-local same-wire cache, verified filesystem object readback, explicit reviewed S3 object readback, and explicit reviewed offline mirror readback paths: auto-resume is allowed only when the local ciphertext cache, filesystem object ciphertext, reviewed S3 ciphertext object, or reviewed offline mirror ciphertext object matches the sanitized envelope header, key id/fingerprint, plaintext/wire hashes, envelope ciphertextSha256, and server resume metadata."
    "S3 remains fail-closed by default; only QTNETWORKCHAT_E2E_S3_OBJECT_RECOVERY_REVIEWED=1 plus normal S3 configuration enables reviewed HEAD/GET ciphertext readback, and status/resume evidence must not export endpoint, bucket, credentials, ciphertext, session keys, or private material. Offline mirror readback is also explicit: only QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_REVIEWED=1 plus QTNETWORKCHAT_E2E_OFFLINE_OBJECT_RECOVERY_ROOT enables canonical safe-token ciphertext readback, and status/resume evidence must not export mirror roots, local paths, ciphertext, session keys, or private material. S3/offline without reviewed opt-ins expose only safe object-key-token evidence and fixed not-reviewed gates, legacy URL/path-like object locators are suppressed from recovery status, and cache loss, missing object root, object loss, hash/header/session mismatch, unsafe locator, or unsupported auto-readback fails closed to resend/clear. Offline attachment replay preserves that header so receivers can decrypt queued ciphertext locally. Automation status now consumes the persisted rollout observability JSON/Markdown artifact together with current GitHub Windows Build visibility and local build/CTest readback; remaining E2E release work is external Windows Build visibility recovery and final production-linked release artifact promotion."
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
$lines.Add('- Tracked remote branch: `' + $TrackedRemoteBranch + '`')
$lines.Add('- Tracked remote hash: `' + $TrackedRemoteHash + '`')
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
    $lines.Add(('  Recovery gates: filesystemReady=`{0}`, filesystemGate=`{1}`, offlineReady=`{2}`, offlineGate=`{3}`' -f `
            (Format-StatusValue $e2eRolloutReadback.filesystemReady), `
            (Format-StatusValue $e2eRolloutReadback.filesystemGate), `
            (Format-StatusValue $e2eRolloutReadback.offlineReady), `
            (Format-StatusValue $e2eRolloutReadback.offlineGate)))
    $lines.Add(('  Sensitive export proof: noSensitiveExport=`{0}`, suppressed=`{1}`' -f `
            (Format-StatusValue $e2eRolloutReadback.noSensitiveExportProof), `
            (Format-StatusValue $e2eRolloutReadback.sensitiveFieldsSuppressed)))
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
        $lines.Add('- Preview task: ' + (Format-PreviewDescriptor $previewRecord))
        if ($previewRecord.taskSummary -ne "unknown") {
            $lines.Add('  Summary: `' + $previewRecord.taskSummary + '`')
        }
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
    $lines.Add(('- Task acknowledgement gate: state=`{0}`, failed=`{1}`, acknowledged=`{2}`, ackExpired=`{3}`, releaseGate=`{4}`, action=`{5}`' -f
            (Format-StatusValue $automationTaskAckGate.state),
            (Format-StatusValue $automationTaskAckGate.failedRunCount),
            (Format-StatusValue $automationTaskAckGate.acknowledged),
            (Format-StatusValue $automationTaskAckGate.ackExpired),
            (Format-StatusValue $automationTaskAckGate.releaseGate),
            (Format-StatusValue $automationTaskAckGate.action)))
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
    (Get-ArtifactIssueText "ack" $automationTaskAckState $automationTaskAckPreviewState $automationTaskAckConfig)
) -join "; "
$e2eRolloutDiagnostics = @(
    ('json={0}' -f (Format-StatusValue $e2eRolloutReadback.jsonArtifact)),
    ('markdown={0}' -f (Format-StatusValue $e2eRolloutReadback.markdownArtifact)),
    ('bundle={0}' -f (Format-StatusValue $e2eRolloutReadback.bundle))
) -join "; "
$lines.Add('- Database health artifacts: `' + $databaseHealthDiagnostics + '`')
$lines.Add('- Large-file governance artifacts: `' + $largeFileGovernanceDiagnostics + '`')
$lines.Add('- Automation history artifacts: `' + $automationHistoryDiagnostics + '`')
$lines.Add('- E2E rollout observability artifacts: `' + $e2eRolloutDiagnostics + '`')
$lines.Add("")
$lines.Add("## Priority Backlog")
$lines.Add("")
$lines.Add($e2eProductionBacklog)
$lines.Add("2. Group productization is closed for the current automation lane: private group creation/invitation/removal, private scoped messages/files, non-member and removed-member fail-closed behavior, snapshot permission fields, removed-member read-only history markers, history visibility policy fields, and send/reject audit evidence are implemented.")
$lines.Add("3. README information architecture is closed for now: keep README as the quick-start/index surface, keep testing coverage, PostgreSQL operations, large-file governance, and E2E hardening status in focused docs, and keep CTest/automation-status references pointed at those docs so long paragraphs do not return.")
$lines.Add("4. Mainwindow structure split is no longer the active lane but remains partially complete: HistoryService, TransferManager, FriendManager, GroupManager, and ClientStorage are extracted with focused CTest coverage; continue only after the current E2E production crypto lane or if explicitly redirected.")
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
    Set-Content -LiteralPath $target -Value ($lines -join [Environment]::NewLine) -Encoding UTF8
    Write-Host ("automation status: {0}" -f $target)
} else {
    Write-Output ($lines -join [Environment]::NewLine)
}

exit 0


