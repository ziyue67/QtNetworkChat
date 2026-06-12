param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$MarkdownPath,
    [string]$ReleaseHead,

    [string]$WindowsPackageManifestPath = "build-qt6-mingw\\release-package\\QtNetworkChat-1.0.0-win-x64\\manifest.json",
    [string]$WindowsPackageZipPath = "",
    [string]$LocalReleaseReviewManifestPath = "build-qt6-mingw\\local-release-review\\local-release-review-manifest.json",
    [string]$LocalReleaseReviewPackagePath = "build-qt6-mingw\\local-release-review\\local-release-review.zip",
    [string]$ReleaseArchiveDecisionManifestPath = "build-qt6-mingw\\release-archive-decision\\release-archive-decision-manifest.json",
    [string]$ReleaseArchiveDecisionMarkdownPath = "build-qt6-mingw\\release-archive-decision\\release-archive-decision.md",
    [string]$ReleasePublicationRecordPath = "build-qt6-mingw\\release-publication-record.json",
    [string]$ReleaseDeliveryDrillManifestPath = "build-qt6-mingw\\release-delivery-drill\\release-delivery-drill-manifest.json",
    [string]$LocalVerificationStatusPath = "build-qt6-mingw\\local-verification-status.json",
    [string]$AutomationStatusPath = "docs\\automation-status.md",
    [string]$ReadmePath = "README.md",
    [string]$InstallerScriptPath = "scripts\\install-qtnetworkchat-package.ps1",
    [string]$DiagnosticsScriptPath = "scripts\\collect-qtnetworkchat-diagnostics.ps1",

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
    $PackagePath = Join-Path $resolvedOutputDir "release-delivery-handoff.zip"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $resolvedOutputDir "release-delivery-handoff-manifest.json"
}
if ([string]::IsNullOrWhiteSpace($MarkdownPath)) {
    $MarkdownPath = Join-Path $resolvedOutputDir "release-delivery-handoff.md"
}

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$resolvedMarkdownPath = Resolve-OptionalPath $MarkdownPath

if ([string]::IsNullOrWhiteSpace($WindowsPackageZipPath)) {
    $resolvedManifestCandidate = Resolve-RepoPath $WindowsPackageManifestPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedManifestCandidate) -and (Test-Path -LiteralPath $resolvedManifestCandidate -PathType Leaf)) {
        $stageDir = Split-Path -Parent $resolvedManifestCandidate
        $packageRoot = Split-Path -Parent $stageDir
        $stageName = Split-Path -Leaf $stageDir
        $WindowsPackageZipPath = Join-Path $packageRoot ($stageName + ".zip")
    }
}

$resolvedWindowsPackageManifestPath = Resolve-WindowsPackageManifestPath $WindowsPackageManifestPath
$resolvedWindowsPackageZipPath = Resolve-RepoPath $WindowsPackageZipPath
$resolvedLocalReleaseReviewManifestPath = Resolve-RepoPath $LocalReleaseReviewManifestPath
$resolvedLocalReleaseReviewPackagePath = Resolve-RepoPath $LocalReleaseReviewPackagePath
$resolvedReleaseArchiveDecisionManifestPath = Resolve-RepoPath $ReleaseArchiveDecisionManifestPath
$resolvedReleaseArchiveDecisionMarkdownPath = Resolve-RepoPath $ReleaseArchiveDecisionMarkdownPath
$resolvedReleasePublicationRecordPath = Resolve-RepoPath $ReleasePublicationRecordPath
$resolvedReleaseDeliveryDrillManifestPath = Resolve-RepoPath $ReleaseDeliveryDrillManifestPath
$resolvedLocalVerificationStatusPath = Resolve-RepoPath $LocalVerificationStatusPath
$resolvedAutomationStatusPath = Resolve-RepoPath $AutomationStatusPath
$resolvedReadmePath = Resolve-RepoPath $ReadmePath
$resolvedInstallerScriptPath = Resolve-RepoPath $InstallerScriptPath
$resolvedDiagnosticsScriptPath = Resolve-RepoPath $DiagnosticsScriptPath

