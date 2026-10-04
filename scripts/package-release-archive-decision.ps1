param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$MarkdownPath,
    [string]$ReleaseHead,

    [string]$DecisionState = "pending-human-decision",
    [string]$DecidedBy = "",
    [string]$DecisionReason = "",
    [string]$PublishingStatus = "",
    [string]$PublishingChannel = "",
    [string]$PublishingRecordPath = "",
    [string]$ReleaseDeliveryDrillManifestPath = "",

    [string]$ReadmePath = "README.md",
    [string]$LocalReleaseReviewManifestPath = "build-qt6-mingw\\local-release-review\\local-release-review-manifest.json",
    [string]$ReleaseDeliveryHandoffManifestPath = "build-qt6-mingw\\release-delivery-handoff\\release-delivery-handoff-manifest.json",

    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-script-common.ps1")

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

function Normalize-HeadValue([object]$Value) {
    $text = Format-Value $Value
    if ($text -eq "unknown") {
        return ""
    }
    $text.Trim().ToLowerInvariant()
}

function Test-HeadMatch([string]$Expected, [string]$Actual) {
    $left = Normalize-HeadValue $Expected
    $right = Normalize-HeadValue $Actual
    if ([string]::IsNullOrWhiteSpace($left) -or [string]::IsNullOrWhiteSpace($right)) {
        return $false
    }
    $left -eq $right -or $left.StartsWith($right) -or $right.StartsWith($left)
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
    if (([System.IO.Path]::GetExtension($PathValue)).ToLowerInvariant() -in @(".zip", ".dll", ".exe", ".dmp", ".mdmp")) {
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

function Resolve-OptionalSibling([string]$ManifestPath, [string]$Suffix, [string]$Replacement) {
    $resolvedManifest = Resolve-OptionalPath $ManifestPath
    if ([string]::IsNullOrWhiteSpace($resolvedManifest) -or -not (Test-Path -LiteralPath $resolvedManifest -PathType Leaf)) {
        return ""
    }
    $candidate = $resolvedManifest
    if ($candidate.EndsWith($Suffix, [System.StringComparison]::OrdinalIgnoreCase)) {
        $candidate = $candidate.Substring(0, $candidate.Length - $Suffix.Length) + $Replacement
    } else {
        $candidate = $candidate + $Replacement
    }
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
    }
    ""
}

function Normalize-PublishingRecord([string]$PathValue) {
    $resolved = Resolve-OptionalPath $PathValue
    if ([string]::IsNullOrWhiteSpace($resolved) -or -not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        return $null
    }
    Read-OptionalJson $resolved
}

function Normalize-DecisionState([string]$Value) {
    $normalized = ([string]$Value).Trim().ToLowerInvariant()
    switch ($normalized) {
        "pending-human-decision" { return "pending-human-decision" }
        "approved-local-archive" { return "approved-local-archive" }
        "approved-for-publishing" { return "approved-for-publishing" }
        "deferred" { return "deferred" }
        "rejected" { return "rejected" }
        default { return "" }
    }
}

function Normalize-PublishingStatus([string]$Value) {
    $normalized = ([string]$Value).Trim().ToLowerInvariant()
    switch ($normalized) {
        "not-started" { return "not-started" }
        "pending-environment-publication" { return "pending-environment-publication" }
        "published-outside-repo" { return "published-outside-repo" }
        "not-required" { return "not-required" }
        default { return "" }
    }
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

$normalizedDecisionState = Normalize-DecisionState $DecisionState
if ([string]::IsNullOrWhiteSpace($normalizedDecisionState)) {
    throw "Unsupported decision state. Use pending-human-decision, approved-local-archive, approved-for-publishing, deferred, or rejected."
}

$decisionRecorded = $normalizedDecisionState -ne "pending-human-decision"
$publishingRequired = $normalizedDecisionState -in @("approved-local-archive", "approved-for-publishing")
$normalizedPublishingStatus = Normalize-PublishingStatus $PublishingStatus
if ([string]::IsNullOrWhiteSpace($normalizedPublishingStatus)) {
    if (-not $decisionRecorded) {
        $normalizedPublishingStatus = "not-started"
    } elseif ($publishingRequired) {
        $normalizedPublishingStatus = "pending-environment-publication"
    } else {
        $normalizedPublishingStatus = "not-required"
    }
}

if ($decisionRecorded) {
    if ([string]::IsNullOrWhiteSpace($DecidedBy)) {
        $DecidedBy = [Environment]::UserName
    }
    if ([string]::IsNullOrWhiteSpace($DecisionReason)) {
        switch ($normalizedDecisionState) {
            "approved-local-archive" { $DecisionReason = "Current-head release evidence was reviewed locally and approved for archive." }
            "approved-for-publishing" { $DecisionReason = "Current-head release evidence was reviewed locally and approved for environment-specific publication." }
            "deferred" { $DecisionReason = "Archive decision was deferred pending an environment-specific follow-up." }
            "rejected" { $DecisionReason = "Archive decision was rejected; do not publish this release bundle." }
        }
    }
} else {
    $DecidedBy = "pending-human-decision"
    if ([string]::IsNullOrWhiteSpace($DecisionReason)) {
        $DecisionReason = "Review the current-head local release review and release delivery handoff artifacts, then record an explicit archive decision."
    }
}

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "release-archive-decision.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "release-archive-decision-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $MarkdownPath = Join-Path $resolvedOutputDir "release-archive-decision.md"
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath
$resolvedReadmePath = Resolve-RepoPath $ReadmePath
$resolvedLocalReleaseReviewManifestPath = Resolve-RepoPath $LocalReleaseReviewManifestPath
$resolvedReleaseDeliveryHandoffManifestPath = Resolve-RepoPath $ReleaseDeliveryHandoffManifestPath
$resolvedPublishingRecordPath = Resolve-RepoPath $PublishingRecordPath
$resolvedReleaseDeliveryDrillManifestPath = Resolve-RepoPath $ReleaseDeliveryDrillManifestPath
$resolvedLocalReleaseReviewMarkdownPath = Resolve-OptionalSibling $resolvedLocalReleaseReviewManifestPath "-manifest.json" ".md"
$resolvedReleaseDeliveryHandoffMarkdownPath = Resolve-OptionalSibling $resolvedReleaseDeliveryHandoffManifestPath "-manifest.json" ".md"

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

$localReleaseReviewManifest = Read-OptionalJson $resolvedLocalReleaseReviewManifestPath
$releaseDeliveryHandoffManifest = Read-OptionalJson $resolvedReleaseDeliveryHandoffManifestPath
$publishingRecordManifest = Normalize-PublishingRecord $resolvedPublishingRecordPath
$releaseDeliveryDrillManifest = Normalize-PublishingRecord $resolvedReleaseDeliveryDrillManifestPath

$blockers = New-Object System.Collections.ArrayList
$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

$localReleaseReviewPresent = $null -ne $localReleaseReviewManifest -and (Get-JsonValue $localReleaseReviewManifest "format" "") -eq "qtnetworkchat-local-release-review-package-v1"
$localReleaseReviewReady = $localReleaseReviewPresent -and [bool](Get-JsonValue $localReleaseReviewManifest "reviewReady" $false)
$localReleaseReviewGate = if ($localReleaseReviewPresent) {
    Format-Value (Get-JsonValue $localReleaseReviewManifest "reviewGate" "unknown")
} else {
    "local-release-review-missing"
}
$localReleaseReviewHead = Format-Value (Get-JsonValue $localReleaseReviewManifest "targetReleaseHead" "unknown")
$localReleaseReviewHeadMatch = if ($localReleaseReviewPresent -and $localReleaseReviewHead -ne "unknown") {
    Test-HeadMatch $ReleaseHead $localReleaseReviewHead
} else {
    $true
}

if (-not $localReleaseReviewPresent) {
    [void]$blockers.Add("local-release-review-missing")
} elseif (-not $localReleaseReviewReady) {
    [void]$blockers.Add("local-release-review-not-ready")
} elseif (-not $localReleaseReviewHeadMatch) {
    [void]$blockers.Add("local-release-review-head-mismatch")
}

$releaseDeliveryHandoffPresent = $null -ne $releaseDeliveryHandoffManifest -and (Get-JsonValue $releaseDeliveryHandoffManifest "format" "") -eq "qtnetworkchat-release-delivery-handoff-v1"
$releaseDeliveryHandoffReady = $releaseDeliveryHandoffPresent -and [bool](Get-JsonValue $releaseDeliveryHandoffManifest "deliveryReady" $false)
$releaseDeliveryHandoffGate = if ($releaseDeliveryHandoffPresent) {
    Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "deliveryGate" "unknown")
} else {
    "release-delivery-handoff-missing"
}
$releaseDeliveryHandoffHead = Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "targetReleaseHead" "unknown")
$releaseDeliveryHandoffHeadMatch = if ($releaseDeliveryHandoffPresent -and $releaseDeliveryHandoffHead -ne "unknown") {
    Test-HeadMatch $ReleaseHead $releaseDeliveryHandoffHead
} else {
    $true
}

