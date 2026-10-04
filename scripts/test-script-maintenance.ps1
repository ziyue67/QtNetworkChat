param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"
. (Join-Path $RepoRoot "scripts/qt-script-common.ps1")

function Assert-Equal($Actual, $Expected, [string]$Message) {
    if ($Actual -ne $Expected) { throw "$Message -- expected '$Expected', got '$Actual'" }
}

# Parse every maintained script, including scripts not called by CMake.
$scripts = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot "scripts") -Filter *.ps1 -File)
$scripts += Get-Item -LiteralPath (Join-Path $RepoRoot "capture-screenshot.ps1")
foreach ($file in $scripts) {
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $file.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -gt 0) { throw ($parseErrors | Out-String) }
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("qnc-script-tests-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Push-Location $temporary
    try {
        Assert-Equal (Resolve-RepoPath "") "" "Empty optional repo input must stay empty"
        Assert-Equal (Resolve-RequiredRepoPath "") (Join-Path $RepoRoot "") "Required empty path preserves the repo root"
        Assert-Equal (Resolve-RepoPath "README.md") (Join-Path $RepoRoot "README.md") "Repo paths must be independent of cwd"
        Assert-Equal (Resolve-OptionalPath "") "" "Optional empty input must stay empty"
        Assert-Equal (Resolve-OptionalPath "fixture.txt") (Join-Path $temporary "fixture.txt") "Input evidence remains relative to cwd"
        Assert-Equal (Resolve-RepoPath $temporary) $temporary "Absolute paths must be preserved"
        $payloadPath = Join-Path $temporary "payload.txt"
        [System.IO.File]::WriteAllText($payloadPath, "abc", [System.Text.UTF8Encoding]::new($false))
        Assert-Equal (Get-Sha256Hex $payloadPath) "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" "SHA256 known answer"
        Assert-Equal (Get-Sha256Hex "missing.txt") "unknown" "Missing evidence must not claim a hash"
        # The hash stream must be disposed so Windows can rename/delete the input.
        Move-Item -LiteralPath $payloadPath -Destination (Join-Path $temporary "renamed.txt")

        $statusPath = Join-Path $temporary "verification.json"
        & (Join-Path $RepoRoot "scripts/write-local-verification-status.ps1") `
            -OutputPath $statusPath -BuildExitCode 0 -CTestExitCode 0 -CTestCount 3
        $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
        Assert-Equal $status.ok $true "Verification script must run with shared helpers"
        Assert-Equal $status.ctest.count 3 "Verification count must remain exact"
        # This calls a consumer which uses cwd-relative optional paths and hashes.
        & (Join-Path $RepoRoot "scripts/write-release-publication-record.ps1") `
            -OutputPath "publication.json" -ReleaseHead "test-fixture" `
            -PublishingStatus "not-published" -Channel "test-only" -Notes "Local helper regression fixture"
        if (-not (Test-Path -LiteralPath "publication.json")) { throw "Publication evidence was not written relative to cwd" }
    } finally {
        Pop-Location
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
& (Join-Path $RepoRoot "scripts/verify-readme-information-architecture.ps1")
Write-Output ("Script maintenance passed: parsed {0} scripts; path/hash/consumer/documentation regressions passed." -f $scripts.Count)