$windowsManifest = Read-OptionalJson $resolvedWindowsPackageManifestPath
$localReleaseReviewManifest = Read-OptionalJson $resolvedLocalReleaseReviewManifestPath
$releaseArchiveDecisionManifest = Read-OptionalJson $resolvedReleaseArchiveDecisionManifestPath
$releasePublicationRecord = Read-OptionalJson $resolvedReleasePublicationRecordPath
$releaseDeliveryDrillManifest = Read-OptionalJson $resolvedReleaseDeliveryDrillManifestPath
$localVerification = Read-OptionalJson $resolvedLocalVerificationStatusPath

$windowsPackagePresent = $null -ne $windowsManifest -and (Get-JsonValue $windowsManifest "packageFormat" "") -eq "qtnetworkchat-windows-package-v1"
$windowsGitCommit = if ($windowsPackagePresent) { Format-Value (Get-JsonValue $windowsManifest "gitCommit" "unknown") } else { "unknown" }
$windowsCurrentHeadMatch = $windowsPackagePresent -and (Test-HeadMatch $ReleaseHead $windowsGitCommit)
$windowsRuntimeOk = $windowsPackagePresent -and [bool](Get-JsonValue (Get-JsonValue $windowsManifest "runtimeCheck" $null) "ok" $false)
$windowsPostgresOk = $windowsPackagePresent -and [bool](Get-JsonValue (Get-JsonValue $windowsManifest "postgresSqlRuntime" $null) "ok" $false)
$windowsZipPresent = -not [string]::IsNullOrWhiteSpace($resolvedWindowsPackageZipPath) -and (Test-Path -LiteralPath $resolvedWindowsPackageZipPath -PathType Leaf)
$windowsPackageReady = $windowsPackagePresent -and $windowsCurrentHeadMatch -and $windowsRuntimeOk -and $windowsZipPresent
$windowsGate = if ($windowsPackageReady) { "windows-package-current-head-ready" } elseif ($windowsPackagePresent -and -not $windowsCurrentHeadMatch) { "windows-package-current-head-missing" } elseif ($windowsPackagePresent -and -not $windowsRuntimeOk) { "windows-package-runtime-incomplete" } else { "windows-package-missing" }

$localReleaseReviewReady = $null -ne $localReleaseReviewManifest `
    -and (Get-JsonValue $localReleaseReviewManifest "format" "") -eq "qtnetworkchat-local-release-review-package-v1" `
    -and [bool](Get-JsonValue $localReleaseReviewManifest "reviewReady" $false)
$localReleaseReviewGate = if ($localReleaseReviewReady) {
    Format-Value (Get-JsonValue $localReleaseReviewManifest "reviewGate" "unknown")
} else {
    "local-release-review-missing"
}

$localVerificationReady = $null -ne $localVerification `
    -and (Get-JsonValue $localVerification "format" "") -eq "qtnetworkchat-local-verification-status-v1" `
    -and [bool](Get-JsonValue $localVerification "ok" $false)

$installerReady = Test-Path -LiteralPath $resolvedInstallerScriptPath -PathType Leaf
$diagnosticsReady = Test-Path -LiteralPath $resolvedDiagnosticsScriptPath -PathType Leaf
$uploadPlanReady = $windowsPackageReady
$opsHandoffReady = $localReleaseReviewReady -and $localVerificationReady
$releaseArchiveDecisionRecorded = $null -ne $releaseArchiveDecisionManifest `
    -and (Get-JsonValue $releaseArchiveDecisionManifest "format" "") -eq "qtnetworkchat-release-archive-decision-v1" `
    -and [bool](Get-JsonValue $releaseArchiveDecisionManifest "decisionRecorded" $false)
$releaseArchiveDecisionGate = if ($null -ne $releaseArchiveDecisionManifest) {
    Format-Value (Get-JsonValue $releaseArchiveDecisionManifest "decisionGate" "unknown")
} else {
    "release-archive-decision-not-recorded"
}

$deliveryTailPending = New-Object System.Collections.ArrayList
if (-not $uploadPlanReady) {
    [void]$deliveryTailPending.Add("release-auto-upload-not-ready")
}
if (-not $installerReady) {
    [void]$deliveryTailPending.Add("installer-bootstrap-not-ready")
}
if (-not $diagnosticsReady) {
    [void]$deliveryTailPending.Add("crash-diagnostics-not-ready")
}
if (-not $opsHandoffReady) {
    [void]$deliveryTailPending.Add("ops-handoff-not-ready")
}

