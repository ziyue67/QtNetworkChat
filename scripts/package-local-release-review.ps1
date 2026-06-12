param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$MarkdownPath,
    [string]$ReleaseHead,
    [string]$AutomationPolicyPath = "",

    [string]$ReadmePath = "README.md",
    [string]$AutomationStatusPath = "docs\\automation-status.md",
    [string]$E2EHardeningStatusPath = "docs\\e2e-hardening-status.md",
    [string]$EndToEndEncryptionPlanPath = "docs\\end-to-end-encryption-plan.md",
    [string]$LocalVerificationStatusPath = "build-qt6-mingw\\local-verification-status.json",
    [string]$E2EReleaseEvidenceManifestPath = "",
    [string]$E2EReleasePromotionPath = "",
    [string]$S3RealBackendReadinessPath = "build-qt6-mingw\\s3-real-backend-readiness.json",
    [string]$LargeFileGovernanceDashboardPath = "build-qt6-mingw\\automation-tasks\\large-file-governance\\large-file-governance-dashboard.json",
    [string]$LargeFileGovernanceReportPath = "build-qt6-mingw\\automation-tasks\\large-file-governance\\large-file-governance-report.md",
    [string]$LargeFileGovernanceDiagnosticsPath = "build-qt6-mingw\\automation-tasks\\large-file-governance\\diagnostics-package\\large-file-governance-diagnostics.zip",
    [string]$PgsqlAcceptancePath = "build-qt6-mingw\\automation-tasks\\pgsql-release-acceptance\\pgsql-release-acceptance.json",
    [string]$PgsqlEvidenceManifestPath = "build-qt6-mingw\\automation-tasks\\pgsql-release-acceptance\\evidence\\pgsql-release-evidence-manifest.json",
    [string]$PgsqlRollbackLivePath = "build-qt6-mingw\\pgsql-rollback-live-evidence\\pgsql-rollback-live-evidence.json",
    [string]$PgsqlRollbackEvidenceManifestPath = "build-qt6-mingw\\pgsql-rollback-live-evidence\\evidence\\pgsql-rollback-live-evidence-manifest.json",
    [string]$WindowsPackageManifestPath = "build-qt6-mingw\\release-package\\QtNetworkChat-1.0.0-win-x64\\manifest.json",
    [string]$ReleaseDeliveryHandoffManifestPath = "build-qt6-mingw\\release-delivery-handoff\\release-delivery-handoff-manifest.json",
    [string]$ReleaseDeliveryHandoffScriptPath = "scripts\\package-release-delivery-handoff.ps1",

    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

$sensitivePatterns = @(
    '(^|["''\s{,])password["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])PGPASSWORD["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    'ghp_[A-Za-z0-9_]+',
    'github_pat_[A-Za-z0-9_]+',
    '(^|["''\s{,])secret[-_\s]?key["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])access[-_\s]?key["'']?\s*[:=]\s*(?!["'']?<redacted>)',
    '(^|["''\s{,])session[-_\s]?token["'']?\s*[:=]\s*(?!["'']?<redacted>)',
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

function Resolve-RepoPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
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

function Read-OptionalJson([string]$PathValue) {
    $resolvedPath = Resolve-OptionalPath $PathValue
    if ([string]::IsNullOrWhiteSpace($resolvedPath) -or -not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        return $null
    }
    $raw = Get-Content -LiteralPath $resolvedPath -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }
    $raw | ConvertFrom-Json -ErrorAction Stop
}

function Resolve-WindowsPackageManifestPath([string]$PreferredPath) {
    $resolvedPreferred = Resolve-RepoPath $PreferredPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedPreferred) -and (Test-Path -LiteralPath $resolvedPreferred -PathType Leaf)) {
        return $resolvedPreferred
    }

    $releasePackageRoot = Resolve-RepoPath "build-qt6-mingw\\release-package"
    if ([string]::IsNullOrWhiteSpace($releasePackageRoot) -or -not (Test-Path -LiteralPath $releasePackageRoot -PathType Container)) {
        return $resolvedPreferred
    }

    $manifests = Get-ChildItem -LiteralPath $releasePackageRoot -Recurse -Filter manifest.json -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending
    foreach ($candidate in $manifests) {
        try {
            $manifest = Get-Content -LiteralPath $candidate.FullName -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
            if ((Get-JsonValue $manifest "packageFormat" "") -eq "qtnetworkchat-windows-package-v1") {
                return $candidate.FullName
            }
        } catch {
        }
    }

    $resolvedPreferred
}

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        return
    }
    $extension = ([System.IO.Path]::GetExtension($PathValue)).ToLowerInvariant()
    if ($extension -in @(".zip", ".dll", ".exe", ".sqlite3", ".bin", ".meta")) {
        return
    }
    $lineNumber = 0
    Get-Content -LiteralPath $PathValue -Encoding UTF8 | ForEach-Object {
        $lineNumber += 1
        $line = [string]$_
        foreach ($pattern in $sensitivePatterns) {
            if ($line -match $pattern -and $line -notmatch "(<redacted>|\\u003credacted\\u003e|&lt;redacted&gt;)") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
    }
}

