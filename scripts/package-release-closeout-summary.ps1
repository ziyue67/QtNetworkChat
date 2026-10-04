param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$MarkdownPath,
    [string]$ReleaseHead,
    [string]$BuildDir = "build-qt6-mingw",
    [string]$ReadmePath = "README.md",
    [string]$AutomationStatusPath = "docs\\automation-status.md",
    [string]$E2EHardeningStatusPath = "docs\\e2e-hardening-status.md",
    [string]$LocalReleaseReviewManifestPath = "build-qt6-mingw\\local-release-review\\local-release-review-manifest.json",
    [string]$LocalReleaseReviewMarkdownPath = "build-qt6-mingw\\local-release-review\\local-release-review.md",
    [string]$LocalReleaseReviewPackagePath = "build-qt6-mingw\\local-release-review\\local-release-review.zip",
    [string]$ReleaseArchiveDecisionManifestPath = "build-qt6-mingw\\release-archive-decision\\release-archive-decision-manifest.json",
    [string]$ReleaseArchiveDecisionMarkdownPath = "build-qt6-mingw\\release-archive-decision\\release-archive-decision.md",
    [string]$ReleaseArchiveDecisionPackagePath = "build-qt6-mingw\\release-archive-decision\\release-archive-decision.zip",
    [string]$ReleaseDeliveryHandoffManifestPath = "build-qt6-mingw\\release-delivery-handoff\\release-delivery-handoff-manifest.json",
    [string]$ReleaseDeliveryHandoffMarkdownPath = "build-qt6-mingw\\release-delivery-handoff\\release-delivery-handoff.md",
    [string]$ReleaseDeliveryHandoffPackagePath = "build-qt6-mingw\\release-delivery-handoff\\release-delivery-handoff.zip",
    [string]$ReleasePublicationRecordPath = "build-qt6-mingw\\release-publication-record.json",
    [string]$ReleaseDeliveryDrillManifestPath = "build-qt6-mingw\\release-delivery-drill\\release-delivery-drill-manifest.json",
    [string]$ReleaseDeliveryDrillMarkdownPath = "build-qt6-mingw\\release-delivery-drill\\release-delivery-drill.md",
    [string]$ReleaseDiagnosticsManifestPath = "build-qt6-mingw\\release-delivery-drill\\release-diagnostics\\release-diagnostics\\manifest.json",
    [string]$ReleaseDiagnosticsPackagePath = "build-qt6-mingw\\release-delivery-drill\\release-diagnostics\\release-diagnostics.zip",

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

