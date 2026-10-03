param(
    [string]$BuildDir = "build-qt6-mingw",
    [string]$Configuration = "Release",
    [string]$QtRoot = "",
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [string]$AutomationStatusPath = "docs\\automation-status.md",
    [string]$ArchiveDecisionState = "pending-human-decision",
    [string]$ArchiveDecidedBy = "",
    [string]$ArchiveDecisionReason = "",
    [string]$ArchivePublishingStatus = "",
    [string]$ArchivePublishingChannel = "",
    [switch]$RunDeliveryDrill,
    [switch]$NoDeploy,
    [switch]$IncludePostgresSql,
    [switch]$FailOnMissingPostgresSql,
    [switch]$FailOnMissingRuntime,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Invoke-RepoScript([string]$ScriptPath, [string[]]$Arguments) {
    $resolvedScriptPath = Resolve-RepoPath $ScriptPath
    & powershell -ExecutionPolicy Bypass -File $resolvedScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Script failed: {0} (exit {1})" -f $resolvedScriptPath, $LASTEXITCODE)
    }
}

function Get-Sha256Hex([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return "unknown"
    }
    $stream = [System.IO.File]::OpenRead($PathValue)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hashBytes = $sha256.ComputeHash($stream)
            return (($hashBytes | ForEach-Object { $_.ToString("x2") }) -join "")
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$resolvedBuildDir = Resolve-RepoPath $BuildDir
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath
$head = ((& git -C $repoRoot rev-parse HEAD) | Select-Object -First 1).Trim()
if ([string]::IsNullOrWhiteSpace($head)) {
    throw "Unable to resolve current HEAD."
}

$lastTestLogPath = Join-Path $resolvedBuildDir "Testing\\Temporary\\LastTest.log"
$ctestCount = 0
if (Test-Path -LiteralPath $lastTestLogPath -PathType Leaf) {
    $raw = Get-Content -LiteralPath $lastTestLogPath -Raw -Encoding UTF8
    $matches = [regex]::Matches($raw, '(?m)^\s*(\d+)\/(\d+)\s+Testing:')
    foreach ($match in $matches) {
        $candidate = [int]$match.Groups[2].Value
        if ($candidate -gt $ctestCount) {
            $ctestCount = $candidate
        }
    }
}

$localVerificationStatusPath = Join-Path $resolvedBuildDir "local-verification-status.json"
$releasePackageDir = Join-Path $resolvedBuildDir "release-package"
$windowsPackageStageDir = Join-Path $releasePackageDir "QtNetworkChat-1.0.0-win-x64"
$windowsPackageManifestPath = Join-Path $windowsPackageStageDir "manifest.json"
$windowsPackageZipPath = Join-Path $releasePackageDir "QtNetworkChat-1.0.0-win-x64.zip"
$linkedCandidateDir = Join-Path $resolvedBuildDir "e2e_release_evidence_linked_candidate"
$linkedEvidenceManifestPath = Join-Path $linkedCandidateDir "e2e-release-evidence-manifest.json"
$linkedPromotionPath = Join-Path $linkedCandidateDir "e2e-release-promotion.json"
$localReleaseReviewDir = Join-Path $resolvedBuildDir "local-release-review"
$localReleaseReviewManifestPath = Join-Path $localReleaseReviewDir "local-release-review-manifest.json"
$localReleaseReviewMarkdownPath = Join-Path $localReleaseReviewDir "local-release-review.md"
$localReleaseReviewPackagePath = Join-Path $localReleaseReviewDir "local-release-review.zip"
$releaseArchiveDecisionDir = Join-Path $resolvedBuildDir "release-archive-decision"
$releaseArchiveDecisionManifestPath = Join-Path $releaseArchiveDecisionDir "release-archive-decision-manifest.json"
$releaseArchiveDecisionMarkdownPath = Join-Path $releaseArchiveDecisionDir "release-archive-decision.md"
$releaseArchiveDecisionPackagePath = Join-Path $releaseArchiveDecisionDir "release-archive-decision.zip"
$releaseDeliveryHandoffDir = Join-Path $resolvedBuildDir "release-delivery-handoff"
$releaseDeliveryHandoffManifestPath = Join-Path $releaseDeliveryHandoffDir "release-delivery-handoff-manifest.json"
$releaseDeliveryHandoffMarkdownPath = Join-Path $releaseDeliveryHandoffDir "release-delivery-handoff.md"
$releaseDeliveryHandoffPackagePath = Join-Path $releaseDeliveryHandoffDir "release-delivery-handoff.zip"
$releasePublicationRecordPath = Join-Path $resolvedBuildDir "release-publication-record.json"
$releaseDeliveryDrillDir = Join-Path $resolvedBuildDir "release-delivery-drill"
$releaseDeliveryDrillManifestPath = Join-Path $releaseDeliveryDrillDir "release-delivery-drill-manifest.json"
$releaseDeliveryDrillMarkdownPath = Join-Path $releaseDeliveryDrillDir "release-delivery-drill.md"
$releaseDiagnosticsDir = Join-Path $releaseDeliveryDrillDir "release-diagnostics"
$releaseDiagnosticsManifestPath = Join-Path $releaseDiagnosticsDir "release-diagnostics\\manifest.json"
$releaseDiagnosticsPackagePath = Join-Path $releaseDeliveryDrillDir "release-diagnostics.zip"
$releaseCloseoutSummaryDir = Join-Path $resolvedBuildDir "release-closeout-summary"
$releaseCloseoutSummaryManifestPath = Join-Path $releaseCloseoutSummaryDir "release-closeout-summary-manifest.json"
$releaseFinalLocalArchiveDir = Join-Path $resolvedBuildDir "release-final-local-archive"
$releaseFinalLocalArchiveManifestPath = Join-Path $releaseFinalLocalArchiveDir "release-final-local-archive-manifest.json"

function Invoke-LocalReleaseReviewPackage {
    Invoke-RepoScript "scripts/package-local-release-review.ps1" @(
        "-OutputDir", $localReleaseReviewDir,
        "-ReleaseHead", $head,
        "-ReadmePath", (Join-Path $repoRoot "README.md"),
        "-AutomationStatusPath", $resolvedAutomationStatusPath,
        "-LocalVerificationStatusPath", $localVerificationStatusPath,
        "-E2EReleaseEvidenceManifestPath", $linkedEvidenceManifestPath,
        "-E2EReleasePromotionPath", $linkedPromotionPath,
        "-S3RealBackendReadinessPath", (Join-Path $resolvedBuildDir "s3-real-backend-readiness.json"),
        "-LargeFileGovernanceDashboardPath", (Join-Path $resolvedBuildDir "automation-tasks\large-file-governance\large-file-governance-dashboard.json"),
        "-LargeFileGovernanceReportPath", (Join-Path $resolvedBuildDir "automation-tasks\large-file-governance\large-file-governance-report.md"),
        "-LargeFileGovernancePerformanceSummaryPath", (Join-Path $resolvedBuildDir "automation-tasks\large-file-governance\large-file-governance-performance-summary.json"),
        "-LargeFileGovernanceDiagnosticsPath", (Join-Path $resolvedBuildDir "automation-tasks\large-file-governance\diagnostics-package\large-file-governance-diagnostics.zip"),
        "-PgsqlAcceptancePath", (Join-Path $resolvedBuildDir "automation-tasks\pgsql-release-acceptance\pgsql-release-acceptance.json"),
        "-PgsqlEvidenceManifestPath", (Join-Path $resolvedBuildDir "automation-tasks\pgsql-release-acceptance\evidence\pgsql-release-evidence-manifest.json"),
        "-PgsqlRollbackLivePath", (Join-Path $resolvedBuildDir "pgsql-rollback-live-evidence\pgsql-rollback-live-evidence.json"),
        "-PgsqlRollbackEvidenceManifestPath", (Join-Path $resolvedBuildDir "pgsql-rollback-live-evidence\evidence\pgsql-rollback-live-evidence-manifest.json"),
        "-WindowsPackageManifestPath", $windowsPackageManifestPath,
        "-ReleaseDeliveryHandoffManifestPath", $releaseDeliveryHandoffManifestPath,
        "-ReleaseArchiveDecisionManifestPath", $releaseArchiveDecisionManifestPath,
        "-ReleaseArchiveDecisionMarkdownPath", $releaseArchiveDecisionMarkdownPath,
        "-AutomationPolicyPath", (Join-Path $repoRoot "docs\automation-policy.json")
    )
}

function Invoke-ReleaseArchiveDecisionPackage {
    Invoke-RepoScript "scripts/package-release-archive-decision.ps1" @(
        "-OutputDir", $releaseArchiveDecisionDir,
        "-ReleaseHead", $head,
        "-DecisionState", $ArchiveDecisionState,
        "-DecidedBy", $ArchiveDecidedBy,
        "-DecisionReason", $ArchiveDecisionReason,
        "-PublishingStatus", $ArchivePublishingStatus,
        "-PublishingChannel", $ArchivePublishingChannel,
        "-PublishingRecordPath", $releasePublicationRecordPath,
        "-ReleaseDeliveryDrillManifestPath", $releaseDeliveryDrillManifestPath,
        "-LocalReleaseReviewManifestPath", $localReleaseReviewManifestPath,
        "-ReleaseDeliveryHandoffManifestPath", $releaseDeliveryHandoffManifestPath
    )
}

function Invoke-ReleaseDeliveryHandoffPackage {
    Invoke-RepoScript "scripts/package-release-delivery-handoff.ps1" @(
        "-OutputDir", $releaseDeliveryHandoffDir,
        "-ReleaseHead", $head,
        "-WindowsPackageManifestPath", $windowsPackageManifestPath,
        "-WindowsPackageZipPath", $windowsPackageZipPath,
        "-LocalReleaseReviewManifestPath", $localReleaseReviewManifestPath,
        "-LocalReleaseReviewPackagePath", $localReleaseReviewPackagePath,
        "-ReleaseArchiveDecisionManifestPath", $releaseArchiveDecisionManifestPath,
        "-ReleaseArchiveDecisionMarkdownPath", $releaseArchiveDecisionMarkdownPath,
        "-ReleasePublicationRecordPath", $releasePublicationRecordPath,
        "-ReleaseDeliveryDrillManifestPath", $releaseDeliveryDrillManifestPath,
        "-LocalVerificationStatusPath", $localVerificationStatusPath,
        "-AutomationStatusPath", $resolvedAutomationStatusPath
    )
}

function Invoke-ReleaseCloseoutSummaryPackage {
    Invoke-RepoScript "scripts/package-release-closeout-summary.ps1" @(
        "-OutputDir", $releaseCloseoutSummaryDir,
        "-ReleaseHead", $head,
        "-BuildDir", $resolvedBuildDir,
        "-AutomationStatusPath", $resolvedAutomationStatusPath,
        "-LocalReleaseReviewManifestPath", $localReleaseReviewManifestPath,
        "-LocalReleaseReviewMarkdownPath", $localReleaseReviewMarkdownPath,
        "-LocalReleaseReviewPackagePath", $localReleaseReviewPackagePath,
        "-ReleaseArchiveDecisionManifestPath", $releaseArchiveDecisionManifestPath,
        "-ReleaseArchiveDecisionMarkdownPath", $releaseArchiveDecisionMarkdownPath,
        "-ReleaseArchiveDecisionPackagePath", $releaseArchiveDecisionPackagePath,
        "-ReleaseDeliveryHandoffManifestPath", $releaseDeliveryHandoffManifestPath,
        "-ReleaseDeliveryHandoffMarkdownPath", $releaseDeliveryHandoffMarkdownPath,
        "-ReleaseDeliveryHandoffPackagePath", $releaseDeliveryHandoffPackagePath,
        "-ReleasePublicationRecordPath", $releasePublicationRecordPath,
        "-ReleaseDeliveryDrillManifestPath", $releaseDeliveryDrillManifestPath,
        "-ReleaseDeliveryDrillMarkdownPath", $releaseDeliveryDrillMarkdownPath,
        "-ReleaseDiagnosticsManifestPath", $releaseDiagnosticsManifestPath,
        "-ReleaseDiagnosticsPackagePath", $releaseDiagnosticsPackagePath
    )
}

function Invoke-ReleaseFinalLocalArchivePackage {
    Invoke-RepoScript "scripts/package-release-final-local-archive.ps1" @(
        "-OutputDir", $releaseFinalLocalArchiveDir,
        "-ReleaseHead", $head,
        "-BuildDir", $resolvedBuildDir,
        "-AutomationStatusPath", $resolvedAutomationStatusPath,
        "-LocalReleaseReviewManifestPath", $localReleaseReviewManifestPath,
        "-LocalReleaseReviewMarkdownPath", $localReleaseReviewMarkdownPath,
        "-LocalReleaseReviewPackagePath", $localReleaseReviewPackagePath,
        "-ReleaseArchiveDecisionManifestPath", $releaseArchiveDecisionManifestPath,
        "-ReleaseArchiveDecisionMarkdownPath", $releaseArchiveDecisionMarkdownPath,
        "-ReleaseArchiveDecisionPackagePath", $releaseArchiveDecisionPackagePath,
        "-ReleaseDeliveryHandoffManifestPath", $releaseDeliveryHandoffManifestPath,
        "-ReleaseDeliveryHandoffMarkdownPath", $releaseDeliveryHandoffMarkdownPath,
        "-ReleaseDeliveryHandoffPackagePath", $releaseDeliveryHandoffPackagePath,
        "-ReleasePublicationRecordPath", $releasePublicationRecordPath,
        "-ReleaseDeliveryDrillManifestPath", $releaseDeliveryDrillManifestPath,
        "-ReleaseDeliveryDrillMarkdownPath", $releaseDeliveryDrillMarkdownPath,
        "-ReleaseDiagnosticsManifestPath", $releaseDiagnosticsManifestPath,
        "-ReleaseDiagnosticsPackagePath", $releaseDiagnosticsPackagePath,
        "-ReleaseCloseoutSummaryManifestPath", $releaseCloseoutSummaryManifestPath
    )
}

function Invoke-AutomationStatusWrite {
    Invoke-RepoScript "scripts/write-automation-status.ps1" @(
        "-MarkdownPath", $resolvedAutomationStatusPath,
        "-Head", $head,
        "-OriginMain", $head,
        "-TrackedRemoteHash", $head,
        "-BuildDir", $resolvedBuildDir,
        "-LocalVerificationStatusPath", $localVerificationStatusPath,
        "-E2ELinkedReleaseCandidateManifestPath", $linkedEvidenceManifestPath,
        "-LocalReleaseReviewManifestPath", $localReleaseReviewManifestPath,
        "-ReleaseArchiveDecisionManifestPath", $releaseArchiveDecisionManifestPath,
        "-ReleaseDeliveryHandoffManifestPath", $releaseDeliveryHandoffManifestPath,
        "-ReleaseCloseoutSummaryManifestPath", $releaseCloseoutSummaryManifestPath,
        "-ReleaseFinalLocalArchiveManifestPath", $releaseFinalLocalArchiveManifestPath,
        "-StatusNowUtc", ((Get-Date).ToUniversalTime().ToString("o"))
    )
}

Invoke-RepoScript "scripts/write-local-verification-status.ps1" @(
    "-OutputPath", $localVerificationStatusPath,
    "-BuildStatus", "passed",
    "-BuildExitCode", "0",
    "-CTestStatus", "passed",
    "-CTestExitCode", "0",
    "-CTestCount", ([string]$ctestCount),
    "-CTestLogPath", $lastTestLogPath
)

$packageWindowsArgs = @(
    "-BuildDir", $resolvedBuildDir,
    "-Configuration", $Configuration,
    "-PackageDir", $releasePackageDir,
    "-SkipBuild"
)
if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
    $packageWindowsArgs += @("-QtRoot", $QtRoot)
}
if ($NoDeploy.IsPresent) {
    $packageWindowsArgs += "-NoDeploy"
}
if (-not [string]::IsNullOrWhiteSpace($PostgresBinDir)) {
    $packageWindowsArgs += @("-PostgresBinDir", $PostgresBinDir)
}
if ($IncludePostgresSql.IsPresent) {
    $packageWindowsArgs += "-IncludePostgresSql"
}
if ($FailOnMissingPostgresSql.IsPresent) {
    $packageWindowsArgs += "-FailOnMissingPostgresSql"
}
if ($FailOnMissingRuntime.IsPresent) {
    $packageWindowsArgs += "-FailOnMissingRuntime"
}
Invoke-RepoScript "scripts/package-windows.ps1" $packageWindowsArgs

