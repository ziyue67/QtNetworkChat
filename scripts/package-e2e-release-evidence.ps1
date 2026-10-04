param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$PromotionPath,
    [string]$RolloutJsonPath,
    [string]$RolloutMarkdownPath,
    [string]$GitHubWindowsBuildStatusPath,
    [string]$LocalVerificationStatusPath,
    [string]$AutomationStatusPath,
    [string]$ReleaseHead,
    [string]$GitHubWindowsBuildPolicy = "",
    [string]$AutomationPolicyPath = "",

    [switch]$FailOnSensitive,
    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-script-common.ps1")

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



function Get-JsonValue([object]$ObjectValue, [string]$Name, [object]$DefaultValue = $null) {
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    if ($ObjectValue.PSObject.Properties.Name -contains $Name) {
        return $ObjectValue.$Name
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
    $result = [ordered]@{
        configured = $false
        readable = $false
        valid = $false
        githubWindowsBuildPolicy = ""
        source = "automation-policy"
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

function Normalize-HeadValue([object]$Value) {
    $text = Format-Value $Value
    if ($text -eq "unknown") {
        return ""
    }
    $text.Trim().ToLowerInvariant()
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

function Normalize-TextValue([object]$Value) {
    $text = Format-Value $Value
    if ($text -eq "unknown") {
        return ""
    }
    $text.Trim()
}

function Test-TextMatch([string]$Expected, [string]$Actual) {
    $normalizedExpected = Normalize-TextValue $Expected
    $normalizedActual = Normalize-TextValue $Actual
    if ([string]::IsNullOrWhiteSpace($normalizedExpected) -or [string]::IsNullOrWhiteSpace($normalizedActual)) {
        return $false
    }
    $normalizedExpected -ceq $normalizedActual
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

function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
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
    [string]$Kind,
    [System.Collections.ArrayList]$ManifestInputs,
    [System.Collections.ArrayList]$ScanPaths
) {
    $resolvedSource = Resolve-OptionalPath $SourcePath
    if ([string]::IsNullOrWhiteSpace($resolvedSource) -or -not (Test-Path -LiteralPath $resolvedSource -PathType Leaf)) {
        return
    }
    $targetName = Split-Path -Leaf $resolvedSource
    $targetPath = Join-Path $TargetDir $targetName
    Copy-Item -LiteralPath $resolvedSource -Destination $targetPath -Force
    [void]$ScanPaths.Add($resolvedSource)
    [void]$ManifestInputs.Add([pscustomobject]@{
        kind = $Kind
        sourceName = Split-Path -Leaf $resolvedSource
        packagedAs = $targetName
        bytes = (Get-Item -LiteralPath $resolvedSource).Length
        sha256 = Get-Sha256Hex $resolvedSource
    })
}

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "e2e-release-evidence.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "e2e-release-evidence-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($PromotionPath)) {
    $PromotionPath = Join-Path $resolvedOutputDir "e2e-release-promotion.json"
}
if ([string]::IsNullOrWhiteSpace($AutomationPolicyPath)) {
    $defaultAutomationPolicyPath = "docs\automation-policy.json"
    $resolvedDefaultAutomationPolicyPath = Resolve-RepoPath $defaultAutomationPolicyPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedDefaultAutomationPolicyPath) `
            -and (Test-Path -LiteralPath $resolvedDefaultAutomationPolicyPath -PathType Leaf)) {
        $AutomationPolicyPath = $resolvedDefaultAutomationPolicyPath
    }
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$resolvedPromotionPath = Resolve-OptionalPath $PromotionPath
$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}
$manifestParent = Split-Path -Parent $resolvedManifestPath
if (-not [string]::IsNullOrWhiteSpace($manifestParent)) {
    New-Item -ItemType Directory -Path $manifestParent -Force | Out-Null
}
$promotionParent = Split-Path -Parent $resolvedPromotionPath
if (-not [string]::IsNullOrWhiteSpace($promotionParent)) {
    New-Item -ItemType Directory -Path $promotionParent -Force | Out-Null
}

$stagingDir = Join-Path $resolvedOutputDir "e2e-release-evidence"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList
Copy-EvidenceFile $RolloutJsonPath $stagingDir "rollout-observability-json" $manifestInputs $scanPaths
Copy-EvidenceFile $RolloutMarkdownPath $stagingDir "rollout-observability-markdown" $manifestInputs $scanPaths
Copy-EvidenceFile $GitHubWindowsBuildStatusPath $stagingDir "github-windows-build-status" $manifestInputs $scanPaths
Copy-EvidenceFile $LocalVerificationStatusPath $stagingDir "local-verification-status" $manifestInputs $scanPaths
Copy-EvidenceFile $AutomationStatusPath $stagingDir "automation-status-markdown" $manifestInputs $scanPaths

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}

if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("E2E release evidence contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$rollout = Read-OptionalJson $RolloutJsonPath
$ci = Read-OptionalJson $GitHubWindowsBuildStatusPath
$local = Read-OptionalJson $LocalVerificationStatusPath
$automationPolicyReadback = Get-AutomationPolicyReadback $AutomationPolicyPath
$gitHubWindowsBuildPolicyResolved = Normalize-GitHubWindowsBuildPolicy $GitHubWindowsBuildPolicy
if ([string]::IsNullOrWhiteSpace($gitHubWindowsBuildPolicyResolved)) {
    if ($automationPolicyReadback.valid) {
        $gitHubWindowsBuildPolicyResolved = $automationPolicyReadback.githubWindowsBuildPolicy
    } else {
        $gitHubWindowsBuildPolicyResolved = "required"
    }
}
$gitHubWindowsBuildPolicyDisabled = $gitHubWindowsBuildPolicyResolved -eq "disabled"

$rolloutAudit = Get-JsonValue $rollout "auditSummary" $null
$rolloutSummary = Get-JsonValue $rollout "summary" $null
$productionAcceptance = Get-JsonValue $rollout "productionAcceptanceSummary" $null
$productionRollout = Get-JsonValue $rollout "productionRolloutObservability" $null
$releaseRun = Get-JsonValue $rollout "releaseRun" $null
$rolloutProof = Get-JsonValue $rollout "sensitiveExportProof" $null
$localBuild = Get-JsonValue $local "build" $null
$localCTest = Get-JsonValue $local "ctest" $null

$rolloutOk = [bool](Get-JsonValue $rollout "ok" $false)
$rolloutStatus = Format-Value (Get-JsonValue $rollout "status" "missing")
$rolloutGate = Format-Value (Get-JsonValue $rolloutAudit "releaseGate" "missing-rollout-observability")
$productionAcceptanceAccepted = [bool](Get-JsonValue $productionAcceptance "accepted" $false)
$productionAcceptanceLinked = [bool](Get-JsonValue $productionAcceptance "linked" $false)
$productionAcceptanceReady = [bool](Get-JsonValue $productionAcceptance "productionReady" $false)
$productionAcceptanceGate = Format-Value (Get-JsonValue $productionAcceptance "releaseGate" "unknown")
$productionAcceptanceBackendId = Format-Value (Get-JsonValue $productionAcceptance "backendId" "unknown")
$productionAcceptanceProviderId = Format-Value (Get-JsonValue $productionAcceptance "providerId" "unknown")
$productionAcceptanceRequiredCount = [int](Get-JsonValue $productionAcceptance "requiredOperationCount" 0)
$productionAcceptanceAvailableCount = [int](Get-JsonValue $productionAcceptance "availableOperationCount" 0)
$productionRolloutAccepted = [bool](Get-JsonValue $productionRollout "accepted" $false)
$productionRolloutLinked = [bool](Get-JsonValue $productionRollout "linked" $false)
$productionRolloutReady = [bool](Get-JsonValue $productionRollout "productionReady" $false)
$productionRolloutGate = Format-Value (Get-JsonValue $productionRollout "releaseGate" "unknown")
$productionRolloutBackendId = Format-Value (Get-JsonValue $productionRollout "backendId" "unknown")
$productionReleaseRunObservable = [bool](Get-JsonValue $productionRollout "releaseRunObservable" $false)
$productionRolloutNoSensitive = [bool](Get-JsonValue $productionRollout "noSensitiveExportProof" $false)
$productionRolloutRequiredCount = [int](Get-JsonValue $productionRollout "requiredOperationCount" 0)
$productionPublicPrimitiveReadyCount = [int](Get-JsonValue $productionRollout "publicPrimitiveReadyCount" 0)
$productionMaterialExportProofCount = [int](Get-JsonValue $productionRollout "materialExportProofCount" 0)
$productionOutputShapeProofCount = [int](Get-JsonValue $productionRollout "outputShapeProofCount" 0)
$releaseRunPersisted = [bool](Get-JsonValue $releaseRun "persisted" $false)
$releaseRunProductionRequired = [bool](Get-JsonValue $releaseRun "productionRequired" $false)
$releaseRunRequestedBackendId = Format-Value (Get-JsonValue $releaseRun "requestedBackendId" "unknown")
$releaseRunSelectedBackendId = Format-Value (Get-JsonValue $releaseRun "selectedBackendId" "unknown")
$rolloutNoSensitive = [bool](Get-JsonValue $rolloutProof "noSensitiveExportProof" $false)
$rolloutSensitiveSuppressed = [bool](Get-JsonValue $rolloutProof "sensitiveFieldsSuppressed" $false)
$sensitiveMaterialExported = [bool](Get-JsonValue $rolloutProof "rawKeyExported" $false) `
    -or [bool](Get-JsonValue $rolloutProof "privateMaterialExported" $false) `
    -or [bool](Get-JsonValue $rolloutProof "sessionSecretExported" $false) `
    -or [bool](Get-JsonValue $rolloutProof "plaintextBytesExported" $false) `
    -or [bool](Get-JsonValue $rolloutProof "ciphertextBytesExported" $false) `
    -or [bool](Get-JsonValue $productionRollout "rawKeyExported" $false) `
    -or [bool](Get-JsonValue $productionRollout "privateMaterialExported" $false) `
    -or [bool](Get-JsonValue $productionRollout "sessionSecretExported" $false) `
    -or [bool](Get-JsonValue $productionRollout "plaintextBytesExported" $false) `
    -or [bool](Get-JsonValue $productionRollout "ciphertextBytesExported" $false)
$ciStatus = Format-Value (Get-JsonValue $ci "status" "missing")
$ciVisibility = Format-Value (Get-JsonValue $ci "visibility" "missing")
$ciRunId = Format-Value (Get-JsonValue $ci "runId" "unknown")
$ciSource = Format-Value (Get-JsonValue $ci "source" "unknown")
$ciHeadSha = Format-Value (Get-JsonValue $ci "headSha" "unknown")
$ciCurrentHeadObserved = [bool](Get-JsonValue $ci "currentHeadObserved" $false)
$ciExternalBlocker = Format-Value (Get-JsonValue $ci "externalBlocker" "unknown")
$ciReleaseGate = Format-Value (Get-JsonValue $ci "releaseGate" "unknown")
$ciLatestObserved = Get-JsonValue $ci "latestObserved" $null
$ciLatestObservedHead = Format-Value (Get-JsonValue $ciLatestObserved "headSha" "unknown")
$localOk = [bool](Get-JsonValue $local "ok" $false)
$localBuildStatus = Format-Value (Get-JsonValue $localBuild "status" "missing")
$localCTestStatus = Format-Value (Get-JsonValue $localCTest "status" "missing")
$localCTestCount = [int](Get-JsonValue $localCTest "count" 0)

$rolloutArtifactPresent = $null -ne $rollout
$ciArtifactPresent = $null -ne $ci
$localArtifactPresent = $null -ne $local
$releaseHeadConfigured = -not [string]::IsNullOrWhiteSpace((Normalize-HeadValue $ReleaseHead))
$targetReleaseHead = if ($releaseHeadConfigured) { Format-Value $ReleaseHead } else { "unknown" }
$ciHeadMatchesReleaseHead = -not $releaseHeadConfigured -or ($ciArtifactPresent -and (Test-HeadMatch $targetReleaseHead $ciHeadSha))
$probeFixture = $ciSource -eq "probe-fixture" `
    -or (Normalize-HeadValue $targetReleaseHead) -eq "production-probe-head" `
    -or (Normalize-HeadValue $ciHeadSha) -eq "production-probe-head"
$releaseEligible = -not $probeFixture
$releaseEligibilityGate = if ($probeFixture) {
    "not-release-eligible-probe-fixture"
} else {
    "release-eligible-current-head"
}
$acceptedBackendMatchesRollout = Test-TextMatch $productionAcceptanceBackendId $productionRolloutBackendId
$requestedBackendMatchesAcceptance = Test-TextMatch $productionAcceptanceBackendId $releaseRunRequestedBackendId
$selectedBackendMatchesAcceptance = Test-TextMatch $productionAcceptanceBackendId $releaseRunSelectedBackendId
$productionOperationCountsReady = $productionAcceptanceRequiredCount -gt 0 `
    -and $productionAcceptanceAvailableCount -ge $productionAcceptanceRequiredCount `
    -and $productionRolloutRequiredCount -ge $productionAcceptanceRequiredCount `
    -and $productionPublicPrimitiveReadyCount -ge $productionAcceptanceRequiredCount `
    -and $productionMaterialExportProofCount -ge $productionAcceptanceRequiredCount `
    -and $productionOutputShapeProofCount -ge $productionAcceptanceRequiredCount
$productionLinkedBlockers = New-Object System.Collections.ArrayList
if (-not $rolloutOk) { [void]$productionLinkedBlockers.Add("rollout-not-ready") }
if (-not $productionAcceptanceAccepted) { [void]$productionLinkedBlockers.Add("production-acceptance-not-accepted") }
if (-not $productionAcceptanceLinked) { [void]$productionLinkedBlockers.Add("production-acceptance-not-linked") }
if (-not $productionAcceptanceReady) { [void]$productionLinkedBlockers.Add("production-acceptance-not-ready") }
if ($productionAcceptanceGate -ne "production-crypto-accepted") { [void]$productionLinkedBlockers.Add("production-acceptance-gate-not-accepted") }
if (-not $productionRolloutAccepted) { [void]$productionLinkedBlockers.Add("production-rollout-not-accepted") }
if (-not $productionRolloutLinked) { [void]$productionLinkedBlockers.Add("production-rollout-not-linked") }
if (-not $productionRolloutReady) { [void]$productionLinkedBlockers.Add("production-rollout-not-ready") }
if ($productionRolloutGate -ne "production-rollout-observability-ready") { [void]$productionLinkedBlockers.Add("production-rollout-gate-not-ready") }
if (-not $productionReleaseRunObservable) { [void]$productionLinkedBlockers.Add("production-release-run-not-observable") }
if (-not $releaseRunPersisted) { [void]$productionLinkedBlockers.Add("release-run-not-persisted") }
if (-not $releaseRunProductionRequired) { [void]$productionLinkedBlockers.Add("release-run-not-production-required") }
if (-not $acceptedBackendMatchesRollout) { [void]$productionLinkedBlockers.Add("production-backend-mismatch") }
if (-not $requestedBackendMatchesAcceptance) { [void]$productionLinkedBlockers.Add("release-run-requested-backend-mismatch") }
if (-not $selectedBackendMatchesAcceptance) { [void]$productionLinkedBlockers.Add("release-run-backend-mismatch") }
if (-not $productionOperationCountsReady) { [void]$productionLinkedBlockers.Add("production-operation-counts-not-ready") }
if (-not $productionRolloutNoSensitive -or -not $rolloutNoSensitive -or $sensitiveMaterialExported) { [void]$productionLinkedBlockers.Add("production-no-sensitive-proof-missing") }
$productionLinkedEvidenceReady = $rolloutArtifactPresent -and $productionLinkedBlockers.Count -eq 0
$productionLinkedGate = if ($productionLinkedEvidenceReady) {
    "production-linked-rollout-ready"
} else {
    "blocked-production-linked-rollout-not-ready"
}
$packageOk = $sensitiveHits.Count -eq 0 `
    -and $rolloutArtifactPresent `
    -and $localArtifactPresent
$releaseReady = $packageOk `
    -and $releaseEligible `
    -and $productionLinkedEvidenceReady `
    -and $localOk

if ($gitHubWindowsBuildPolicyDisabled) {
    $ciStatus = "disabled-by-policy"
    $ciVisibility = "not-required"
    $ciRunId = "not-required"
    $ciSource = "automation-policy"
    $ciCurrentHeadObserved = "not-required"
    $ciExternalBlocker = "waived-by-policy"
    $ciReleaseGate = "not-required"
    $ciLatestObservedHead = "not-required"
    $ciHeadSha = "not-required"
    $ciHeadMatchesReleaseHead = "not-required"
}

$releaseGate = if ($sensitiveHits.Count -gt 0) {
    "blocked-sensitive-evidence"
} elseif (-not $rolloutArtifactPresent) {
    "blocked-missing-rollout-observability"
} elseif (-not $localArtifactPresent) {
    "blocked-missing-local-verification-status"
} elseif (-not $releaseEligible) {
    "blocked-release-artifact-probe-fixture"
} elseif (-not $localOk) {
    "blocked-local-verification"
} elseif (-not $productionLinkedEvidenceReady) {
    $productionLinkedGate
} else {
    "ready-local-verification-only"
}

$promotionBlockers = New-Object System.Collections.ArrayList
if ($sensitiveHits.Count -gt 0) {
    [void]$promotionBlockers.Add("sensitive-evidence")
}
if (-not $releaseEligible) {
    [void]$promotionBlockers.Add("release-artifact-probe-fixture")
}
if (-not $rolloutArtifactPresent) {
    [void]$promotionBlockers.Add("missing-rollout-observability")
} else {
    if (-not $rolloutOk) {
        [void]$promotionBlockers.Add("rollout-not-ready")
    }
    if (-not $productionLinkedEvidenceReady) {
        [void]$promotionBlockers.Add("production-linked-rollout-not-ready")
    }
}
if (-not $localArtifactPresent) {
    [void]$promotionBlockers.Add("missing-local-verification-status")
} elseif (-not $localOk) {
    [void]$promotionBlockers.Add("local-verification-not-ready")
}

$promotionReady = $releaseReady `
    -and $releaseEligible `
    -and $productionLinkedEvidenceReady `
    -and $promotionBlockers.Count -eq 0
$promotionGate = if ($promotionReady) {
    "ready-local-verification-only"
} else {
    "blocked-e2e-release-artifact-promotion"
}

$manifest = [ordered]@{
    format = "qtnetworkchat-e2e-release-evidence-package-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = $packageOk
    releaseReady = $releaseReady
    releaseGate = $releaseGate
    packagePath = Split-Path -Leaf $resolvedPackagePath
    packageSha256 = "pending"
    releaseHeadConfigured = $releaseHeadConfigured
    targetReleaseHead = $targetReleaseHead
    probeFixture = $probeFixture
    releaseEligible = $releaseEligible
    releaseEligibilityGate = $releaseEligibilityGate
    stagingDir = Split-Path -Leaf $stagingDir
    manifestPackagedAs = "manifest.json"
    manifestEmbedded = $true
    promotionPackagedAs = Split-Path -Leaf $resolvedPromotionPath
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    rollout = [ordered]@{
        present = $rolloutArtifactPresent
        status = $rolloutStatus
        ok = $rolloutOk
        readiness = Format-Value (Get-JsonValue $rolloutSummary "readiness" "unknown")
        releaseGate = $rolloutGate
    }
    productionLinkedEvidence = [ordered]@{
        ready = $productionLinkedEvidenceReady
        releaseGate = $productionLinkedGate
        blockers = @($productionLinkedBlockers.ToArray())
        acceptanceAccepted = $productionAcceptanceAccepted
        acceptanceLinked = $productionAcceptanceLinked
        acceptanceProductionReady = $productionAcceptanceReady
        acceptanceReleaseGate = $productionAcceptanceGate
        acceptanceBackendId = $productionAcceptanceBackendId
        acceptanceProviderId = $productionAcceptanceProviderId
        acceptanceRequiredOperationCount = $productionAcceptanceRequiredCount
        acceptanceAvailableOperationCount = $productionAcceptanceAvailableCount
        rolloutAccepted = $productionRolloutAccepted
        rolloutLinked = $productionRolloutLinked
        rolloutProductionReady = $productionRolloutReady
        rolloutReleaseGate = $productionRolloutGate
        rolloutBackendId = $productionRolloutBackendId
        releaseRunObservable = $productionReleaseRunObservable
        releaseRunPersisted = $releaseRunPersisted
        releaseRunProductionRequired = $releaseRunProductionRequired
        releaseRunRequestedBackendId = $releaseRunRequestedBackendId
        releaseRunSelectedBackendId = $releaseRunSelectedBackendId
        acceptedBackendMatchesRollout = $acceptedBackendMatchesRollout
        requestedBackendMatchesAcceptance = $requestedBackendMatchesAcceptance
        selectedBackendMatchesAcceptance = $selectedBackendMatchesAcceptance
        operationCountsReady = $productionOperationCountsReady
        requiredOperationCount = $productionAcceptanceRequiredCount
        publicPrimitiveReadyCount = $productionPublicPrimitiveReadyCount
        materialExportProofCount = $productionMaterialExportProofCount
        outputShapeProofCount = $productionOutputShapeProofCount
        rolloutNoSensitiveExportProof = $productionRolloutNoSensitive
        artifactNoSensitiveExportProof = $rolloutNoSensitive
        sensitiveFieldsSuppressed = $rolloutSensitiveSuppressed
        sensitiveMaterialExported = $sensitiveMaterialExported
    }
    ci = [ordered]@{
        present = $ciArtifactPresent
        status = $ciStatus
        headSha = $ciHeadSha
        runId = $ciRunId
        source = $ciSource
        visibility = $ciVisibility
        observedRunCount = [int](Get-JsonValue $ci "observedRunCount" 0)
        currentHeadObserved = $ciCurrentHeadObserved
        headMatchesReleaseHead = $ciHeadMatchesReleaseHead
        externalBlocker = $ciExternalBlocker
        releaseGate = $ciReleaseGate
        latestObservedHead = $ciLatestObservedHead
        probeFixture = $probeFixture
    }
    localVerification = [ordered]@{
        present = $localArtifactPresent
        ok = $localOk
        buildStatus = $localBuildStatus
        ctestStatus = $localCTestStatus
        ctestCount = $localCTestCount
    }
    sensitiveExportProof = [ordered]@{
        noSensitiveExportProof = $sensitiveHits.Count -eq 0
        logsExported = $false
        annotationsExported = $false
        tokensExported = $false
        credentialsExported = $false
        ciphertextBytesExported = $false
        plaintextBytesExported = $false
        privateMaterialExported = $false
    }
    promotion = [ordered]@{
        format = "qtnetworkchat-e2e-release-artifact-promotion-v1"
        promoted = $promotionReady
        promotionReady = $promotionReady
        releaseGate = $promotionGate
        evidenceReleaseGate = $releaseGate
        packagePath = Split-Path -Leaf $resolvedPackagePath
        packageSha256 = "pending"
        manifestPath = Split-Path -Leaf $resolvedManifestPath
        targetReleaseHead = $targetReleaseHead
        ciHeadSha = $ciHeadSha
        currentHeadObserved = $ciCurrentHeadObserved
        ciHeadMatchesReleaseHead = $ciHeadMatchesReleaseHead
        ciStatus = $ciStatus
        probeFixture = $probeFixture
        releaseEligible = $releaseEligible
        releaseEligibilityGate = $releaseEligibilityGate
        productionLinkedEvidenceReady = $productionLinkedEvidenceReady
        productionLinkedReleaseGate = $productionLinkedGate
        localVerificationOk = $localOk
        rolloutOk = $rolloutOk
        noSensitiveExportProof = $sensitiveHits.Count -eq 0
        blockers = @($promotionBlockers.ToArray())
        operatorAction = if ($promotionReady) {
            if ($gitHubWindowsBuildPolicyDisabled) {
                "GitHub Windows Build is disabled by repo policy; use local build/CTest and linked production evidence for release review."
            } else {
                "Promote the packaged E2E release artifact using the recorded package SHA-256 and attached evidence."
            }
        } else {
            if ($gitHubWindowsBuildPolicyDisabled) {
                "Do not promote the E2E release artifact; resolve local verification or production-linked evidence blockers and regenerate this promotion decision."
            } else {
                "Do not promote the E2E release artifact; resolve blockers and regenerate this promotion decision."
            }
        }
    }
    sensitiveHits = @($sensitiveHits.ToArray())
}

$stagingManifestPath = Join-Path $stagingDir "manifest.json"
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $stagingManifestPath -Encoding UTF8

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}
Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

$manifest.packageSha256 = Get-Sha256Hex $resolvedPackagePath
$manifest.promotion.packageSha256 = $manifest.packageSha256
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resolvedManifestPath -Encoding UTF8
$manifest.promotion | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resolvedPromotionPath -Encoding UTF8

Write-Host "e2e release evidence package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  promotion: {0}" -f $resolvedPromotionPath)
Write-Host ("  release gate: {0}" -f $releaseGate)
Write-Host ("  promotion gate: {0}" -f $promotionGate)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