$deliveryReady = $deliveryTailPending.Count -eq 0
$deliveryGate = if ($deliveryReady) { "ready-local-delivery-handoff" } else { "blocked-local-delivery-handoff" }
$operatorAction = if ($deliveryReady) {
    "Use the packaged upload plan, bootstrap installer, diagnostics collector, and operator handoff summary as the local release delivery bundle for the current HEAD."
} else {
    "Refresh the current-head Windows package and local release review artifacts, then regenerate the release delivery handoff bundle."
}

$stagingDir = Join-Path $resolvedOutputDir "release-delivery-handoff"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

$manifestInputs = New-Object System.Collections.ArrayList
$scanPaths = New-Object System.Collections.ArrayList

[void](Copy-EvidenceFile $resolvedReadmePath $stagingDir "docs/README.md" "readme" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedAutomationStatusPath $stagingDir "docs/automation-status.md" "automation-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalVerificationStatusPath $stagingDir "verification/local-verification-status.json" "local-verification-status" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewManifestPath $stagingDir "release-review/local-release-review-manifest.json" "local-release-review-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedLocalReleaseReviewPackagePath $stagingDir "release-review/local-release-review.zip" "local-release-review-package" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseArchiveDecisionManifestPath $stagingDir "archive/release-archive-decision-manifest.json" "release-archive-decision-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseArchiveDecisionMarkdownPath $stagingDir "archive/release-archive-decision.md" "release-archive-decision-markdown" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleasePublicationRecordPath $stagingDir "archive/release-publication-record.json" "release-publication-record" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedReleaseDeliveryDrillManifestPath $stagingDir "archive/release-delivery-drill-manifest.json" "release-delivery-drill-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedWindowsPackageManifestPath $stagingDir "windows/manifest.json" "windows-package-manifest" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedWindowsPackageZipPath $stagingDir "windows/QtNetworkChat-win-x64.zip" "windows-package-zip" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedInstallerScriptPath $stagingDir "tools/install-qtnetworkchat-package.ps1" "installer-script" $manifestInputs $scanPaths)
[void](Copy-EvidenceFile $resolvedDiagnosticsScriptPath $stagingDir "tools/collect-qtnetworkchat-diagnostics.ps1" "diagnostics-script" $manifestInputs $scanPaths)

$uploadPlan = [ordered]@{
    format = "qtnetworkchat-release-upload-plan-v1"
    ready = $uploadPlanReady
    releaseHead = $ReleaseHead
    sourcePackage = if ($windowsZipPresent) { "windows/QtNetworkChat-win-x64.zip" } else { "missing" }
    sourceManifest = if ($windowsPackagePresent) { "windows/manifest.json" } else { "missing" }
    packageSha256 = Get-Sha256Hex $resolvedWindowsPackageZipPath
    artifactName = "QtNetworkChat-win-x64.zip"
    operatorAction = if ($uploadPlanReady) {
        "Attach the packaged zip plus manifest to the chosen release channel or internal artifact store. No remote upload is performed automatically by this repository."
    } else {
        "Generate a current-head Windows package before attempting release distribution."
    }
}
$uploadPlanPath = Join-Path $stagingDir "release-upload-plan.json"
$uploadPlanMarkdownPath = Join-Path $stagingDir "release-upload-plan.md"
$uploadPlan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $uploadPlanPath -Encoding UTF8
$uploadPlanMarkdown = @(
    "# Release Upload Plan",
    "",
    ('- Ready: `{0}`' -f (Format-Value $uploadPlan.ready)),
    ('- Release head: `{0}`' -f $ReleaseHead),
    ('- Artifact: `{0}`' -f $uploadPlan.artifactName),
    ('- SHA-256: `{0}`' -f (Format-Value $uploadPlan.packageSha256)),
    ('- Operator action: `{0}`' -f $uploadPlan.operatorAction)
) -join [Environment]::NewLine
[System.IO.File]::WriteAllText($uploadPlanMarkdownPath, $uploadPlanMarkdown, (New-Object System.Text.UTF8Encoding($false)))
[void]$scanPaths.Add($uploadPlanPath)
[void]$scanPaths.Add($uploadPlanMarkdownPath)

