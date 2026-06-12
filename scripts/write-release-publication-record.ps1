param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$ReleaseHead,
    [string]$Channel = "not-recorded",
    [string]$PublishingStatus = "pending-environment-publication",
    [string]$PublishedBy = "",
    [string]$PublishedAt = "",
    [string]$ArtifactName = "QtNetworkChat-win-x64.zip",
    [string]$ArtifactSha256 = "",
    [string]$Notes = "",
    [switch]$NoFailOnSensitive
)

$ErrorActionPreference = "Stop"

function Resolve-OptionalPath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
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
        $ReleaseHead = "unknown"
    }
}
if ([string]::IsNullOrWhiteSpace($PublishedBy)) {
    $PublishedBy = [Environment]::UserName
}
if ([string]::IsNullOrWhiteSpace($PublishedAt)) {
    $PublishedAt = (Get-Date).ToUniversalTime().ToString("o")
}
if ([string]::IsNullOrWhiteSpace($Notes)) {
    $Notes = "Environment-specific publication remains outside this repository; this file records only the sanitized publication handoff state."
}

$resolvedOutputPath = Resolve-OptionalPath $OutputPath
$parent = Split-Path -Parent $resolvedOutputPath
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}

$manifest = [ordered]@{
    format = "qtnetworkchat-release-publication-record-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    releaseHead = $ReleaseHead
    channel = $Channel
    publishingStatus = $PublishingStatus
    publishedBy = $PublishedBy
    publishedAt = $PublishedAt
    artifactName = $ArtifactName
    artifactSha256 = $ArtifactSha256
    notes = $Notes
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

$sensitiveHits = New-Object System.Collections.ArrayList
Add-SensitiveHits $resolvedOutputPath $sensitiveHits
if ($sensitiveHits.Count -gt 0 -and -not $NoFailOnSensitive) {
    throw ("Release publication record contains sensitive-looking fields: {0}" -f (@($sensitiveHits) -join "; "))
}

Write-Host "release publication record"
Write-Host ("  output: {0}" -f $resolvedOutputPath)
Write-Host ("  status: {0}" -f $PublishingStatus)