function New-StatusLine([string]$Label, [bool]$Present, [string]$Gate, [string]$Detail) {
    '- {0}: `present={1}; gate={2}; detail={3}`' -f $Label, (Format-Value $Present), (Format-Value $Gate), (Format-Value $Detail)
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

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $resolvedOutputDir "release-closeout-summary.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "release-closeout-summary-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $MarkdownPath = Join-Path $resolvedOutputDir "release-closeout-summary.md"
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath

$resolvedReadmePath = Resolve-RepoPath $ReadmePath
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath
$resolvedE2EHardeningStatusPath = Resolve-RepoPath $E2EHardeningStatusPath
$resolvedLocalReleaseReviewManifestPath = Resolve-RepoPath $LocalReleaseReviewManifestPath
$resolvedLocalReleaseReviewMarkdownPath = Resolve-RepoPath $LocalReleaseReviewMarkdownPath
$resolvedLocalReleaseReviewPackagePath = Resolve-RepoPath $LocalReleaseReviewPackagePath
$resolvedReleaseArchiveDecisionManifestPath = Resolve-RepoPath $ReleaseArchiveDecisionManifestPath
$resolvedReleaseArchiveDecisionMarkdownPath = Resolve-RepoPath $ReleaseArchiveDecisionMarkdownPath
$resolvedReleaseArchiveDecisionPackagePath = Resolve-RepoPath $ReleaseArchiveDecisionPackagePath
$resolvedReleaseDeliveryHandoffManifestPath = Resolve-RepoPath $ReleaseDeliveryHandoffManifestPath
$resolvedReleaseDeliveryHandoffMarkdownPath = Resolve-RepoPath $ReleaseDeliveryHandoffMarkdownPath
$resolvedReleaseDeliveryHandoffPackagePath = Resolve-RepoPath $ReleaseDeliveryHandoffPackagePath
$resolvedReleasePublicationRecordPath = Resolve-RepoPath $ReleasePublicationRecordPath
$resolvedReleaseDeliveryDrillManifestPath = Resolve-RepoPath $ReleaseDeliveryDrillManifestPath
$resolvedReleaseDeliveryDrillMarkdownPath = Resolve-RepoPath $ReleaseDeliveryDrillMarkdownPath
$resolvedReleaseDiagnosticsManifestPath = Resolve-RepoPath $ReleaseDiagnosticsManifestPath
$resolvedReleaseDiagnosticsPackagePath = Resolve-RepoPath $ReleaseDiagnosticsPackagePath

$localReleaseReviewManifest = Read-OptionalJson $resolvedLocalReleaseReviewManifestPath
$releaseArchiveDecisionManifest = Read-OptionalJson $resolvedReleaseArchiveDecisionManifestPath
$releaseDeliveryHandoffManifest = Read-OptionalJson $resolvedReleaseDeliveryHandoffManifestPath
$releasePublicationRecord = Read-OptionalJson $resolvedReleasePublicationRecordPath
$releaseDeliveryDrillManifest = Read-OptionalJson $resolvedReleaseDeliveryDrillManifestPath
$releaseDiagnosticsManifest = Read-OptionalJson $resolvedReleaseDiagnosticsManifestPath

$localReleaseReviewPresent = $null -ne $localReleaseReviewManifest -and (Get-JsonValue $localReleaseReviewManifest "format" "") -eq "qtnetworkchat-local-release-review-package-v1"
$localReleaseReviewReady = $localReleaseReviewPresent -and [bool](Get-JsonValue $localReleaseReviewManifest "reviewReady" $false)
$localReleaseReviewGate = if ($localReleaseReviewPresent) { Format-Value (Get-JsonValue $localReleaseReviewManifest "reviewGate" "unknown") } else { "missing" }

$archiveDecisionPresent = $null -ne $releaseArchiveDecisionManifest -and (Get-JsonValue $releaseArchiveDecisionManifest "format" "") -eq "qtnetworkchat-release-archive-decision-v1"
$archiveDecisionRecorded = $archiveDecisionPresent -and [bool](Get-JsonValue $releaseArchiveDecisionManifest "decisionRecorded" $false)
$archiveDecisionGate = if ($archiveDecisionPresent) { Format-Value (Get-JsonValue $releaseArchiveDecisionManifest "decisionGate" "unknown") } else { "missing" }
$archiveDecisionState = if ($archiveDecisionPresent) { Format-Value (Get-JsonValue $releaseArchiveDecisionManifest "decisionState" "unknown") } else { "unknown" }
$archivePublishingStatus = if ($archiveDecisionPresent) { Format-Value (Get-JsonValue $releaseArchiveDecisionManifest "publishingStatus" "unknown") } else { "unknown" }

$deliveryHandoffPresent = $null -ne $releaseDeliveryHandoffManifest -and (Get-JsonValue $releaseDeliveryHandoffManifest "format" "") -eq "qtnetworkchat-release-delivery-handoff-v1"
$deliveryHandoffReady = $deliveryHandoffPresent -and [bool](Get-JsonValue $releaseDeliveryHandoffManifest "deliveryReady" $false)
$deliveryHandoffGate = if ($deliveryHandoffPresent) { Format-Value (Get-JsonValue $releaseDeliveryHandoffManifest "deliveryGate" "unknown") } else { "missing" }
$deliveryTailCount = if ($deliveryHandoffPresent) { [int](Get-JsonValue $releaseDeliveryHandoffManifest "deliveryTailCount" 0) } else { -1 }

$publicationRecordPresent = $null -ne $releasePublicationRecord -and (Get-JsonValue $releasePublicationRecord "format" "") -eq "qtnetworkchat-release-publication-record-v1"
$publicationChannel = if ($publicationRecordPresent) { Format-Value (Get-JsonValue $releasePublicationRecord "channel" "unknown") } else { "unknown" }
$publicationStatus = if ($publicationRecordPresent) { Format-Value (Get-JsonValue $releasePublicationRecord "publishingStatus" "unknown") } else { "unknown" }

$deliveryDrillPresent = $null -ne $releaseDeliveryDrillManifest -and (Get-JsonValue $releaseDeliveryDrillManifest "format" "") -eq "qtnetworkchat-release-delivery-drill-v1"
$deliveryDrillOk = $deliveryDrillPresent -and [bool](Get-JsonValue $releaseDeliveryDrillManifest "ok" $false)
$deliveryDrillArchiveDecisionState = if ($deliveryDrillPresent) { Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "archiveDecisionState" "unknown") } else { "unknown" }

