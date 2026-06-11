param(
    [string]$OutputPath = "build-qt6-mingw\github-windows-build-status.json",
    [string]$Head,
    [string]$Workflow = "Windows Build",
    [string]$RunListJsonPath,
    [string]$GitHubWindowsBuildPolicy = "",
    [string]$AutomationPolicyPath = "docs\automation-policy.json",
    [int]$Limit = 20,
    [switch]$PlanOnly,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

$script:LastToolFailureClass = "none"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Get-GitHubRunListFailureClass([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return "github-cli-run-list-unavailable"
    }
    $normalized = $Text.ToLowerInvariant()
    if ($normalized -match "keyring|token.*invalid|failed to log in|gh auth login|authentication required|oauth") {
        return "github-cli-auth-invalid"
    }
    if ($normalized -match "payment|spending limit|billing") {
        return "github-actions-account-billing-blocked"
    }
    if ($normalized -match "127\.0\.0\.1:443|connection refused|connectex|dial tcp|proxy|could not resolve|resolve host|dns|timed out|timeout|no connection") {
        return "github-cli-network-unavailable"
    }
    "github-cli-run-list-unavailable"
}

function Invoke-ToolText([string]$CommandName, [string[]]$Arguments) {
    $script:LastToolFailureClass = "none"
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $CommandName @Arguments 2>&1
        $outputText = (@($output | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $_.ToString()
            } else {
                [string]$_
            }
        }) -join "`n")
        if ($LASTEXITCODE -ne 0) {
            $script:LastToolFailureClass = Get-GitHubRunListFailureClass $outputText
            $global:LASTEXITCODE = 0
            return ""
        }
        $global:LASTEXITCODE = 0
        return ($outputText.Trim())
    } catch {
        $script:LastToolFailureClass = "github-cli-run-list-unavailable"
        $global:LASTEXITCODE = 0
        return ""
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Format-StatusValue([object]$Value) {
    if ($null -eq $Value) {
        return "unknown"
    }
    $text = ([string]$Value).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        return "unknown"
    }
    $text
}

function Normalize-GitHubWindowsBuildPolicy([string]$Value) {
    $normalized = ([string]$Value).Trim().ToLowerInvariant()
    if ($normalized -in @("disabled", "optional", "required")) {
        return $normalized
    }
    ""
}

function Read-GitHubWindowsBuildPolicy([string]$PathValue) {
    $result = [ordered]@{
        policy = ""
        source = "automation-policy-missing"
    }
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return $result
    }
    $resolvedPath = Resolve-RepoPath $PathValue
    if (-not (Test-Path -LiteralPath $resolvedPath)) {
        return $result
    }
    try {
        $policyJson = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ((Format-StatusValue $policyJson.format) -ne "qtnetworkchat-automation-policy-v1") {
            $result.source = "automation-policy-invalid-format"
            return $result
        }
        $policyValue = Normalize-GitHubWindowsBuildPolicy ([string]$policyJson.gitHubWindowsBuildPolicy)
        if ([string]::IsNullOrWhiteSpace($policyValue)) {
            $result.source = "automation-policy-invalid-github-windows-build-policy"
            return $result
        }
        $result.policy = $policyValue
        $result.source = "automation-policy"
    } catch {
        $result.source = "automation-policy-unreadable"
    }
    $result
}