function Copy-EvidenceFile(
    [string]$SourcePath,
    [string]$TargetDir,
    [string]$PackageRelativePath,
    [string]$Kind,
    [System.Collections.ArrayList]$ManifestInputs,
    [System.Collections.ArrayList]$ScanPaths
) {
    $resolvedSource = Resolve-OptionalPath $SourcePath
    if ([string]::IsNullOrWhiteSpace($resolvedSource) -or -not (Test-Path -LiteralPath $resolvedSource -PathType Leaf)) {
        return $false
    }
    $targetPath = Join-Path $TargetDir $PackageRelativePath
    $targetParent = Split-Path -Parent $targetPath
    if (-not [string]::IsNullOrWhiteSpace($targetParent)) {
        New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
    }
    Copy-Item -LiteralPath $resolvedSource -Destination $targetPath -Force
    [void]$ScanPaths.Add($resolvedSource)
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $Kind
        sourceName = Split-Path -Leaf $resolvedSource
        packagedAs = ($PackageRelativePath -replace "\\", "/")
        bytes = (Get-Item -LiteralPath $resolvedSource).Length
        sha256 = Get-Sha256Hex $resolvedSource
    })
    $true
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
    }
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return [pscustomobject]$result
    }
    $result.configured = $true
    try {
        $resolved = Resolve-OptionalPath $PathValue
        if ([string]::IsNullOrWhiteSpace($resolved) -or -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return [pscustomobject]$result
        }
        $policy = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
        $result.readable = $true
        if ((Get-JsonValue $policy "format" "") -ne "qtnetworkchat-automation-policy-v1") {
            return [pscustomobject]$result
        }
        $policyValue = Normalize-GitHubWindowsBuildPolicy ([string](Get-JsonValue $policy "gitHubWindowsBuildPolicy" ""))
        if ([string]::IsNullOrWhiteSpace($policyValue)) {
            return [pscustomobject]$result
        }
        $result.valid = $true
        $result.githubWindowsBuildPolicy = $policyValue
    } catch {
    }
    [pscustomobject]$result
}

function Test-HeadMatch([string]$Expected, [string]$Actual) {
    $left = (Format-Value $Expected).ToLowerInvariant()
    $right = (Format-Value $Actual).ToLowerInvariant()
    if ($left -eq "unknown" -or $right -eq "unknown") {
        return $false
    }
    $left -eq $right -or $left.StartsWith($right) -or $right.StartsWith($left)
}