if (-not $releaseDeliveryHandoffPresent) {
    [void]$blockers.Add("release-delivery-handoff-missing")
} elseif (-not $releaseDeliveryHandoffReady) {
    [void]$blockers.Add("release-delivery-handoff-not-ready")
} elseif (-not $releaseDeliveryHandoffHeadMatch) {
    [void]$blockers.Add("release-delivery-handoff-head-mismatch")
}

$decisionGate = "blocked-release-archive-decision"
if ($blockers.Count -eq 0) {
    if (-not $decisionRecorded) {
        $decisionGate = "ready-for-archive-decision-record"
    } else {
        switch ($normalizedDecisionState) {
            "approved-local-archive" {
                if ($normalizedPublishingStatus -eq "published-outside-repo") {
                    $decisionGate = "archive-decision-recorded-and-published"
                } else {
                    $decisionGate = "archive-decision-recorded-publication-pending"
                }
            }
            "approved-for-publishing" {
                if ($normalizedPublishingStatus -eq "published-outside-repo") {
                    $decisionGate = "archive-decision-recorded-and-published"
                } else {
                    $decisionGate = "archive-decision-recorded-publication-pending"
                }
            }
            "deferred" { $decisionGate = "archive-decision-recorded-deferred" }
            "rejected" { $decisionGate = "archive-decision-recorded-rejected" }
        }
    }
}