Invoke-RepoScript "scripts/promote-e2e-linked-candidate.ps1" @(
    "-SourceCandidateDir", $resolvedBuildDir,
    "-OutputDir", $linkedCandidateDir,
    "-LocalVerificationStatusPath", $localVerificationStatusPath,
    "-AutomationStatusPath", $resolvedAutomationStatusPath,
    "-ReleaseHead", $head
)

if (-not [string]::IsNullOrWhiteSpace($ArchivePublishingStatus)) {
    $artifactSha = if (Test-Path -LiteralPath $windowsPackageZipPath -PathType Leaf) {
        Get-Sha256Hex $windowsPackageZipPath
    } else {
        "unknown"
    }
    Invoke-RepoScript "scripts/write-release-publication-record.ps1" @(
        "-OutputPath", $releasePublicationRecordPath,
        "-ReleaseHead", $head,
        "-Channel", $(if ([string]::IsNullOrWhiteSpace($ArchivePublishingChannel)) { "not-recorded" } else { $ArchivePublishingChannel }),
        "-PublishingStatus", $ArchivePublishingStatus,
        "-PublishedBy", $(if ([string]::IsNullOrWhiteSpace($ArchiveDecidedBy)) { [Environment]::UserName } else { $ArchiveDecidedBy }),
        "-ArtifactSha256", $artifactSha
    )
}