function New-ArtifactSummary([string]$Kind, [bool]$Present, [bool]$Ready, [string]$Gate, [string]$Detail, [bool]$Blocking) {
    [pscustomobject]@{
        kind = $Kind
        present = $Present
        ready = $Ready
        releaseGate = $Gate
        detail = $Detail
        blocking = $Blocking
    }
}

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "local-release-review.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "local-release-review-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $MarkdownPath = Join-Path $resolvedOutputDir "local-release-review.md"
}
if ([string]::IsNullOrWhiteSpace($AutomationPolicyPath)) {
    $defaultAutomationPolicyPath = "docs\\automation-policy.json"
    $resolvedDefaultAutomationPolicyPath = Resolve-RepoPath $defaultAutomationPolicyPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedDefaultAutomationPolicyPath) `
            -and (Test-Path -LiteralPath $resolvedDefaultAutomationPolicyPath -PathType Leaf)) {
        $AutomationPolicyPath = $resolvedDefaultAutomationPolicyPath
    }
}
if ([string]::IsNullOrWhiteSpace($E2EReleaseEvidenceManifestPath)) {
    $linkedManifest = Resolve-RepoPath "build-qt6-mingw\\e2e_release_evidence_linked_candidate\\e2e-release-evidence-manifest.json"
    if (Test-Path -LiteralPath $linkedManifest -PathType Leaf) {
        $E2EReleaseEvidenceManifestPath = $linkedManifest
    } else {
        $E2EReleaseEvidenceManifestPath = Resolve-RepoPath "build-qt6-mingw\\e2e_release_evidence\\e2e-release-evidence-manifest.json"
    }
}
if ([string]::IsNullOrWhiteSpace($E2EReleasePromotionPath)) {
    $manifestParent = Split-Path -Parent (Resolve-RepoPath $E2EReleaseEvidenceManifestPath)
    if (-not [string]::IsNullOrWhiteSpace($manifestParent)) {
        $candidatePromotionPath = Join-Path $manifestParent "e2e-release-promotion.json"
        if (Test-Path -LiteralPath $candidatePromotionPath -PathType Leaf) {
            $E2EReleasePromotionPath = $candidatePromotionPath
        }
    }
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath

$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}
$manifestParent = Split-Path -Parent $resolvedManifestPath
if (-not [string]::IsNullOrWhiteSpace($manifestParent)) {
    New-Item -ItemType Directory -Path $manifestParent -Force | Out-Null
}
$markdownParent = Split-Path -Parent $resolvedMarkdownPath
if (-not [string]::IsNullOrWhiteSpace($markdownParent)) {
    New-Item -ItemType Directory -Path $markdownParent -Force | Out-Null
}

if ([string]::IsNullOrWhiteSpace($ReleaseHead)) {
    try {
        $ReleaseHead = ((& git rev-parse HEAD) | Select-Object -First 1).Trim()
    } catch {
        $ReleaseHead = ""
    }
}
if ([string]::IsNullOrWhiteSpace($ReleaseHead)) {
    $ReleaseHead = "unknown"
}

$policyReadback = Get-AutomationPolicyReadback $AutomationPolicyPath
$gitHubWindowsBuildPolicy = if ($policyReadback.valid) { $policyReadback.githubWindowsBuildPolicy } else { "unknown" }

$resolvedReadmePath = Resolve-RepoPath $ReadmePath
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath
$resolvedE2EHardeningStatusPath = Resolve-RepoPath $E2EHardeningStatusPath
$resolvedEndToEndPlanPath = Resolve-RepoPath $EndToEndEncryptionPlanPath
$resolvedLocalVerificationStatusPath = Resolve-RepoPath $LocalVerificationStatusPath
$resolvedE2EReleaseEvidenceManifestPath = Resolve-RepoPath $E2EReleaseEvidenceManifestPath
$resolvedE2EReleasePromotionPath = Resolve-RepoPath $E2EReleasePromotionPath
$resolvedS3RealBackendReadinessPath = Resolve-RepoPath $S3RealBackendReadinessPath
$resolvedLargeFileGovernanceDashboardPath = Resolve-RepoPath $LargeFileGovernanceDashboardPath
$resolvedLargeFileGovernanceReportPath = Resolve-RepoPath $LargeFileGovernanceReportPath
$resolvedLargeFileGovernanceDiagnosticsPath = Resolve-RepoPath $LargeFileGovernanceDiagnosticsPath
$resolvedPgsqlAcceptancePath = Resolve-RepoPath $PgsqlAcceptancePath
$resolvedPgsqlEvidenceManifestPath = Resolve-RepoPath $PgsqlEvidenceManifestPath
$resolvedPgsqlRollbackLivePath = Resolve-RepoPath $PgsqlRollbackLivePath
$resolvedPgsqlRollbackEvidenceManifestPath = Resolve-RepoPath $PgsqlRollbackEvidenceManifestPath
$resolvedWindowsPackageManifestPath = Resolve-WindowsPackageManifestPath $WindowsPackageManifestPath
$resolvedReleaseDeliveryHandoffManifestPath = Resolve-RepoPath $ReleaseDeliveryHandoffManifestPath
$resolvedReleaseDeliveryHandoffScriptPath = Resolve-RepoPath $ReleaseDeliveryHandoffScriptPath

$e2eManifest = Read-OptionalJson $resolvedE2EReleaseEvidenceManifestPath
$e2ePromotion = Read-OptionalJson $resolvedE2EReleasePromotionPath
$localVerification = Read-OptionalJson $resolvedLocalVerificationStatusPath
$s3Readiness = Read-OptionalJson $resolvedS3RealBackendReadinessPath
$governanceDashboard = Read-OptionalJson $resolvedLargeFileGovernanceDashboardPath
$pgsqlAcceptance = Read-OptionalJson $resolvedPgsqlAcceptancePath
$pgsqlRollbackLive = Read-OptionalJson $resolvedPgsqlRollbackLivePath
$windowsPackageManifest = Read-OptionalJson $resolvedWindowsPackageManifestPath
$releaseDeliveryHandoffManifest = Read-OptionalJson $resolvedReleaseDeliveryHandoffManifestPath

$artifactSummaries = New-Object System.Collections.ArrayList
$blockers = New-Object System.Collections.ArrayList
$nonBlockingObservations = New-Object System.Collections.ArrayList

$deliveryTailPending = @(
    "release-auto-upload-not-implemented",
    "installer-not-packaged",
    "crash-diagnostic-collection-not-productized",
    "non-dev-ops-handoff-not-productized"
)

$localVerificationReady = $false
$localVerificationGate = "local-verification-missing"
$localVerificationDetail = "missing"
if ($null -ne $localVerification -and (Get-JsonValue $localVerification "format" "") -eq "qtnetworkchat-local-verification-status-v1") {
    $localVerificationReady =
        [bool](Get-JsonValue $localVerification "ok" $false) `
        -and (Format-Value (Get-JsonValue (Get-JsonValue $localVerification "build" $null) "status" "")) -eq "passed" `
        -and (Format-Value (Get-JsonValue (Get-JsonValue $localVerification "ctest" $null) "status" "")) -eq "passed"
    $localVerificationGate = if ($localVerificationReady) { "local-verification-passed" } else { "local-verification-failed" }
    $localVerificationDetail = ('build={0}; ctest={1}; count={2}' -f `
        (Format-Value (Get-JsonValue (Get-JsonValue $localVerification "build" $null) "status" "")), `
        (Format-Value (Get-JsonValue (Get-JsonValue $localVerification "ctest" $null) "status" "")), `
        (Format-Value (Get-JsonValue (Get-JsonValue $localVerification "ctest" $null) "count" 0)))
} else {
    [void]$blockers.Add("local-verification-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "local-verification" ($null -ne $localVerification) $localVerificationReady $localVerificationGate $localVerificationDetail $true))
if (-not $localVerificationReady -and -not $blockers.Contains("local-verification-missing")) {
    [void]$blockers.Add("local-verification-not-ready")
}

$e2eReady = $false
$e2eGate = "e2e-release-review-missing"
$e2eDetail = "missing"
$e2eSource = "unknown"
$e2ePackageSha256 = "unknown"
if ($null -ne $e2eManifest -and (Get-JsonValue $e2eManifest "format" "") -eq "qtnetworkchat-e2e-release-evidence-package-v1") {
    $e2eSource = if ($resolvedE2EReleaseEvidenceManifestPath -match "linked_candidate") { "linked-current-head-candidate" } else { "default-release-evidence" }
    $e2ePackageSha256 = Format-Value (Get-JsonValue $e2eManifest "packageSha256" "unknown")
    $e2eReleaseReady = [bool](Get-JsonValue $e2eManifest "releaseReady" $false)
    $e2ePromoted = if ($null -ne $e2ePromotion) { [bool](Get-JsonValue $e2ePromotion "promoted" $false) } else { [bool](Get-JsonValue (Get-JsonValue $e2eManifest "promotion" $null) "promoted" $false) }
    $e2ePromotionReady = if ($null -ne $e2ePromotion) { [bool](Get-JsonValue $e2ePromotion "promotionReady" $false) } else { [bool](Get-JsonValue (Get-JsonValue $e2eManifest "promotion" $null) "promotionReady" $false) }
    $productionLinkedReady = [bool](Get-JsonValue (Get-JsonValue $e2eManifest "productionLinkedEvidence" $null) "ready" $false)
    $e2eGate = Format-Value (Get-JsonValue $e2eManifest "releaseGate" "unknown")
    $e2eReady = $e2eReleaseReady -and $e2ePromoted -and $e2ePromotionReady -and $productionLinkedReady
    $e2eDetail = ('source={0}; releaseReady={1}; promoted={2}; productionLinked={3}; gate={4}; packageSha256={5}' -f `
        $e2eSource, (Format-Value $e2eReleaseReady), (Format-Value $e2ePromoted), `
        (Format-Value $productionLinkedReady), $e2eGate, $e2ePackageSha256)
} else {
    [void]$blockers.Add("e2e-release-evidence-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "e2e-release-review" ($null -ne $e2eManifest) $e2eReady $e2eGate $e2eDetail $true))
if (-not $e2eReady -and -not $blockers.Contains("e2e-release-evidence-missing")) {
    [void]$blockers.Add("e2e-release-review-not-ready")
}

$s3Ready = $false
$s3Gate = "s3-real-backend-readiness-missing"
$s3Detail = "missing"
if ($null -ne $s3Readiness -and (Get-JsonValue $s3Readiness "format" "") -eq "qtnetworkchat-s3-real-backend-readiness-v1") {
    $s3Gate = Format-Value (Get-JsonValue (Get-JsonValue $s3Readiness "auditSummary" $null) "releaseGate" "unknown")
    $s3Ready = [bool](Get-JsonValue $s3Readiness "ok" $false) -and $s3Gate -eq "can-review-s3-real-backend-evidence"
    $s3Detail = ('status={0}; readiness={1}; gate={2}' -f `
        (Format-Value (Get-JsonValue $s3Readiness "status" "unknown")), `
        (Format-Value (Get-JsonValue (Get-JsonValue $s3Readiness "summary" $null) "readiness" "unknown")), `
        $s3Gate)
} else {
    [void]$blockers.Add("s3-real-backend-readiness-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "s3-real-backend-readiness" ($null -ne $s3Readiness) $s3Ready $s3Gate $s3Detail $true))
if (-not $s3Ready -and -not $blockers.Contains("s3-real-backend-readiness-missing")) {
    [void]$blockers.Add("s3-real-backend-readiness-not-reviewable")
}

$governanceReady = $false
$governanceGate = "large-file-governance-missing"
$governanceDetail = "missing"
if ($null -ne $governanceDashboard -and (Get-JsonValue $governanceDashboard "format" "") -eq "qtnetworkchat-large-file-governance-dashboard-v1") {
    $governanceGate = Format-Value (Get-JsonValue (Get-JsonValue $governanceDashboard "auditSummary" $null) "releaseGate" "unknown")
    $governanceReady = [bool](Get-JsonValue $governanceDashboard "ok" $false) -and $governanceGate -eq "can-review-governance-evidence"
    $governanceDetail = ('status={0}; warnings={1}; gate={2}' -f `
        (Format-Value (Get-JsonValue $governanceDashboard "status" "unknown")), `
        (Format-Value (Get-JsonValue $governanceDashboard "totalWarnings" "unknown")), `
        $governanceGate)
} else {
    [void]$blockers.Add("large-file-governance-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "large-file-governance" ($null -ne $governanceDashboard) $governanceReady $governanceGate $governanceDetail $true))
if (-not $governanceReady -and -not $blockers.Contains("large-file-governance-missing")) {
    [void]$blockers.Add("large-file-governance-not-reviewable")
}

$pgsqlAcceptanceReady = $false
$pgsqlAcceptanceGate = "pgsql-acceptance-missing"
$pgsqlAcceptanceDetail = "missing"
if ($null -ne $pgsqlAcceptance -and (Get-JsonValue $pgsqlAcceptance "format" "") -eq "qtnetworkchat-pgsql-release-acceptance-v1") {
    $pgsqlAcceptanceGate = Format-Value (Get-JsonValue (Get-JsonValue $pgsqlAcceptance "auditSummary" $null) "releaseGate" "unknown")
    $pgsqlAcceptanceReady = [bool](Get-JsonValue $pgsqlAcceptance "ok" $false) -and $pgsqlAcceptanceGate -eq "can-review-cutover"
    $pgsqlAcceptanceDetail = ('status={0}; readiness={1}; gate={2}' -f `
        (Format-Value (Get-JsonValue $pgsqlAcceptance "status" "unknown")), `
        (Format-Value (Get-JsonValue (Get-JsonValue $pgsqlAcceptance "summary" $null) "readiness" "unknown")), `
        $pgsqlAcceptanceGate)
} else {
    [void]$blockers.Add("pgsql-acceptance-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "pgsql-acceptance" ($null -ne $pgsqlAcceptance) $pgsqlAcceptanceReady $pgsqlAcceptanceGate $pgsqlAcceptanceDetail $true))
if (-not $pgsqlAcceptanceReady -and -not $blockers.Contains("pgsql-acceptance-missing")) {
    [void]$blockers.Add("pgsql-acceptance-not-reviewable")
}

$pgsqlRollbackReady = $false
$pgsqlRollbackGate = "pgsql-rollback-live-missing"
$pgsqlRollbackDetail = "missing"
if ($null -ne $pgsqlRollbackLive -and (Get-JsonValue $pgsqlRollbackLive "format" "") -eq "qtnetworkchat-pgsql-rollback-live-evidence-v1") {
    $pgsqlRollbackGate = Format-Value (Get-JsonValue (Get-JsonValue $pgsqlRollbackLive "summary" $null) "releaseGate" "unknown")
    $pgsqlRollbackReady = [bool](Get-JsonValue $pgsqlRollbackLive "ok" $false) -and $pgsqlRollbackGate -eq "can-close-pgsql-rollback-live-evidence"
    $pgsqlRollbackDetail = ('status={0}; readiness={1}; gate={2}' -f `
        (Format-Value (Get-JsonValue $pgsqlRollbackLive "status" "unknown")), `
        (Format-Value (Get-JsonValue (Get-JsonValue $pgsqlRollbackLive "summary" $null) "readiness" "unknown")), `
        $pgsqlRollbackGate)
} else {
    [void]$blockers.Add("pgsql-rollback-live-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "pgsql-rollback-live" ($null -ne $pgsqlRollbackLive) $pgsqlRollbackReady $pgsqlRollbackGate $pgsqlRollbackDetail $true))
if (-not $pgsqlRollbackReady -and -not $blockers.Contains("pgsql-rollback-live-missing")) {
    [void]$blockers.Add("pgsql-rollback-live-not-reviewable")
}

$windowsPackagePresent = $null -ne $windowsPackageManifest -and (Get-JsonValue $windowsPackageManifest "packageFormat" "") -eq "qtnetworkchat-windows-package-v1"
$windowsPackageCurrentHeadMatch = $false
$windowsPackageGate = "windows-package-not-available"
$windowsPackageDetail = "missing"
$windowsRuntimeOk = $false
$windowsPostgresOk = $false
if ($windowsPackagePresent) {
    $windowsGitCommit = Format-Value (Get-JsonValue $windowsPackageManifest "gitCommit" "unknown")
    $windowsRuntimeOk = [bool](Get-JsonValue (Get-JsonValue $windowsPackageManifest "runtimeCheck" $null) "ok" $false)
    $windowsPostgresOk = [bool](Get-JsonValue (Get-JsonValue $windowsPackageManifest "postgresSqlRuntime" $null) "ok" $false)
    $windowsPackageCurrentHeadMatch = Test-HeadMatch $ReleaseHead $windowsGitCommit
    if ($windowsPackageCurrentHeadMatch) {
        $windowsPackageGate = "windows-package-current-head-ready"
    } elseif ($windowsRuntimeOk -and $windowsPostgresOk) {
        $windowsPackageGate = "windows-package-sample-stale"
        [void]$nonBlockingObservations.Add("windows-package-manifest-stale-sample")
    } else {
        $windowsPackageGate = "windows-package-incomplete"
        [void]$nonBlockingObservations.Add("windows-package-manifest-incomplete")
    }
    $windowsPackageDetail = ('gitCommit={0}; runtimeOk={1}; postgresRuntimeOk={2}; currentHeadMatch={3}' -f `
        $windowsGitCommit, (Format-Value $windowsRuntimeOk), (Format-Value $windowsPostgresOk), (Format-Value $windowsPackageCurrentHeadMatch))
} else {
    [void]$nonBlockingObservations.Add("windows-package-manifest-missing")
}
[void]$artifactSummaries.Add((New-ArtifactSummary "windows-package" $windowsPackagePresent ($windowsPackagePresent -and $windowsPackageCurrentHeadMatch) $windowsPackageGate $windowsPackageDetail $false))

$releaseDeliveryHandoffAvailable = $null -ne $releaseDeliveryHandoffManifest -and (Get-JsonValue $releaseDeliveryHandoffManifest "format" "") -eq "qtnetworkchat-release-delivery-handoff-v1"
$releaseDeliveryHandoffReady = $releaseDeliveryHandoffAvailable -and [bool](Get-JsonValue $releaseDeliveryHandoffManifest "deliveryReady" $false)
$manifestTail = @()
if ($releaseDeliveryHandoffAvailable) {
    $manifestTail = @(Get-JsonValue $releaseDeliveryHandoffManifest "deliveryTailPending" @())
}

$uploadPlanComponentReady = $windowsPackagePresent -and $windowsPackageCurrentHeadMatch -and $windowsRuntimeOk
$installerComponentReady = Test-Path -LiteralPath (Resolve-RepoPath "scripts\\install-qtnetworkchat-package.ps1") -PathType Leaf
$diagnosticsComponentReady = Test-Path -LiteralPath (Resolve-RepoPath "scripts\\collect-qtnetworkchat-diagnostics.ps1") -PathType Leaf
$opsHandoffComponentReady = Test-Path -LiteralPath $resolvedReleaseDeliveryHandoffScriptPath -PathType Leaf

if ($releaseDeliveryHandoffReady -and $manifestTail.Count -eq 0) {
    $deliveryTailPending = @()
} elseif ($manifestTail.Count -gt 0) {
    $deliveryTailPending = @($manifestTail | ForEach-Object { [string]$_ })
} else {
    $deliveryTailPending = @()
    if (-not $uploadPlanComponentReady) {
        $deliveryTailPending += "release-auto-upload-not-implemented"
    }
    if (-not $installerComponentReady) {
        $deliveryTailPending += "installer-not-packaged"
    }
    if (-not $diagnosticsComponentReady) {
        $deliveryTailPending += "crash-diagnostic-collection-not-productized"
    }
    if (-not $opsHandoffComponentReady) {
        $deliveryTailPending += "non-dev-ops-handoff-not-productized"
    }
}

[void]$artifactSummaries.Add((New-ArtifactSummary "release-delivery-handoff" ($null -ne $releaseDeliveryHandoffManifest) ([bool](Get-JsonValue $releaseDeliveryHandoffManifest "deliveryReady" $false)) (Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "deliveryGate" "unknown")) ('deliveryTail={0}' -f $deliveryTailPending.Count) $false))

$reviewReady = $blockers.Count -eq 0
$reviewGate = if ($reviewReady) { "ready-for-final-archive-decision" } else { "blocked-local-release-review-core-gates" }
$operatorAction = if ($reviewReady) {
    "Review the packaged local release evidence, record the final archive decision, and track delivery-tail follow-up items separately from the verified code gate."
} else {
    "Do not record a final archive decision yet; resolve the blocking local release review gates and regenerate this package."
}

$stagingDir = Join-Path $resolvedOutputDir "local-release-review"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

[void](Copy-EvidenceFile $resolvedReadmePath $stagingDir "docs/README.md" "readme" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedAutomationStatusPath $stagingDir "docs/automation-status.md" "automation-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedE2EHardeningStatusPath $stagingDir "docs/e2e-hardening-status.md" "e2e-hardening-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedEndToEndPlanPath $stagingDir "docs/end-to-end-encryption-plan.md" "end-to-end-encryption-plan" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalVerificationStatusPath $stagingDir "verification/local-verification-status.json" "local-verification-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedE2EReleaseEvidenceManifestPath $stagingDir "e2e/e2e-release-evidence-manifest.json" "e2e-release-evidence-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedE2EReleasePromotionPath $stagingDir "e2e/e2e-release-promotion.json" "e2e-release-promotion" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedS3RealBackendReadinessPath $stagingDir "s3/s3-real-backend-readiness.json" "s3-real-backend-readiness" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLargeFileGovernanceDashboardPath $stagingDir "governance/large-file-governance-dashboard.json" "large-file-governance-dashboard" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLargeFileGovernanceReportPath $stagingDir "governance/large-file-governance-report.md" "large-file-governance-report" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLargeFileGovernanceDiagnosticsPath $stagingDir "governance/large-file-governance-diagnostics.zip" "large-file-governance-diagnostics" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedPgsqlAcceptancePath $stagingDir "pgsql/pgsql-release-acceptance.json" "pgsql-release-acceptance" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedPgsqlEvidenceManifestPath $stagingDir "pgsql/pgsql-release-evidence-manifest.json" "pgsql-release-evidence-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedPgsqlRollbackLivePath $stagingDir "pgsql/pgsql-rollback-live-evidence.json" "pgsql-rollback-live-evidence" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedPgsqlRollbackEvidenceManifestPath $stagingDir "pgsql/pgsql-rollback-live-evidence-manifest.json" "pgsql-rollback-live-evidence-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedWindowsPackageManifestPath $stagingDir "windows/manifest.json" "windows-package-manifest" $manifestInputs $scanPaths)

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("# Local Release Review")
$summaryLines.Add("")
$summaryLines.Add(('- Generated at: `{0}`' -f ((Get-Date).ToUniversalTime().ToString("o"))))
$summaryLines.Add(('- Release head: `{0}`' -f $ReleaseHead))
$summaryLines.Add(('- GitHub Windows Build policy: `{0}`' -f (Format-Value $gitHubWindowsBuildPolicy)))
$summaryLines.Add(('- Review gate: `{0}`' -f (Format-Value $reviewGate)))
$summaryLines.Add(('- Review ready: `{0}`' -f (Format-Value $reviewReady)))
$summaryLines.Add('- Human decision required: `true`')
$summaryLines.Add("")
$summaryLines.Add("## Core Gates")
$summaryLines.Add("")
foreach ($artifact in $artifactSummaries | Where-Object { $_.blocking }) {
    $summaryLines.Add(('- {0}: `present={1}; ready={2}; gate={3}; detail={4}`' -f `
        $artifact.kind, (Format-Value $artifact.present), (Format-Value $artifact.ready), `
        (Format-Value $artifact.releaseGate), (Format-Value $artifact.detail)))
}
$summaryLines.Add("")
$summaryLines.Add("## Supporting Evidence")
$summaryLines.Add("")
foreach ($artifact in $artifactSummaries | Where-Object { -not $_.blocking }) {
    $summaryLines.Add(('- {0}: `present={1}; ready={2}; gate={3}; detail={4}`' -f `
        $artifact.kind, (Format-Value $artifact.present), (Format-Value $artifact.ready), `
        (Format-Value $artifact.releaseGate), (Format-Value $artifact.detail)))
}
$summaryLines.Add("")
$summaryLines.Add("## Final Archive Decision")
$summaryLines.Add("")
$summaryLines.Add(('- Review gate: `{0}`' -f (Format-Value $reviewGate)))
$summaryLines.Add(('- Blocking gate count: `{0}`' -f (Format-Value $blockers.Count)))
$summaryLines.Add(('- Delivery tail count: `{0}`' -f (Format-Value $deliveryTailPending.Count)))
$summaryLines.Add(('- Operator action: `{0}`' -f $operatorAction))
$summaryLines.Add("")
if ($blockers.Count -gt 0) {
    $summaryLines.Add("### Blocking Gates")
    $summaryLines.Add("")
    foreach ($blocker in $blockers) {
        $summaryLines.Add(('- `{0}`' -f $blocker))
    }
    $summaryLines.Add("")
}
$summaryLines.Add("### Delivery Tail")
$summaryLines.Add("")
foreach ($tail in $deliveryTailPending) {
    $summaryLines.Add(('- `{0}`' -f $tail))
}
if ($nonBlockingObservations.Count -gt 0) {
    $summaryLines.Add("")
    $summaryLines.Add("### Non-blocking Observations")
    $summaryLines.Add("")
    foreach ($observation in $nonBlockingObservations) {
        $summaryLines.Add(('- `{0}`' -f $observation))
    }
}
$summaryLines.Add("")
$summaryLines.Add("## Packaged Inputs")
$summaryLines.Add("")
foreach ($input in $manifestInputs) {
    $summaryLines.Add(("- `{0}` as `{1}` (`{2}` bytes)" -f `
        $input.sourceName, $input.packagedAs, $input.bytes))
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($resolvedMarkdownPath, ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $stagingDir "local-release-review.md"), ($summaryLines -join [Environment]::NewLine), $utf8NoBom)

[void]$scanPaths.Add($resolvedMarkdownPath)

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}

if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Local release review contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifest = [ordered]@{
    format = "qtnetworkchat-local-release-review-package-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = ($sensitiveHits.Count -eq 0)
    reviewReady = $reviewReady
    reviewGate = $reviewGate
    packagePath = "local-release-review.zip"
    packageSha256 = "pending"
    targetReleaseHead = $ReleaseHead
    githubWindowsBuildPolicy = $gitHubWindowsBuildPolicy
    stagingDir = "local-release-review"
    markdownPackagedAs = "local-release-review.md"
    manifestPackagedAs = "manifest.json"
    manifestEmbedded = $true
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    artifacts = @($artifactSummaries)
    finalArchiveDecision = [ordered]@{
        ready = $reviewReady
        reviewGate = $reviewGate
        humanDecisionRequired = $true
        blockerCount = $blockers.Count
        blockers = @($blockers.ToArray())
        deliveryTailCount = $deliveryTailPending.Count
        deliveryTailPending = @($deliveryTailPending)
        nonBlockingObservationCount = $nonBlockingObservations.Count
        nonBlockingObservations = @($nonBlockingObservations.ToArray())
        operatorAction = $operatorAction
    }
    sensitiveExportProof = [ordered]@{
        noSensitiveExportProof = ($sensitiveHits.Count -eq 0)
        credentialsExported = $false
        tokensExported = $false
        privateMaterialExported = $false
        plaintextBytesExported = $false
        ciphertextBytesExported = $false
    }
    sensitiveHits = @($sensitiveHits.ToArray())
}

$embeddedManifestPath = Join-Path $stagingDir "manifest.json"
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $embeddedManifestPath -Encoding UTF8

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}
Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

$manifest.packageSha256 = Get-Sha256Hex $resolvedPackagePath
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedManifestPath -Encoding UTF8
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $embeddedManifestPath -Encoding UTF8

Write-Host "local release review package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
Write-Host ("  review gate: {0}" -f $reviewGate)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
