param(
    [string]$ReadmePath = (Join-Path (Split-Path -Parent $PSScriptRoot) "README.md"),
    [string]$DocsDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "docs")
)
$ErrorActionPreference = "Stop"

function Assert-Contains([string]$Text, [string]$Needle) {
    if (-not $Text.Contains($Needle)) {
        throw "Expected README to contain: $Needle"
    }
}

if (-not (Test-Path -LiteralPath $ReadmePath)) {
    throw "README.md is missing."
}

$readme = Get-Content -LiteralPath $ReadmePath -Raw -Encoding UTF8
Assert-Contains $readme "# QtNetworkChat"
Assert-Contains $readme "cmake --build build --target QtNetworkChat"
Assert-Contains $readme "QtNetworkChatExecutableExists|MessageSerializationRoundTrip|TlsSecurityPinning|WebSocketTransport"
Assert-Contains $readme "wss://qt.ziyuexc.top/ws"
Assert-Contains $readme "scripts/README.md"
Assert-Contains $readme "deploy/README.md"

foreach ($doc in @(
    "architecture.md",
    "testing-coverage.md",
    "e2e-hardening-status.md",
    "refactoring-closeout.md"
)) {
    $path = Join-Path $DocsDir $doc
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected documentation file is missing: $doc"
    }
    Assert-Contains $readme "docs/$doc"
}


Write-Host "README information architecture verified for current QtNetworkChat scope."
