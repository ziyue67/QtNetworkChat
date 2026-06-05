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
$readme = Get-Content -LiteralPath $resolvedReadmePath -Raw
$docsRoot = $resolvedDocsDir

$requiredDocs = @(
    "testing-coverage.md",
    "postgresql-operations.md",
    "large-file-governance.md",
    "e2e-hardening-status.md"
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

$automationStatusPath = Join-Path $docsRoot "automation-status.md"
Assert-File $automationStatusPath
$automationStatus = Get-Content -LiteralPath $automationStatusPath -Raw
Assert-Contains $automationStatus "Group productization is the active automation lane"
Assert-Contains $automationStatus "Mainwindow structure split follows group productization"
Assert-Contains $automationStatus "README information architecture is closed for now"

$utf8 = [System.Text.Encoding]::UTF8
$oversizedLines = @(Get-Content -LiteralPath $resolvedReadmePath |
    Where-Object { $utf8.GetByteCount($_) -gt 2500 })
if ($oversizedLines.Count -gt 0) {
    throw ("README still contains oversized UTF-8 lines: {0}" -f $oversizedLines.Count)
}

Write-Host "README information architecture verified"