$diagnosticsPresent = $null -ne $releaseDiagnosticsManifest -and (Get-JsonValue $releaseDiagnosticsManifest "format" "") -eq "qtnetworkchat-release-diagnostics-package-v1"
$diagnosticsOk = $diagnosticsPresent -and [bool](Get-JsonValue $releaseDiagnosticsManifest "ok" $false)
$diagnosticsDumpCount = if ($diagnosticsPresent) { Format-Value (Get-JsonValue $releaseDiagnosticsManifest "dumpCount" "unknown") } else { "unknown" }
$diagnosticsInputCount = if ($diagnosticsPresent) { Format-Value (Get-JsonValue $releaseDiagnosticsManifest "inputCount" "unknown") } else { "unknown" }

$closeoutReady = $localReleaseReviewReady -and $archiveDecisionRecorded -and $deliveryHandoffReady -and $publicationRecordPresent -and $deliveryDrillOk -and $diagnosticsOk
$closeoutGate = if ($closeoutReady) { "release-closeout-ready-for-stop-writing" } else { "release-closeout-incomplete" }
$operatorAction = if ($closeoutReady) {
    if ($publicationStatus -eq "published-outside-repo") {
        "The local closeout chain is complete and publication has been recorded. Preserve this summary with the release evidence bundle."
    } else {
        "The local closeout chain is complete. Publish the already-generated package through the environment-specific channel when ready, then refresh the publication record if that external action is completed."
    }
} else {
    "Refresh the missing closeout artifacts until the local release review, archive decision, delivery handoff, delivery drill, publication record, and diagnostics package are all present and readable."
}

$stagingDir = Join-Path $resolvedOutputDir "release-closeout-summary"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

