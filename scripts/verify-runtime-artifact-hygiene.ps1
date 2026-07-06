param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Read-Utf8Text([string]$Path) {
    return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
    if (-not $Text.Contains($Needle)) {
        throw ("Missing {0} ignore rule: {1}" -f $Label, $Needle)
    }
}

function Assert-NoTrackedMatch([object[]]$TrackedFiles, [string]$Pattern, [string]$Label) {
    $matches = @($TrackedFiles | Where-Object { $_ -match $Pattern })
    if ($matches.Count -gt 0) {
        throw ("Tracked runtime artifact hygiene violation ({0}): {1}" -f $Label, ($matches -join "; "))
    }
}

function Assert-GitIgnoreSample([string]$Root, [string]$Path) {
    & git -C $Root check-ignore -q -- $Path
    if ($LASTEXITCODE -ne 0) {
        throw ("Expected sample runtime artifact to be ignored: {0}" -f $Path)
    }
}

$resolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$rootIgnorePath = Join-Path $resolvedRepoRoot ".gitignore"
$tauriIgnorePath = Join-Path $resolvedRepoRoot "tauri-qqnt/src-tauri/.gitignore"

if (-not (Test-Path -LiteralPath $rootIgnorePath)) {
    throw "Missing root .gitignore"
}
if (-not (Test-Path -LiteralPath $tauriIgnorePath)) {
    throw "Missing tauri-qqnt/src-tauri/.gitignore"
}

$rootIgnore = Read-Utf8Text $rootIgnorePath
$tauriIgnore = Read-Utf8Text $tauriIgnorePath

$requiredRootRules = @(
    "*.sqlite",
    "*.sqlite3",
    "accounts.sqlite3",
    "history.sqlite3",
    "client_*.sqlite3",
    "histories/",
    "offline-attachments/",
    "offline_attachments/",
    "offline-files/",
    "offline_files/",
    "received-files/",
    "transfer-payloads/",
    "transfer_payloads/",
    "tauri-qqnt/dist/",
    "tauri-qqnt/src-tauri/target/",
    "tauri-qqnt/src-tauri/binaries/*.exe",
    "tauri-qqnt/src-tauri/gen/schemas/"
)
foreach ($rule in $requiredRootRules) {
    Assert-Contains $rootIgnore $rule "root runtime artifact"
}

Assert-Contains $tauriIgnore "/target/" "Tauri generated output"
Assert-Contains $tauriIgnore "/gen/schemas" "Tauri generated schema"

$ignoredSamples = @(
    "accounts.sqlite3",
    "histories/alice/history.sqlite3",
    "offline-attachments/alice/payload.bin",
    "offline_files/bob/payload.bin",
    "received-files/alice/photo.png",
    "transfer-payloads/send/blob.bin",
    "tauri-qqnt/dist/index.html",
    "tauri-qqnt/src-tauri/target/debug/tauri-qqnt.exe",
    "tauri-qqnt/src-tauri/binaries/QQNTEngine-x86_64-pc-windows-msvc.exe",
    "tauri-qqnt/src-tauri/gen/schemas/desktop-schema.json"
)
foreach ($sample in $ignoredSamples) {
    Assert-GitIgnoreSample $resolvedRepoRoot $sample
}

$trackedFiles = @((& git -C $resolvedRepoRoot ls-files) | ForEach-Object {
    $_ -replace "\\", "/"
})
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed while checking runtime artifact hygiene"
}

Assert-NoTrackedMatch $trackedFiles '(?i)\.(sqlite|sqlite3|db)$' "local database files"
Assert-NoTrackedMatch $trackedFiles '(?i)(^|/)(histories|offline-attachments|offline_attachments|offline-files|offline_files|received-files|transfer-payloads|transfer_payloads)(/|$)' "runtime data directories"
Assert-NoTrackedMatch $trackedFiles '(?i)^tauri-qqnt/(dist|node_modules)/' "Tauri frontend generated output"
Assert-NoTrackedMatch $trackedFiles '(?i)^tauri-qqnt/src-tauri/(target|binaries)/' "Tauri Rust generated output and sidecar binaries"
Assert-NoTrackedMatch $trackedFiles '(?i)(^|/)build(-[^/]*)?/' "CMake build tree"
Assert-NoTrackedMatch $trackedFiles '(?i)\.(exe|dll|pdb|ilk|obj|o|a|lib|so|dylib|msi|zip|7z|dmp|mdmp)$' "binary or archive output"

Write-Host "Runtime artifact hygiene verified"
