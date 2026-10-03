param(
    [string]$SourceCandidateDir = "build-qt6-mingw\e2e_release_evidence_linked_candidate",
    [string]$OutputDir = "build-qt6-mingw\e2e_release_evidence_linked_candidate",
    [string]$GitHubWindowsBuildStatusPath = "build-qt6-mingw\github-windows-build-status.json",
    [string]$LocalVerificationStatusPath = "build-qt6-mingw\local-verification-status.json",
    [string]$AutomationStatusPath = "docs\automation-status.md",
    [string]$ReleaseHead = "",
    [string]$GitHubWindowsBuildPolicy = "",
    [string]$AutomationPolicyPath = "",
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Ensure-File([string]$PathValue, [string]$Label) {
    if (-not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw ("Missing {0}: {1}" -f $Label, $PathValue)
    }
}

function Resolve-LinkedRolloutSource([string]$BaseDir) {
    $candidates = @(
        [pscustomobject]@{
            label = "existing-linked-candidate-package"
            json = Join-Path $BaseDir "e2e_release_evidence_linked_candidate\e2e-release-evidence\e2e-rollout-observability.json"
            markdown = Join-Path $BaseDir "e2e_release_evidence_linked_candidate\e2e-release-evidence\e2e-rollout-observability.md"
        },
        [pscustomobject]@{
            label = "packaged-linked-candidate"
            json = Join-Path $BaseDir "e2e-release-evidence\e2e-rollout-observability.json"
            markdown = Join-Path $BaseDir "e2e-release-evidence\e2e-rollout-observability.md"
        },
        [pscustomobject]@{
            label = "linked-build-rollout-evidence"
            json = Join-Path $BaseDir "e2e_rollout_observability_evidence\e2e-rollout-observability.json"
            markdown = Join-Path $BaseDir "e2e_rollout_observability_evidence\e2e-rollout-observability.md"
        },
        [pscustomobject]@{
            label = "direct-rollout-evidence"
            json = Join-Path $BaseDir "e2e-rollout-observability.json"
            markdown = Join-Path $BaseDir "e2e-rollout-observability.md"
        }
    )

    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath $candidate.json -PathType Leaf) `
                -and (Test-Path -LiteralPath $candidate.markdown -PathType Leaf)) {
            return $candidate
        }
    }

    throw ("SourceCandidateDir does not contain linked rollout observability evidence: {0}" -f $BaseDir)
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
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    try {
        $resolved = Resolve-RepoPath $PathValue
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return ""
        }
        $policy = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        if ((Get-JsonValue $policy "format" "") -ne "qtnetworkchat-automation-policy-v1") {
            return ""
        }
        Normalize-GitHubWindowsBuildPolicy ([string](Get-JsonValue $policy "gitHubWindowsBuildPolicy" ""))
    } catch {
        ""
    }
}

$resolvedSourceCandidateDir = Resolve-RepoPath $SourceCandidateDir
$resolvedOutputDir = Resolve-RepoPath $OutputDir
$resolvedCiPath = Resolve-RepoPath $GitHubWindowsBuildStatusPath
$resolvedLocalPath = Resolve-RepoPath $LocalVerificationStatusPath
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath
if ([string]::IsNullOrWhiteSpace($AutomationPolicyPath)) {
    $defaultAutomationPolicyPath = "docs\automation-policy.json"
    $resolvedDefaultAutomationPolicyPath = Resolve-RepoPath $defaultAutomationPolicyPath
    if (Test-Path -LiteralPath $resolvedDefaultAutomationPolicyPath -PathType Leaf) {
        $AutomationPolicyPath = $defaultAutomationPolicyPath
    }
}
$gitHubWindowsBuildPolicyResolved = Normalize-GitHubWindowsBuildPolicy $GitHubWindowsBuildPolicy
if ([string]::IsNullOrWhiteSpace($gitHubWindowsBuildPolicyResolved)) {
    $gitHubWindowsBuildPolicyResolved = Get-AutomationPolicyReadback $AutomationPolicyPath
}
if ([string]::IsNullOrWhiteSpace($gitHubWindowsBuildPolicyResolved)) {
    $gitHubWindowsBuildPolicyResolved = "required"
}

$sourceRollout = Resolve-LinkedRolloutSource $resolvedSourceCandidateDir
$sourceRolloutJson = $sourceRollout.json
$sourceRolloutMarkdown = $sourceRollout.markdown
Ensure-File $sourceRolloutJson ("linked rollout JSON ({0})" -f $sourceRollout.label)
Ensure-File $sourceRolloutMarkdown ("linked rollout Markdown ({0})" -f $sourceRollout.label)
if ($gitHubWindowsBuildPolicyResolved -ne "disabled") {
    Ensure-File $resolvedCiPath "GitHub Windows Build status"
}
Ensure-File $resolvedLocalPath "local verification status"

$rollout = Get-Content -LiteralPath $sourceRolloutJson -Raw -Encoding UTF8 | ConvertFrom-Json
$rolloutGate = [string](Get-JsonValue (Get-JsonValue $rollout "auditSummary" $null) "releaseGate" "")
$rolloutOk = [bool](Get-JsonValue $rollout "ok" $false)
if (-not $rolloutOk -or $rolloutGate -ne "production-rollout-observability-ready") {
    throw ("Source linked rollout evidence is not accepted: ok={0}; gate={1}" -f $rolloutOk, $rolloutGate)
}

if ([string]::IsNullOrWhiteSpace($ReleaseHead)) {
    $ReleaseHead = (& git rev-parse HEAD).Trim()
}
if ([string]::IsNullOrWhiteSpace($ReleaseHead)) {
    throw "ReleaseHead is required"
}

if ($gitHubWindowsBuildPolicyResolved -eq "disabled") {
    Write-Host "GitHub Windows Build policy: disabled; linked candidate promotion will use local verification plus production-linked evidence."
}

$inputDirName = "e2e-linked-candidate-input-{0}" -f ([guid]::NewGuid().ToString("N"))
$inputDir = Join-Path (Split-Path -Parent $resolvedOutputDir) $inputDirName
New-Item -ItemType Directory -Path $inputDir -Force | Out-Null
try {
    $inputRolloutJson = Join-Path $inputDir "e2e-rollout-observability.json"
    $inputRolloutMarkdown = Join-Path $inputDir "e2e-rollout-observability.md"
    Copy-Item -LiteralPath $sourceRolloutJson -Destination $inputRolloutJson -Force
    Copy-Item -LiteralPath $sourceRolloutMarkdown -Destination $inputRolloutMarkdown -Force

    $packager = Join-Path $PSScriptRoot "package-e2e-release-evidence.ps1"
    $packagerArgs = @(
        "-OutputDir", $resolvedOutputDir,
        "-RolloutJsonPath", $inputRolloutJson,
        "-RolloutMarkdownPath", $inputRolloutMarkdown,
        "-GitHubWindowsBuildStatusPath", $resolvedCiPath,
        "-LocalVerificationStatusPath", $resolvedLocalPath,
        "-ReleaseHead", $ReleaseHead,
        "-GitHubWindowsBuildPolicy", $gitHubWindowsBuildPolicyResolved
    )
    if (Test-Path -LiteralPath $resolvedAutomationStatusPath -PathType Leaf) {
        $packagerArgs += @("-AutomationStatusPath", $resolvedAutomationStatusPath)
    }
    if (-not [string]::IsNullOrWhiteSpace($AutomationPolicyPath)) {
        $packagerArgs += @("-AutomationPolicyPath", (Resolve-RepoPath $AutomationPolicyPath))
    }
    if ($FailOnSensitive.IsPresent) {
        $packagerArgs += "-FailOnSensitive"
    }

    & powershell -ExecutionPolicy Bypass -File $packager @packagerArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Remove-Item -LiteralPath $inputDir -Recurse -Force -ErrorAction SilentlyContinue
}

$manifestPath = Join-Path $resolvedOutputDir "e2e-release-evidence-manifest.json"
Ensure-File $manifestPath "linked candidate manifest"
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
$promotion = Get-JsonValue $manifest "promotion" $null
Write-Host "e2e linked candidate"
Write-Host ("  manifest: {0}" -f $manifestPath)
Write-Host ("  target: {0}" -f (Get-JsonValue $manifest "targetReleaseHead" "unknown"))
Write-Host ("  release gate: {0}" -f (Get-JsonValue $manifest "releaseGate" "unknown"))
Write-Host ("  promotion gate: {0}" -f (Get-JsonValue $promotion "releaseGate" "unknown"))
