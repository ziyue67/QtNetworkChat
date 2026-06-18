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

function Assert-Equals([object]$Actual, [object]$Expected, [string]$Label) {
    if ($Actual -ne $Expected) {
        throw ("Unexpected {0}: expected '{1}', got '{2}'" -f $Label, $Expected, $Actual)
    }
}

function Assert-ArrayContains([object[]]$Values, [string]$Expected, [string]$Label) {
    if ($Values -notcontains $Expected) {
        throw ("Missing expected {0}: {1}" -f $Label, $Expected)
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

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
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
$packagePath = Join-Path $repoRoot "tauri-qqnt/package.json"
$tauriConfigPath = Join-Path $repoRoot "tauri-qqnt/src-tauri/tauri.conf.json"
$branchPlanPath = Join-Path $docsRoot "qqnt-concurrent-branches-plan.md"
$coveragePath = Join-Path $docsRoot "testing-coverage.md"

$requiredDocs = @(
    "qqnt-implementation-plan.md",
    "qqnt-concurrent-branches-plan.md",
    "qqnt-ipcv1.md",
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

Assert-File $packagePath
Assert-File $tauriConfigPath
Assert-File $branchPlanPath
Assert-File $coveragePath

Assert-Contains $readme "docs/automation-status.md"
Assert-Contains $readme "docs/qqnt-implementation-plan.md"
Assert-Contains $readme "docs/qqnt-concurrent-branches-plan.md"
Assert-Contains $readme "docs/qqnt-ipcv1.md"
Assert-Contains $readme "docs/testing-coverage.md"
Assert-Contains $readme "docs/postgresql-operations.md"
Assert-Contains $readme "docs/large-file-governance.md"
Assert-Contains $readme "docs/e2e-hardening-status.md"
Assert-Contains $readme "docs/release-closeout.md"

Assert-Contains $readme "cmake --build build-qt6-mingw --target QQNTEngine QQNTServer"
Assert-Contains $readme "QQNTEngineSmoke|QQNTEngineEndToEnd|QQNTProtocolDrift|QQNTServerRedis|RedisServerReadiness|RedisStartupRequirement"
Assert-Contains $readme "cd tauri-qqnt/src-tauri"
Assert-Contains $readme "cargo fmt --check"
Assert-Contains $readme "cargo test"
Assert-Contains $readme "cd tauri-qqnt"
Assert-Contains $readme "npm run tauri build"
Assert-Contains $readme 'Backend work happens on `codex/qqnt-backend`'
Assert-Contains $readme 'Frontend work happens on `codex/qqnt-frontend`'
Assert-Contains $readme "tauri-qqnt/src-tauri/"
Assert-Contains $readme "tauri-qqnt/src/"

$package = Read-Utf8Text $packagePath | ConvertFrom-Json
$tauriConfig = Read-Utf8Text $tauriConfigPath | ConvertFrom-Json

Assert-Equals $package.scripts.build "tsc && vite build" "package build script"
Assert-Equals $package.scripts.tauri "tauri" "package tauri script"
Assert-Contains $readme ("npm run {0} build" -f $package.scripts.tauri)

Assert-Equals $tauriConfig.build.beforeBuildCommand "npm run build" "Tauri beforeBuildCommand"
Assert-Equals $tauriConfig.build.frontendDist "../dist" "Tauri frontendDist"
Assert-Equals $tauriConfig.bundle.active $true "Tauri bundle.active"
Assert-ArrayContains @($tauriConfig.bundle.targets) "nsis" "Tauri bundle target"
Assert-ArrayContains @($tauriConfig.bundle.externalBin) "binaries/QQNTEngine" "Tauri externalBin"
Assert-ArrayContains @($tauriConfig.bundle.externalBin) "binaries/QQNTServer" "Tauri externalBin"

$branchPlan = Read-Utf8Text $branchPlanPath
Assert-Contains $branchPlan "| B14 |"
Assert-Contains $branchPlan 'tauri-qqnt/src-tauri/tauri.conf.json'
Assert-Contains $branchPlan 'npm run tauri build'
Assert-Contains $branchPlan "| BM6 | B14 |"
Assert-Contains $branchPlan '`codex/qqnt-backend`'
Assert-Contains $branchPlan '`codex/qqnt-frontend`'
Assert-Contains $branchPlan "tauri-qqnt/src-tauri/"
Assert-Contains $branchPlan "tauri-qqnt/src/"

$coverage = Read-Utf8Text $coveragePath
Assert-Contains $coverage "npm run tauri build"
Assert-Contains $coverage "ReadmeInformationArchitecture"
Assert-Contains $coverage "Backend validation should stay scoped"
Assert-Contains $coverage 'Frontend rendering and mock UI behavior belong to `codex/qqnt-frontend`'

$automationStatusPath = Join-Path $docsRoot "automation-status.md"
Assert-File $automationStatusPath
$automationStatus = Read-Utf8Text $automationStatusPath
Assert-Contains $automationStatus "E2E production crypto evidence remains the deepest closeout evidence lane for this branch"
Assert-Contains $automationStatus "callable manifest"
Assert-Contains $automationStatus "sanitized execution result contract"
Assert-Contains $automationStatus "production rotation dry-run/execute evidence"
Assert-Contains $automationStatus "Current repository work is in closeout, not feature expansion"
Assert-Contains $automationStatus "Release and operations delivery now uses a local-closeout policy"
Assert-Contains $automationStatus "Environment-specific publishing remains an explicit step outside this repository"
Assert-Contains $automationStatus "README information architecture and current-state alignment remain maintenance work only"
Assert-Contains $automationStatus "PostgreSQL release acceptance and database-health warnings now belong to operational evidence follow-up"
Assert-Contains $automationStatus "Governance performance closeout remains a summary lane"

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