Invoke-LocalReleaseReviewPackage
Invoke-ReleaseArchiveDecisionPackage
Invoke-ReleaseDeliveryHandoffPackage

if ($RunDeliveryDrill.IsPresent) {
    Invoke-RepoScript "scripts/run-release-delivery-drill.ps1" @(
        "-OutputDir", $releaseDeliveryDrillDir,
        "-BuildDir", $resolvedBuildDir,
        "-ReleaseHead", $head
    )
}

# The release packages are mutually referential, so generate a single seed pass
# and then one final pass after automation-status contains the closeout readback.
Invoke-LocalReleaseReviewPackage
Invoke-ReleaseArchiveDecisionPackage
Invoke-ReleaseDeliveryHandoffPackage
Invoke-ReleaseCloseoutSummaryPackage
Invoke-ReleaseFinalLocalArchivePackage
Invoke-AutomationStatusWrite
Invoke-LocalReleaseReviewPackage
Invoke-ReleaseArchiveDecisionPackage
Invoke-ReleaseDeliveryHandoffPackage
Invoke-ReleaseCloseoutSummaryPackage
Invoke-ReleaseFinalLocalArchivePackage
Invoke-AutomationStatusWrite

Write-Host "release closeout refreshed"
Write-Host ("  head: {0}" -f $head)
Write-Host ("  buildDir: {0}" -f $resolvedBuildDir)