$operatorAction = switch ($decisionGate) {
    "ready-for-archive-decision-record" {
        "Review the local release review and release delivery handoff manifests, then rerun this script with an explicit archive decision state."
    }
    "archive-decision-recorded-publication-pending" {
        "The local archive decision is recorded and the repository-side closeout stays complete. Publish the already-generated release bundle through the environment-specific channel outside this repository when ready."
    }
    "archive-decision-recorded-and-published" {
        "The local archive decision and external publication handoff are both recorded. Preserve this manifest with the release evidence bundle."
    }
    "archive-decision-recorded-deferred" {
        "The archive decision is deferred. Track the follow-up externally and refresh this manifest when a final decision is available."
    }
    "archive-decision-recorded-rejected" {
        "The archive decision is rejected. Do not publish this release bundle; refresh only after a replacement head is reviewed."
    }
    default {
        "Do not record or publish a final archive decision yet; regenerate the local release review and release delivery handoff artifacts until the current-head local blockers clear."
    }
}

$generatedAt = (Get-Date).ToUniversalTime().ToString("o")
$publishingChannelValue = if ([string]::IsNullOrWhiteSpace($PublishingChannel)) {
    "not-recorded"
} else {
    $PublishingChannel
}

$stagingDir = Join-Path $resolvedOutputDir "release-archive-decision"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

[void](Copy-EvidenceFile $resolvedReadmePath $stagingDir "docs/README.md" "readme" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewManifestPath $stagingDir "release-review/local-release-review-manifest.json" "local-release-review-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewMarkdownPath $stagingDir "release-review/local-release-review.md" "local-release-review-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryHandoffManifestPath $stagingDir "release-delivery/release-delivery-handoff-manifest.json" "release-delivery-handoff-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryHandoffMarkdownPath $stagingDir "release-delivery/release-delivery-handoff.md" "release-delivery-handoff-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedPublishingRecordPath $stagingDir "publishing/release-publication-record.json" "release-publication-record" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryDrillManifestPath $stagingDir "delivery-drill/release-delivery-drill-manifest.json" "release-delivery-drill-manifest" $manifestInputs $scanPaths)

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("# Release Archive Decision")
$summaryLines.Add("")
$summaryLines.Add(('- Generated at: `{0}`' -f $generatedAt))
$summaryLines.Add(('- Release head: `{0}`' -f $ReleaseHead))
$summaryLines.Add(('- Decision recorded: `{0}`' -f (Format-Value $decisionRecorded)))
$summaryLines.Add(('- Decision state: `{0}`' -f $normalizedDecisionState))
$summaryLines.Add(('- Decision gate: `{0}`' -f $decisionGate))
$summaryLines.Add(('- Publishing required: `{0}`' -f (Format-Value $publishingRequired)))
$summaryLines.Add(('- Publishing status: `{0}`' -f $normalizedPublishingStatus))
$summaryLines.Add(('- Publishing channel: `{0}`' -f (Format-Value $publishingChannelValue)))
$summaryLines.Add(('- Publishing record: `{0}`' -f $(if ($null -ne $publishingRecordManifest) { "present" } else { "missing" })))
$summaryLines.Add(('- Delivery drill: `{0}`' -f $(if ($null -ne $releaseDeliveryDrillManifest) { "present" } else { "missing" })))
$summaryLines.Add("")
$summaryLines.Add("## Source Gates")
$summaryLines.Add("")
$summaryLines.Add(('- Local release review: `present={0}; ready={1}; gate={2}; headMatch={3}`' -f `
        (Format-Value $localReleaseReviewPresent), (Format-Value $localReleaseReviewReady), `
        $localReleaseReviewGate, (Format-Value $localReleaseReviewHeadMatch)))