function New-StatusPayload(
    [string]$Status,
    [string]$RunId,
    [string]$Source,
    [string]$Visibility,
    [object[]]$Runs,
    [string]$RunListFailureClass = "none"
) {
    $latestRun = if ($Runs.Count -gt 0) { @($Runs | Select-Object -First 1)[0] } else { $null }
    $latestHeadSha = ""
    $latestStatus = ""
    $latestConclusion = ""
    $latestCreatedAt = ""
    if ($null -ne $latestRun) {
        $latestHeadSha = [string]$latestRun.headSha
        $latestStatus = [string]$latestRun.status
        $latestConclusion = [string]$latestRun.conclusion
        $latestCreatedAt = [string]$latestRun.createdAt
    }
    $externalBlocker = "none"
    $releaseGate = "github-windows-build-status-unknown"
    $operatorAction = "inspect GitHub Windows Build status before release"
    $currentHeadObserved = -not [string]::IsNullOrWhiteSpace($RunId)
    $runMatched = -not [string]::IsNullOrWhiteSpace($RunId)
    if ($Status -eq "disabled-by-policy") {
        $externalBlocker = "waived-by-policy"
        $releaseGate = "not-required"
        $operatorAction = "skip GitHub Windows Build; local build and local CTest are the active release verification path"
        $currentHeadObserved = "not-required"
        $runMatched = "not-required"
    } elseif ($Status -eq "success") {
        $releaseGate = "github-windows-build-current-head-success"
        $operatorAction = "continue release evidence review"
    } elseif ($Visibility -eq "run-list-auth-blocked") {
        $externalBlocker = "github-windows-build-gh-auth-invalid"
        $releaseGate = "blocked-ci-gh-auth-invalid"
        $operatorAction = "reauthenticate GitHub CLI before release evidence collection"
    } elseif ($Visibility -eq "run-list-network-blocked") {
        $externalBlocker = "github-windows-build-gh-network-unavailable"
        $releaseGate = "blocked-ci-gh-network-unavailable"
        $operatorAction = "restore GitHub API network/proxy access before release evidence collection"
    } elseif ($Visibility -eq "run-list-account-blocked") {
        $externalBlocker = "github-actions-account-billing-blocked"
        $releaseGate = "blocked-ci-account-billing"
        $operatorAction = "resolve GitHub Actions account billing or spending-limit blocker before release evidence collection"
    } elseif ($Visibility -eq "head-not-observed" -or $Visibility -eq "no-runs") {
        $externalBlocker = "github-windows-build-current-head-not-observed"
        $releaseGate = "blocked-ci-head-not-observed"
        $operatorAction = "wait for GitHub Windows Build to observe the current head or resolve external Actions visibility"
    } elseif ($Visibility -eq "run-list-unavailable" -or $Visibility -eq "run-list-invalid-json" -or $Visibility -eq "run-list-unreadable") {
        $externalBlocker = "github-windows-build-run-list-unavailable"
        $releaseGate = "blocked-ci-run-list-unavailable"
        $operatorAction = "restore sanitized GitHub Actions run-list readback before release"
    } elseif (-not [string]::IsNullOrWhiteSpace($Status) -and $Status -ne "unknown") {
        $releaseGate = "blocked-ci-" + $Status
        $operatorAction = "review current-head GitHub Windows Build result before release"
    }
    [ordered]@{
        format = "qtnetworkchat-github-windows-build-status-v1"
        generatedAt = (Get-Date).ToUniversalTime().ToString("o")
        workflow = $Workflow
        headSha = $Head
        status = $Status
        runId = $RunId
        source = $Source
        visibility = $Visibility
        runListFailureClass = $RunListFailureClass
        runMatched = $runMatched
        observedRunCount = $Runs.Count
        currentHeadObserved = $currentHeadObserved
        externalBlocker = $externalBlocker
        releaseGate = $releaseGate
        operatorAction = $operatorAction
        latestObserved = [ordered]@{
            headSha = Format-StatusValue $latestHeadSha
            status = Format-StatusValue $latestStatus
            conclusion = Format-StatusValue $latestConclusion
            createdAt = Format-StatusValue $latestCreatedAt
        }
        sensitiveExportProof = [ordered]@{
            noSensitiveExportProof = $true
            logsExported = $false
            annotationsExported = $false
            displayTitlesExported = $false
            tokensExported = $false
            credentialsExported = $false
        }
    }
}

if ([string]::IsNullOrWhiteSpace($Head)) {
    $Head = Invoke-ToolText "git" @("rev-parse", "HEAD")
    if ([string]::IsNullOrWhiteSpace($Head)) {
        $Head = "unknown"
    }
}

$policySource = "parameter"
$policyResolved = Normalize-GitHubWindowsBuildPolicy $GitHubWindowsBuildPolicy
if ([string]::IsNullOrWhiteSpace($policyResolved)) {
    $policyReadback = Read-GitHubWindowsBuildPolicy $AutomationPolicyPath
    $policyResolved = $policyReadback.policy
    $policySource = $policyReadback.source
}
if ([string]::IsNullOrWhiteSpace($policyResolved)) {
    $policyResolved = "required"
}

