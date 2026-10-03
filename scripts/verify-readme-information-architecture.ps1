$ErrorActionPreference = "Stop"

function Assert-Contains([string]$Text, [string]$Needle) {
    if (-not $Text.Contains($Needle)) {
        throw "Expected README to contain: $Needle"
    }
}

function Assert-NotContains([string]$Text, [string]$Needle) {
    if ($Text.Contains($Needle)) {
        throw "README or docs should not contain removed rewrite reference: $Needle"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$readmePath = Join-Path $repoRoot "README.md"
$docsRoot = Join-Path $repoRoot "docs"

if (-not (Test-Path -LiteralPath $readmePath)) {
    throw "README.md is missing."
}

$readme = Get-Content -LiteralPath $readmePath -Raw -Encoding UTF8
Assert-Contains $readme "# QtNetworkChat"
Assert-Contains $readme "cmake --build build-qt6-mingw --target QtNetworkChat"
Assert-Contains $readme "QtNetworkChatExecutableExists|MessageSerializationRoundTrip|RedisServerReadiness"

foreach ($doc in @(
    "automation-status.md",
    "testing-coverage.md",
    "postgresql-operations.md",
    "large-file-governance.md",
    "e2e-hardening-status.md",
    "release-closeout.md"
)) {
    $path = Join-Path $docsRoot $doc
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected documentation file is missing: $doc"
    }
    Assert-Contains $readme "docs/$doc"
}


Write-Host "README information architecture verified for current QtNetworkChat scope."