$opsHandoff = [ordered]@{
    format = "qtnetworkchat-ops-handoff-v1"
    ready = $opsHandoffReady
    releaseHead = $ReleaseHead
    localReleaseReviewGate = $localReleaseReviewGate
    windowsPackageGate = $windowsGate
    deliveryGate = $deliveryGate
    evidenceBundle = @(
        "release-review/local-release-review-manifest.json",
        "release-review/local-release-review.zip",
        "windows/manifest.json",
        "windows/QtNetworkChat-win-x64.zip",
        "release-upload-plan.json",
        "tools/install-qtnetworkchat-package.ps1",
        "tools/collect-qtnetworkchat-diagnostics.ps1"
    )
    operatorChecklist = @(
        "Review local release review gate and current-head package commit.",
        "Install the package with tools/install-qtnetworkchat-package.ps1 on the target workstation if a local delivery drill is required.",
        "Use release-upload-plan.json/.md to publish the zip to the chosen internal or external distribution channel.",
        "Use tools/collect-qtnetworkchat-diagnostics.ps1 when a recipient reports a startup, dependency, or runtime verification problem."
    )
}
$opsHandoffPath = Join-Path $stagingDir "ops-handoff.json"
$opsHandoffMarkdownPath = Join-Path $stagingDir "ops-handoff.md"
$opsHandoff | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $opsHandoffPath -Encoding UTF8
$opsHandoffMarkdown = @(
    "# Operations Handoff",
    "",
    ('- Ready: `{0}`' -f (Format-Value $opsHandoff.ready)),
    ('- Release head: `{0}`' -f $ReleaseHead),
    ('- Local release review gate: `{0}`' -f $localReleaseReviewGate),
    ('- Windows package gate: `{0}`' -f $windowsGate),
    ('- Delivery gate: `{0}`' -f $deliveryGate),
    "",
    "## Checklist",
    ""
) + ($opsHandoff.operatorChecklist | ForEach-Object { '- ' + $_ })
[System.IO.File]::WriteAllText($opsHandoffMarkdownPath, ($opsHandoffMarkdown -join [Environment]::NewLine), (New-Object System.Text.UTF8Encoding($false)))
[void]$scanPaths.Add($opsHandoffPath)
[void]$scanPaths.Add($opsHandoffMarkdownPath)

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("# Release Delivery Handoff")
$summaryLines.Add("")
$summaryLines.Add(('- Generated at: `{0}`' -f ((Get-Date).ToUniversalTime().ToString("o"))))
$summaryLines.Add(('- Release head: `{0}`' -f $ReleaseHead))
$summaryLines.Add(('- Delivery ready: `{0}`' -f (Format-Value $deliveryReady)))
$summaryLines.Add(('- Delivery gate: `{0}`' -f $deliveryGate))
$summaryLines.Add(('- Operator action: `{0}`' -f $operatorAction))
$summaryLines.Add("")
$summaryLines.Add("## Component Gates")
$summaryLines.Add("")
$summaryLines.Add(('- Windows package: `present={0}; currentHeadMatch={1}; runtimeOk={2}; postgresRuntimeOk={3}; zip={4}; gate={5}`' -f `
        (Format-Value $windowsPackagePresent), (Format-Value $windowsCurrentHeadMatch), (Format-Value $windowsRuntimeOk), `
        (Format-Value $windowsPostgresOk), (Format-Value $windowsZipPresent), $windowsGate))