$runs = @()
$source = "auto-gh-run-list"
$status = "unknown"
$runId = ""
$visibility = "unknown"
$runListJson = ""

if ($policyResolved -eq "disabled") {
    $source = $policySource
    $status = "disabled-by-policy"
    $runId = "not-required"
    $visibility = "not-required"
} elseif ($PlanOnly.IsPresent) {
    $source = "plan-only"
    $status = "unknown"
    $visibility = "plan-only"
} elseif (-not [string]::IsNullOrWhiteSpace($RunListJsonPath)) {
    $source = "json-artifact"
    try {
        $runListJson = Get-Content -LiteralPath (Resolve-RepoPath $RunListJsonPath) -Raw -Encoding UTF8
    } catch {
        $source = "json-artifact-unreadable"
        $status = "unavailable"
        $visibility = "run-list-unreadable"
    }
} else {
    $runListJson = Invoke-ToolText "gh" @(
        "run", "list",
        "--workflow", $Workflow,
        "--limit", ([string]$Limit),
        "--json", "databaseId,headSha,status,conclusion,createdAt,workflowName"
    )
    if ([string]::IsNullOrWhiteSpace($runListJson)) {
        $failureClass = $script:LastToolFailureClass
        if ($failureClass -eq "github-cli-auth-invalid") {
            $source = "auto-gh-run-list-auth-blocked"
            $status = "external-auth-blocked"
            $visibility = "run-list-auth-blocked"
        } elseif ($failureClass -eq "github-cli-network-unavailable") {
            $source = "auto-gh-run-list-network-blocked"
            $status = "external-network-blocked"
            $visibility = "run-list-network-blocked"
        } elseif ($failureClass -eq "github-actions-account-billing-blocked") {
            $source = "auto-gh-run-list-account-blocked"
            $status = "external-account-blocked"
            $visibility = "run-list-account-blocked"
        } else {
            $source = "auto-gh-run-list-unavailable"
            $status = "unavailable"
            $visibility = "run-list-unavailable"
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($runListJson)) {
    try {
        $parsedRuns = $runListJson | ConvertFrom-Json -ErrorAction Stop
        $runs = @($parsedRuns | ForEach-Object { $_ })
    } catch {
        $source = $source + "-invalid-json"
        $status = "unavailable"
        $visibility = "run-list-invalid-json"
        $runs = @()
    }
}

if ($runs.Count -gt 0 -and $status -eq "unknown") {
    $normalizedHead = $Head.Trim().ToLowerInvariant()
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
        $status = "external-visibility-stale"
        $visibility = "head-not-observed"
    } else {
        $runStatus = ([string]$matchingRun.status).Trim().ToLowerInvariant()
        $runConclusion = ([string]$matchingRun.conclusion).Trim().ToLowerInvariant()
        if ($runStatus -eq "completed" -and -not [string]::IsNullOrWhiteSpace($runConclusion)) {
            $status = $runConclusion
        } elseif (-not [string]::IsNullOrWhiteSpace($runStatus)) {
            $status = $runStatus
        } else {
            $status = "unknown"
        }
        $runId = [string]$matchingRun.databaseId
        $visibility = "current-head-observed"
    }
} elseif ($runs.Count -eq 0 -and $status -eq "unknown") {
    $status = "external-visibility-stale"
    $visibility = "no-runs"
}

$payload = New-StatusPayload $status $runId $source $visibility $runs $script:LastToolFailureClass
$json = $payload | ConvertTo-Json -Depth 6

foreach ($forbidden in @("ghp_", "github_pat_", "Authorization:", "Credential=", "Signature=")) {
    if ($json.Contains($forbidden)) {
        Write-Host ("github windows build status leaked forbidden text: {0}" -f $forbidden)
        if ($FailOnSensitive) {
            exit 2
        }
    }
}

if ($PlanOnly.IsPresent) {
    Write-Output $json
} else {
    $resolvedOutput = Resolve-RepoPath $OutputPath
    $parent = Split-Path -Parent $resolvedOutput
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $json | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
    Write-Host ("github windows build status: {0}" -f $resolvedOutput)
}
