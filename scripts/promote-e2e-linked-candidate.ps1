param(
    [string]$SourceCandidateDir = "build-qt6-mingw\e2e_release_evidence_linked_candidate",
    [string]$OutputDir = "build-qt6-mingw\e2e_release_evidence_linked_candidate",
    [string]$GitHubWindowsBuildStatusPath = "build-qt6-mingw\github-windows-build-status.json",
    [string]$LocalVerificationStatusPath = "build-qt6-mingw\local-verification-status.json",
    [string]$AutomationStatusPath = "docs\automation-status.md",
    [string]$ReleaseHead = "",
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

$resolvedSourceCandidateDir = Resolve-RepoPath $SourceCandidateDir
$resolvedOutputDir = Resolve-RepoPath $OutputDir
$resolvedCiPath = Resolve-RepoPath $GitHubWindowsBuildStatusPath
$resolvedLocalPath = Resolve-RepoPath $LocalVerificationStatusPath
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath

$sourceEvidenceDir = Join-Path $resolvedSourceCandidateDir "e2e-release-evidence"
$sourceRolloutJson = Join-Path $sourceEvidenceDir "e2e-rollout-observability.json"
$sourceRolloutMarkdown = Join-Path $sourceEvidenceDir "e2e-rollout-observability.md"
Ensure-File $sourceRolloutJson "linked rollout JSON"
Ensure-File $sourceRolloutMarkdown "linked rollout Markdown"
Ensure-File $resolvedCiPath "GitHub Windows Build status"
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
        "-ReleaseHead", $ReleaseHead
    )
    if (Test-Path -LiteralPath $resolvedAutomationStatusPath -PathType Leaf) {
        $packagerArgs += @("-AutomationStatusPath", $resolvedAutomationStatusPath)
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
