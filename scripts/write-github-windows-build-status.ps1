param(
    [string]$OutputPath = "build-qt6-mingw\github-windows-build-status.json",
    [string]$Head,
    [string]$Workflow = "Windows Build",
    [string]$RunListJsonPath,
    [int]$Limit = 20,
    [switch]$PlanOnly,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
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

function New-StatusPayload(
    [string]$Status,
    [string]$RunId,
    [string]$Source,
    [string]$Visibility,
    [object[]]$Runs
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
    [ordered]@{
        format = "qtnetworkchat-github-windows-build-status-v1"
        generatedAt = (Get-Date).ToUniversalTime().ToString("o")
        workflow = $Workflow
        headSha = $Head
        status = $Status
        runId = $RunId
        source = $Source
        visibility = $Visibility
        runMatched = -not [string]::IsNullOrWhiteSpace($RunId)
        observedRunCount = $Runs.Count
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

$runs = @()
$source = "auto-gh-run-list"
$status = "unknown"
$runId = ""
$visibility = "unknown"
$runListJson = ""

if ($PlanOnly.IsPresent) {
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
        $source = "auto-gh-run-list-unavailable"
        $status = "unavailable"
        $visibility = "run-list-unavailable"
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

$payload = New-StatusPayload $status $runId $source $visibility $runs
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
