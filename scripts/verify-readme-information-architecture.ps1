param(
    [string]$ReadmePath = "README.md",
    [string]$DocsDir = "docs"
)

$ErrorActionPreference = "Stop"

function Assert-Contains([string]$Text, [string]$Needle) {
    if (-not $Text.Contains($Needle)) {
        throw "Missing expected README IA text: $Needle"
    }
}

function Assert-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing documentation file: $Path"
    }
}

function Read-Utf8Text([string]$Path) {
    return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

function Read-Utf8Lines([string]$Path) {
    return [System.IO.File]::ReadAllLines($Path, [System.Text.Encoding]::UTF8)
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$resolvedReadmePath = if ([System.IO.Path]::IsPathRooted($ReadmePath)) {
    $ReadmePath
} else {
    Join-Path $repoRoot $ReadmePath
}
$resolvedDocsDir = if ([System.IO.Path]::IsPathRooted($DocsDir)) {
    $DocsDir
} else {
    Join-Path $repoRoot $DocsDir
}
$readme = Read-Utf8Text $resolvedReadmePath
$docsRoot = $resolvedDocsDir

$requiredDocs = @(
    "testing-coverage.md",
    "postgresql-operations.md",
    "large-file-governance.md",
    "e2e-hardening-status.md",
    "release-closeout.md"
)

foreach ($doc in $requiredDocs) {
    Assert-File (Join-Path $docsRoot $doc)
    Assert-Contains $readme ("docs/$doc")
}

Assert-Contains $readme "docs/automation-status.md"
Assert-Contains $readme "docs/testing-coverage.md"
Assert-Contains $readme "docs/postgresql-operations.md"
Assert-Contains $readme "docs/large-file-governance.md"
Assert-Contains $readme "docs/e2e-hardening-status.md"
Assert-Contains $readme "docs/release-closeout.md"

$automationStatusPath = Join-Path $docsRoot "automation-status.md"
Assert-File $automationStatusPath
$automationStatus = Read-Utf8Text $automationStatusPath
Assert-Contains $automationStatus "E2E production crypto is the active automation lane"
Assert-Contains $automationStatus "callable manifest"
Assert-Contains $automationStatus "sanitized execution result contract"
Assert-Contains $automationStatus "production rotation dry-run/execute evidence"
Assert-Contains $automationStatus "File/offline attachment productization is closed for the current automation lane"
Assert-Contains $automationStatus "Group productization is closed for the current automation lane"
Assert-Contains $automationStatus "Mainwindow structure split is no longer the active lane and the product-facing Stage 1/2 work is complete"
Assert-Contains $automationStatus "README information architecture and current-state alignment are now the main documentation lane"
Assert-Contains $automationStatus "Release and operations delivery now has a complete local closeout chain"
Assert-Contains $automationStatus "Governance performance closeout is now a first-class summary lane"

$utf8 = [System.Text.Encoding]::UTF8
$lineNumber = 0
$oversizedLines = @(Read-Utf8Lines $resolvedReadmePath | ForEach-Object {
    $lineNumber++
    $byteCount = $utf8.GetByteCount($_)
    if ($byteCount -gt 2500) {
        "line=$lineNumber bytes=$byteCount"
    }
})
if ($oversizedLines.Count -gt 0) {
    throw ("README still contains oversized UTF-8 lines: {0}: {1}" -f $oversizedLines.Count, ($oversizedLines -join "; "))
}

Write-Host "README information architecture verified"
