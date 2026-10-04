param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [string]$BuildDir = "build-qt6-mingw",
    [string]$InstallDir = "",
    [string]$NotesPath = "",
    [string]$ReleaseHead = "",
    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "qt-script-common.ps1")



function Invoke-RepoScript([string]$ScriptPath, [string[]]$Arguments) {
    $resolvedScriptPath = Resolve-RepoPath $ScriptPath
    & powershell -ExecutionPolicy Bypass -File $resolvedScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Script failed: {0} (exit {1})" -f $resolvedScriptPath, $LASTEXITCODE)
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

function Get-JsonValue([object]$ObjectValue, [string]$Name, [object]$DefaultValue = $null) {
    if ($null -eq $ObjectValue) {
        return $DefaultValue
    }
    if ($ObjectValue.PSObject.Properties.Name -contains $Name) {
        return $ObjectValue.$Name
    }
    $DefaultValue
}


function Add-SensitiveHits([string]$PathValue, [System.Collections.ArrayList]$Hits) {
    $patterns = @(
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
        foreach ($pattern in $patterns) {
            if ($line -match $pattern -and $line -notmatch "(<redacted>|\\u003credacted\\u003e|&lt;redacted&gt;)") {
                [void]$Hits.Add(("{0}:{1}:{2}" -f (Split-Path -Leaf $PathValue), $lineNumber, $pattern))
            }
        }
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

$resolvedOutputDir = Resolve-OptionalPath $OutputDir
New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null

$resolvedBuildDir = Resolve-RepoPath $BuildDir
$deliveryHandoffDir = Join-Path $resolvedBuildDir "release-delivery-handoff\release-delivery-handoff"
$windowsZipPath = Join-Path $deliveryHandoffDir "windows\QtNetworkChat-win-x64.zip"
$releaseArchiveDecisionManifestPath = Join-Path $resolvedBuildDir "release-archive-decision\release-archive-decision-manifest.json"
$releaseDeliveryHandoffManifestPath = Join-Path $resolvedBuildDir "release-delivery-handoff\release-delivery-handoff-manifest.json"

if (-not (Test-Path -LiteralPath $windowsZipPath -PathType Leaf)) {
    throw ("Windows package zip not found for delivery drill: {0}" -f $windowsZipPath)
}

$resolvedInstallDir = Resolve-OptionalPath $InstallDir
if ([string]::IsNullOrWhiteSpace($resolvedInstallDir)) {
    $resolvedInstallDir = Join-Path $resolvedOutputDir "installed-package"
}
if (Test-Path -LiteralPath $resolvedInstallDir) {
    Remove-Item -LiteralPath $resolvedInstallDir -Recurse -Force
}

Invoke-RepoScript "scripts/install-qtnetworkchat-package.ps1" @(
    "-PackageZipPath", $windowsZipPath,
    "-InstallDir", $resolvedInstallDir,
    "-Force"
)

$diagnosticsOutputDir = Join-Path $resolvedOutputDir "release-diagnostics"
$diagnosticsNotesPath = Resolve-OptionalPath $NotesPath
if ([string]::IsNullOrWhiteSpace($diagnosticsNotesPath)) {
    $diagnosticsNotesPath = Join-Path $resolvedOutputDir "delivery-drill-notes.txt"
    @(
        "delivery drill: local install and diagnostics package generated from sanitized artifacts only",
        ("release-head={0}" -f $ReleaseHead),
        "publication channel remains environment-specific and is not invoked by this drill"
    ) | Set-Content -LiteralPath $diagnosticsNotesPath -Encoding UTF8
}

$diagnosticsArgs = @(
    "-OutputDir", $diagnosticsOutputDir,
    "-BuildDir", $resolvedBuildDir,
    "-InstallDir", $resolvedInstallDir,
    "-NotesPath", $diagnosticsNotesPath
)
if ($NoFailOnSensitive.IsPresent) {
    $diagnosticsArgs += "-NoFailOnSensitive"
}
Invoke-RepoScript "scripts/collect-qtnetworkchat-diagnostics.ps1" $diagnosticsArgs

$installRecordPath = Join-Path $resolvedInstallDir "install-record.json"
$diagnosticsManifestPath = Join-Path $diagnosticsOutputDir "release-diagnostics\manifest.json"
$diagnosticsPackagePath = Join-Path $diagnosticsOutputDir "release-diagnostics.zip"

$installRecord = Read-OptionalJson $installRecordPath
$diagnosticsManifest = Read-OptionalJson $diagnosticsManifestPath
$archiveDecisionManifest = Read-OptionalJson $releaseArchiveDecisionManifestPath
$deliveryHandoffManifest = Read-OptionalJson $releaseDeliveryHandoffManifestPath

$generatedAt = (Get-Date).ToUniversalTime().ToString("o")
$drillManifestPath = Join-Path $resolvedOutputDir "release-delivery-drill-manifest.json"
$drillMarkdownPath = Join-Path $resolvedOutputDir "release-delivery-drill.md"

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("# Release Delivery Drill")
$summaryLines.Add("")
$summaryLines.Add(('- Generated at: `{0}`' -f $generatedAt))
$summaryLines.Add(('- Release head: `{0}`' -f $ReleaseHead))
$summaryLines.Add(('- Install record: `{0}`' -f $(if ($null -ne $installRecord) { "present" } else { "missing" })))
$summaryLines.Add(('- Diagnostics package: `{0}`' -f $(if ($null -ne $diagnosticsManifest) { "present" } else { "missing" })))
$summaryLines.Add(('- Archive decision state: `{0}`' -f ([string](Get-JsonValue $archiveDecisionManifest "decisionState" "unknown"))))
$summaryLines.Add(('- Delivery handoff gate: `{0}`' -f ([string](Get-JsonValue $deliveryHandoffManifest "deliveryGate" "unknown"))))
$summaryLines.Add("")
$summaryLines.Add("## Drill Evidence")
$summaryLines.Add("")
$summaryLines.Add(('- Installed package zip: `{0}`' -f (Split-Path -Leaf $windowsZipPath)))
$summaryLines.Add(('- Install directory: `{0}`' -f (Split-Path -Leaf $resolvedInstallDir)))
$summaryLines.Add(('- Install record sha256: `{0}`' -f (Get-Sha256Hex $installRecordPath)))
$summaryLines.Add(('- Diagnostics package sha256: `{0}`' -f (Get-Sha256Hex $diagnosticsPackagePath)))
$summaryLines.Add(('- Diagnostics manifest sha256: `{0}`' -f (Get-Sha256Hex $diagnosticsManifestPath)))
$summaryLines.Add("")
$summaryLines.Add("## Operator Outcome")
$summaryLines.Add("")
$summaryLines.Add('- `Local install/unpack path verified.`')
$summaryLines.Add('- `Sanitized diagnostics bundle generated.`')
$summaryLines.Add('- `Environment-specific publication remains external.`')

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($drillMarkdownPath, ($summaryLines -join [Environment]::NewLine), $utf8NoBom)

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $drillMarkdownPath $sensitiveHits
Add-SensitiveHits $diagnosticsManifestPath $sensitiveHits
Add-SensitiveHits $installRecordPath $sensitiveHits
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Release delivery drill contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

$manifest = [ordered]@{
    format = "qtnetworkchat-release-delivery-drill-v1"
    generatedAt = $generatedAt
    ok = ($null -ne $installRecord) -and ($null -ne $diagnosticsManifest) -and ($sensitiveHits.Count -eq 0)
    releaseHead = $ReleaseHead
    installDirName = (Split-Path -Leaf $resolvedInstallDir)
    packageZipName = (Split-Path -Leaf $windowsZipPath)
    installRecordPath = "installed-package/install-record.json"
    diagnosticsPackagePath = "release-diagnostics/release-diagnostics.zip"
    diagnosticsManifestPath = "release-diagnostics/release-diagnostics/manifest.json"
    archiveDecisionState = [string](Get-JsonValue $archiveDecisionManifest "decisionState" "unknown")
    deliveryGate = [string](Get-JsonValue $deliveryHandoffManifest "deliveryGate" "unknown")
    installRecordSha256 = (Get-Sha256Hex $installRecordPath)
    diagnosticsPackageSha256 = (Get-Sha256Hex $diagnosticsPackagePath)
    diagnosticsManifestSha256 = (Get-Sha256Hex $diagnosticsManifestPath)
    notesPath = (Split-Path -Leaf $diagnosticsNotesPath)
    sensitiveHits = @($sensitiveHits.ToArray())
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $drillManifestPath -Encoding UTF8

Write-Host "release delivery drill"
Write-Host ("  manifest: {0}" -f $drillManifestPath)
Write-Host ("  markdown: {0}" -f $drillMarkdownPath)
Write-Host ("  install dir: {0}" -f $resolvedInstallDir)
Write-Host ("  diagnostics: {0}" -f $diagnosticsPackagePath)