$summaryLines.Add(('- Local release review: `ready={0}; gate={1}`' -f (Format-Value $localReleaseReviewReady), $localReleaseReviewGate))
$summaryLines.Add(('- Release archive decision: `recorded={0}; gate={1}`' -f (Format-Value $releaseArchiveDecisionRecorded), $releaseArchiveDecisionGate))
$summaryLines.Add(('- Release publication record: `present={0}; status={1}`' -f (Format-Value ($null -ne $releasePublicationRecord)), (Format-Value (Get-JsonValue $releasePublicationRecord "publishingStatus" "unknown"))))
$summaryLines.Add(('- Release delivery drill: `present={0}; ok={1}`' -f (Format-Value ($null -ne $releaseDeliveryDrillManifest)), (Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "ok" "unknown"))))
$summaryLines.Add(('- Upload plan: `ready={0}`' -f (Format-Value $uploadPlanReady)))
$summaryLines.Add(('- Installer bootstrap: `ready={0}`' -f (Format-Value $installerReady)))
$summaryLines.Add(('- Diagnostics collector: `ready={0}`' -f (Format-Value $diagnosticsReady)))
$summaryLines.Add(('- Ops handoff: `ready={0}`' -f (Format-Value $opsHandoffReady)))
$summaryLines.Add("")
$summaryLines.Add("## Delivery Tail")
$summaryLines.Add("")
if ($deliveryTailPending.Count -eq 0) {
    $summaryLines.Add('- `none`')
} else {
    foreach ($tail in $deliveryTailPending) {
        $summaryLines.Add(('- `{0}`' -f $tail))
    }
}
$summaryLines.Add("")
$summaryLines.Add("## Packaged Inputs")
$summaryLines.Add("")
foreach ($input in $manifestInputs) {
    $summaryLines.Add(('- `{0}` as `{1}` (`{2}` bytes)' -f $input.sourceName, $input.packagedAs, $input.bytes))
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($resolvedMarkdownPath, ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[System.IO.File]::WriteAllText((Join-Path $stagingDir "release-delivery-handoff.md"), ($summaryLines -join [Environment]::NewLine), $utf8NoBom)
[void]$scanPaths.Add($resolvedMarkdownPath)

$sensitiveHits = New-Object System.Collections.ArrayList
foreach ($path in $scanPaths) {
    Add-SensitiveHits $path $sensitiveHits
}
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Release delivery handoff contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifest = [ordered]@{
    format = "qtnetworkchat-release-delivery-handoff-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = ($sensitiveHits.Count -eq 0)
    deliveryReady = $deliveryReady
    deliveryGate = $deliveryGate
    packagePath = "release-delivery-handoff.zip"
    packageSha256 = "pending"
    targetReleaseHead = $ReleaseHead
    markdownPackagedAs = "release-delivery-handoff.md"
    stagingDir = "release-delivery-handoff"
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    windowsPackage = [ordered]@{
        present = $windowsPackagePresent
        gitCommit = $windowsGitCommit
        currentHeadMatch = $windowsCurrentHeadMatch
        runtimeOk = $windowsRuntimeOk
        postgresRuntimeOk = $windowsPostgresOk
        zipPresent = $windowsZipPresent
        releaseGate = $windowsGate
    }
    localReleaseReview = [ordered]@{
        ready = $localReleaseReviewReady
        reviewGate = $localReleaseReviewGate
    }
    releaseArchiveDecision = [ordered]@{
        recorded = $releaseArchiveDecisionRecorded
        decisionGate = $releaseArchiveDecisionGate
    }
    releasePublicationRecord = [ordered]@{
        present = ($null -ne $releasePublicationRecord)
        publishingStatus = (Format-Value (Get-JsonValue $releasePublicationRecord "publishingStatus" "unknown"))
        channel = (Format-Value (Get-JsonValue $releasePublicationRecord "channel" "unknown"))
    }
    releaseDeliveryDrill = [ordered]@{
        present = ($null -ne $releaseDeliveryDrillManifest)
        ok = (Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "ok" "unknown"))
        manifestFormat = (Format-Value (Get-JsonValue $releaseDeliveryDrillManifest "format" "unknown"))
    }
    components = [ordered]@{
        uploadPlanReady = $uploadPlanReady
        installerReady = $installerReady
        diagnosticsReady = $diagnosticsReady
        opsHandoffReady = $opsHandoffReady
    }
    deliveryTailCount = $deliveryTailPending.Count
    deliveryTailPending = @($deliveryTailPending)
    operatorAction = $operatorAction
    sensitiveExportProof = [ordered]@{
        noSensitiveExportProof = ($sensitiveHits.Count -eq 0)
        credentialsExported = $false
        tokensExported = $false
        privateMaterialExported = $false
        plaintextBytesExported = $false
        ciphertextBytesExported = $false
        crashDumpBytesExported = $false
    }
    sensitiveHits = @($sensitiveHits)
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

Write-Host "release delivery handoff package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  markdown: {0}" -f $resolvedMarkdownPath)
Write-Host ("  delivery gate: {0}" -f $deliveryGate)
