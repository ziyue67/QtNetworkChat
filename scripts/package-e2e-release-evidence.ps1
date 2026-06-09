param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$PackagePath,
    [string]$ManifestPath,
    [string]$RolloutJsonPath,
    [string]$RolloutMarkdownPath,
    [string]$GitHubWindowsBuildStatusPath,
    [string]$LocalVerificationStatusPath,
    [string]$AutomationStatusPath,

    [switch]$FailOnSensitive,
    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

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

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
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

$resolvedPackagePath = Resolve-OptionalPath $PackagePath
$resolvedManifestPath = Resolve-OptionalPath $ManifestPath
$packageParent = Split-Path -Parent $resolvedPackagePath
if (-not [string]::IsNullOrWhiteSpace($packageParent)) {
    New-Item -ItemType Directory -Path $packageParent -Force | Out-Null
}
$manifestParent = Split-Path -Parent $resolvedManifestPath
if (-not [string]::IsNullOrWhiteSpace($manifestParent)) {
    New-Item -ItemType Directory -Path $manifestParent -Force | Out-Null
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

$rolloutAudit = Get-JsonValue $rollout "auditSummary" $null
$rolloutSummary = Get-JsonValue $rollout "summary" $null
$localBuild = Get-JsonValue $local "build" $null
$localCTest = Get-JsonValue $local "ctest" $null

$rolloutOk = [bool](Get-JsonValue $rollout "ok" $false)
$rolloutStatus = Format-Value (Get-JsonValue $rollout "status" "missing")
$rolloutGate = Format-Value (Get-JsonValue $rolloutAudit "releaseGate" "missing-rollout-observability")
$ciStatus = Format-Value (Get-JsonValue $ci "status" "missing")
$ciVisibility = Format-Value (Get-JsonValue $ci "visibility" "missing")
$ciRunId = Format-Value (Get-JsonValue $ci "runId" "unknown")
$ciSource = Format-Value (Get-JsonValue $ci "source" "unknown")
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
$packageOk = $sensitiveHits.Count -eq 0 -and $rolloutArtifactPresent -and $ciArtifactPresent -and $localArtifactPresent
$releaseReady = $packageOk -and $rolloutOk -and $ciStatus -eq "success" -and $localOk

$releaseGate = if ($sensitiveHits.Count -gt 0) {
    "blocked-sensitive-evidence"
} elseif (-not $rolloutArtifactPresent) {
    "blocked-missing-rollout-observability"
} elseif (-not $ciArtifactPresent) {
    "blocked-missing-github-windows-build-status"
} elseif (-not $localArtifactPresent) {
    "blocked-missing-local-verification-status"
} elseif ($ciStatus -ne "success") {
    if ($ciReleaseGate -ne "unknown") {
        $ciReleaseGate
    } elseif ($ciStatus -eq "external-visibility-stale") {
        "blocked-ci-head-not-observed"
    } else {
        "blocked-ci-" + $ciStatus
    }
} elseif (-not $localOk) {
    "blocked-local-verification"
} elseif (-not $rolloutOk) {
    $rolloutGate
} else {
    "e2e-release-evidence-ready"
}

$manifest = [ordered]@{
    format = "qtnetworkchat-e2e-release-evidence-package-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = $packageOk
    releaseReady = $releaseReady
    releaseGate = $releaseGate
    packagePath = Split-Path -Leaf $resolvedPackagePath
    packageSha256 = "pending"
    stagingDir = Split-Path -Leaf $stagingDir
    manifestPackagedAs = "manifest.json"
    manifestEmbedded = $true
    inputCount = $manifestInputs.Count
    inputs = @($manifestInputs)
    rollout = [ordered]@{
        present = $rolloutArtifactPresent
        status = $rolloutStatus
        ok = $rolloutOk
        readiness = Format-Value (Get-JsonValue $rolloutSummary "readiness" "unknown")
        releaseGate = $rolloutGate
    }
    ci = [ordered]@{
        present = $ciArtifactPresent
        status = $ciStatus
        runId = $ciRunId
        source = $ciSource
        visibility = $ciVisibility
        observedRunCount = [int](Get-JsonValue $ci "observedRunCount" 0)
        currentHeadObserved = $ciCurrentHeadObserved
        externalBlocker = $ciExternalBlocker
        releaseGate = $ciReleaseGate
        latestObservedHead = $ciLatestObservedHead
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
    sensitiveHits = @($sensitiveHits.ToArray())
}

$stagingManifestPath = Join-Path $stagingDir "manifest.json"
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $stagingManifestPath -Encoding UTF8

if (Test-Path -LiteralPath $resolvedPackagePath) {
    Remove-Item -LiteralPath $resolvedPackagePath -Force
}
Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $resolvedPackagePath -Force

$manifest.packageSha256 = Get-Sha256Hex $resolvedPackagePath
$manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resolvedManifestPath -Encoding UTF8

Write-Host "e2e release evidence package"
Write-Host ("  package: {0}" -f $resolvedPackagePath)
Write-Host ("  manifest: {0}" -f $resolvedManifestPath)
Write-Host ("  release gate: {0}" -f $releaseGate)
Write-Host ("  inputs: {0}" -f $manifestInputs.Count)
