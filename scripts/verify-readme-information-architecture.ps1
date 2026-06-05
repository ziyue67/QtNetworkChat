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

Assert-Contains $readme "## 文档索引"
Assert-Contains $readme "README 保留快速上手、项目结构、构建入口和运行入口"
Assert-Contains $readme "docs/automation-status.md"

$automationStatusPath = Join-Path $docsRoot "automation-status.md"
Assert-File $automationStatusPath
$automationStatus = Get-Content -LiteralPath $automationStatusPath -Raw
Assert-Contains $automationStatus "README information architecture is now the active automation lane until closed"
Assert-Contains $automationStatus "Group productization is next after README IA"
Assert-Contains $automationStatus "Mainwindow structure split follows group productization"

$oversizedLines = @(Get-Content -LiteralPath $resolvedReadmePath |
    Where-Object { $_.Length -gt 2500 })
if ($oversizedLines.Count -gt 0) {
    throw ("README still contains oversized lines: {0}" -f $oversizedLines.Count)
}

Write-Host "README information architecture verified"