[void](Copy-EvidenceFile $resolvedReadmePath $stagingDir "docs/README.md" "readme" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedAutomationStatusPath $stagingDir "docs/automation-status.md" "automation-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedE2EHardeningStatusPath $stagingDir "docs/e2e-hardening-status.md" "e2e-hardening-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewManifestPath $stagingDir "release-review/local-release-review-manifest.json" "local-release-review-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewMarkdownPath $stagingDir "release-review/local-release-review.md" "local-release-review-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewPackagePath $stagingDir "release-review/local-release-review.zip" "local-release-review-package" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseArchiveDecisionManifestPath $stagingDir "archive/release-archive-decision-manifest.json" "release-archive-decision-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseArchiveDecisionMarkdownPath $stagingDir "archive/release-archive-decision.md" "release-archive-decision-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseArchiveDecisionPackagePath $stagingDir "archive/release-archive-decision.zip" "release-archive-decision-package" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryHandoffManifestPath $stagingDir "delivery/release-delivery-handoff-manifest.json" "release-delivery-handoff-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryHandoffMarkdownPath $stagingDir "delivery/release-delivery-handoff.md" "release-delivery-handoff-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryHandoffPackagePath $stagingDir "delivery/release-delivery-handoff.zip" "release-delivery-handoff-package" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleasePublicationRecordPath $stagingDir "publishing/release-publication-record.json" "release-publication-record" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryDrillManifestPath $stagingDir "delivery-drill/release-delivery-drill-manifest.json" "release-delivery-drill-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryDrillMarkdownPath $stagingDir "delivery-drill/release-delivery-drill.md" "release-delivery-drill-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDiagnosticsManifestPath $stagingDir "diagnostics/release-diagnostics-manifest.json" "release-diagnostics-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDiagnosticsPackagePath $stagingDir "diagnostics/release-diagnostics.zip" "release-diagnostics-package" $manifestInputs $scanPaths)

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("# Release Closeout Summary")
$summaryLines.Add("")
$summaryLines.Add(('- Generated at: `{0}`' -f ((Get-Date).ToUniversalTime().ToString("o"))))
$summaryLines.Add(('- Release head: `{0}`' -f $ReleaseHead))
$summaryLines.Add(('- Closeout ready: `{0}`' -f (Format-Value $closeoutReady)))
$summaryLines.Add(('- Closeout gate: `{0}`' -f $closeoutGate))
$summaryLines.Add(('- Operator action: `{0}`' -f $operatorAction))
$summaryLines.Add("")
$summaryLines.Add("## Closeout Chain")
$summaryLines.Add("")
$summaryLines.Add((New-StatusLine "Local release review" $localReleaseReviewPresent $localReleaseReviewGate ('ready={0}' -f (Format-Value $localReleaseReviewReady))))
$summaryLines.Add((New-StatusLine "Release archive decision" $archiveDecisionPresent $archiveDecisionGate ('recorded={0}; state={1}; publishing={2}' -f (Format-Value $archiveDecisionRecorded), $archiveDecisionState, $archivePublishingStatus)))
$summaryLines.Add((New-StatusLine "Release delivery handoff" $deliveryHandoffPresent $deliveryHandoffGate ('ready={0}; deliveryTail={1}' -f (Format-Value $deliveryHandoffReady), (Format-Value $deliveryTailCount))))
$summaryLines.Add((New-StatusLine "Release publication record" $publicationRecordPresent $publicationStatus ('channel={0}' -f $publicationChannel)))
$summaryLines.Add((New-StatusLine "Release delivery drill" $deliveryDrillPresent $(if ($deliveryDrillOk) { "delivery-drill-ok" } else { "delivery-drill-missing-or-failed" }) ('ok={0}; archiveDecisionState={1}' -f (Format-Value $deliveryDrillOk), $deliveryDrillArchiveDecisionState)))
$summaryLines.Add((New-StatusLine "Release diagnostics package" $diagnosticsPresent $(if ($diagnosticsOk) { "release-diagnostics-ok" } else { "release-diagnostics-missing-or-failed" }) ('ok={0}; dumpCount={1}; inputs={2}' -f (Format-Value $diagnosticsOk), $diagnosticsDumpCount, $diagnosticsInputCount)))
$summaryLines.Add("")
$summaryLines.Add("## Interpretation")
$summaryLines.Add("")
$summaryLines.Add("- `release-closeout-ready-for-stop-writing` means the local release review, archive decision, delivery handoff, delivery drill, publication record, and sanitized diagnostics package are all present and internally consistent.")
$summaryLines.Add("- Environment-specific publishing remains an explicit step outside this repository even when the local closeout chain is complete.")
$summaryLines.Add("- The publication record captures only the sanitized handoff state; it does not perform the external upload.")
$summaryLines.Add("")
$summaryLines.Add("## Packaged Inputs")
$summaryLines.Add("")
foreach ($input in $manifestInputs) {
    $summaryLines.Add(('- `{0}` as `{1}` (`{2}` bytes)' -f $input.sourceName, $input.packagedAs, $input.bytes))
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($resolvedMarkdownPath, ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $stagingDir "release-closeout-summary.md"), ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[void]$scanPaths.Add($resolvedMarkdownPath)

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Release closeout summary contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifest = [ordered]@{
    format = "qtnetworkchat-release-closeout-summary-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = ($sensitiveHits.Count -eq 0)
    closeoutReady = $closeoutReady
    closeoutGate = $closeoutGate
    operatorAction = $operatorAction
    releaseHead = $ReleaseHead
    packagePath = "release-closeout-summary.zip"
    packageSha256 = "pending"
    stagingDir = "release-closeout-summary"
    markdownPackagedAs = "release-closeout-summary.md"
    manifestEmbedded = $true
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    localReleaseReview = [ordered]@{
        present = $localReleaseReviewPresent
        ready = $localReleaseReviewReady
        reviewGate = $localReleaseReviewGate
    }
    releaseArchiveDecision = [ordered]@{
        present = $archiveDecisionPresent
        recorded = $archiveDecisionRecorded
        decisionGate = $archiveDecisionGate
        decisionState = $archiveDecisionState
        publishingStatus = $archivePublishingStatus
    }
    releaseDeliveryHandoff = [ordered]@{
        present = $deliveryHandoffPresent
        ready = $deliveryHandoffReady
        deliveryGate = $deliveryHandoffGate
        deliveryTailCount = $deliveryTailCount
    }
    releasePublicationRecord = [ordered]@{
        present = $publicationRecordPresent
        channel = $publicationChannel
        publishingStatus = $publicationStatus
    }
    releaseDeliveryDrill = [ordered]@{
        present = $deliveryDrillPresent
        ok = $deliveryDrillOk
        archiveDecisionState = $deliveryDrillArchiveDecisionState
    }
    releaseDiagnostics = [ordered]@{
        present = $diagnosticsPresent
        ok = $diagnosticsOk
        dumpCount = $diagnosticsDumpCount
        inputCount = $diagnosticsInputCount
    }
    sensitiveExportProof = [ordered]@{
        noSensitiveExportProof = ($sensitiveHits.Count -eq 0)
        credentialsExported = $false
        tokensExported = $false
        privateMaterialExported = $false
        plaintextBytesExported = $false
        ciphertextBytesExported = $false
        crashDumpBytesExported = $false
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

Write-Host "release closeout summary package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
Write-Host ("  closeout gate: {0}" -f $closeoutGate)