$summaryLines.Add(('- Release delivery handoff: `present={0}; ready={1}; gate={2}; headMatch={3}`' -f `
        (Format-Value $releaseDeliveryHandoffPresent), (Format-Value $releaseDeliveryHandoffReady), `
        $releaseDeliveryHandoffGate, (Format-Value $releaseDeliveryHandoffHeadMatch)))
$summaryLines.Add("")
$summaryLines.Add("## Archive Record")
$summaryLines.Add("")
$summaryLines.Add(('- Decided by: `{0}`' -f (Format-Value $DecidedBy)))
$summaryLines.Add(('- Decision reason: `{0}`' -f $DecisionReason))
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
$summaryLines.Add("## Packaged Inputs")
$summaryLines.Add("")
foreach ($input in $manifestInputs) {
    $summaryLines.Add(('- `{0}` as `{1}` (`{2}` bytes)' -f $input.sourceName, $input.packagedAs, $input.bytes))
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($resolvedMarkdownPath, ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $stagingDir "release-archive-decision.md"), ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[void]$scanPaths.Add($resolvedMarkdownPath)

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Release archive decision contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifest = [ordered]@{
    format = "qtnetworkchat-release-archive-decision-v1"
    generatedAt = $generatedAt
    ok = ($sensitiveHits.Count -eq 0)
    decisionRecorded = $decisionRecorded
    decisionState = $normalizedDecisionState
    decisionGate = $decisionGate
    decidedBy = $DecidedBy
    decisionReason = $DecisionReason
    publishingRequired = $publishingRequired
    publishingStatus = $normalizedPublishingStatus
    publishingChannel = $publishingChannelValue
    publishingRecordPresent = $null -ne $publishingRecordManifest
    releaseDeliveryDrillPresent = $null -ne $releaseDeliveryDrillManifest
    operatorAction = $operatorAction
    targetReleaseHead = $ReleaseHead
    packagePath = "release-archive-decision.zip"
    packageSha256 = "pending"
    stagingDir = "release-archive-decision"
    markdownPackagedAs = "release-archive-decision.md"
    manifestPackagedAs = "manifest.json"
    manifestEmbedded = $true
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    blockers = @($blockers.ToArray())
    sourceArtifacts = [ordered]@{
        localReleaseReview = [ordered]@{
            present = $localReleaseReviewPresent
            ready = $localReleaseReviewReady
            reviewGate = $localReleaseReviewGate
            targetReleaseHead = $localReleaseReviewHead
            targetHeadMatches = $localReleaseReviewHeadMatch
            packageSha256 = (Format-Value (Get-JsonValue $localReleaseReviewManifest "packageSha256" "unknown"))
        }
        releaseDeliveryHandoff = [ordered]@{
            present = $releaseDeliveryHandoffPresent
            ready = $releaseDeliveryHandoffReady
            deliveryGate = $releaseDeliveryHandoffGate
            targetReleaseHead = $releaseDeliveryHandoffHead
            targetHeadMatches = $releaseDeliveryHandoffHeadMatch
            packageSha256 = (Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "packageSha256" "unknown"))
            deliveryTailCount = (Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "deliveryTailCount" "unknown"))
        }
        publishingRecord = [ordered]@{
            present = ($null -ne $publishingRecordManifest)
            format = (Format-Value (Get-JsonValue $publishingRecordManifest "format" "unknown"))
            publishingStatus = (Format-Value (Get-JsonValue $publishingRecordManifest "publishingStatus" "unknown"))
            channel = (Format-Value (Get-JsonValue $publishingRecordManifest "channel" "unknown"))
        }
        releaseDeliveryDrill = [ordered]@{
            present = ($null -ne $releaseDeliveryDrillManifest)
            format = (Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "format" "unknown"))
            ok = (Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "ok" "unknown"))
        }
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

Write-Host "release archive decision package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
Write-Host ("  decision gate: {0}" -f $decisionGate)
