param(
    [string]$OutputPath = "build-qt6-mingw\local-verification-status.json",
    [string]$BuildStatus = "unknown",
    [int]$BuildExitCode = -1,
    [string]$CTestStatus = "unknown",
    [int]$CTestExitCode = -1,
    [int]$CTestCount = 0,
    [string]$BuildCommand = "cmake --build build-qt6-mingw",
    [string]$CTestCommand = "ctest --test-dir build-qt6-mingw --output-on-failure",
    [int]$BuildTimeoutSeconds = 600,
    [int]$CTestTimeoutSeconds = 900,
    [string]$CTestLogPath,
    [switch]$FailOnSensitive
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")) $PathValue
}

function Normalize-Status([string]$Value, [int]$ExitCode) {
    $status = ([string]$Value).Trim().ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($status) -or $status -eq "unknown") {
        if ($ExitCode -eq 0) {
            return "passed"
        }
        if ($ExitCode -gt 0) {
            return "failed"
        }
        return "unknown"
    }
    $status
}

function Read-CTestCount([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return 0
    }
    try {
        $resolved = Resolve-RepoPath $PathValue
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            return 0
        }
        $raw = Get-Content -LiteralPath $resolved -Raw -Encoding UTF8
        $matches = [regex]::Matches($raw, '(?m)^\s*(\d+)\/(\d+)\s+Testing:')
        $total = 0
        foreach ($match in $matches) {
            $candidate = [int]$match.Groups[2].Value
            if ($candidate -gt $total) {
                $total = $candidate
            }
        }
        $total
    } catch {
        0
    }
}

$resolvedOutput = Resolve-RepoPath $OutputPath
$parent = Split-Path -Parent $resolvedOutput
if (-not [string]::IsNullOrWhiteSpace($parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}

if ([string]::IsNullOrWhiteSpace($CTestLogPath)) {
    $CTestLogPath = "build-qt6-mingw\Testing\Temporary\LastTest.log"
}
if ($CTestCount -le 0) {
    $CTestCount = Read-CTestCount $CTestLogPath
}

$buildStatusValue = Normalize-Status $BuildStatus $BuildExitCode
$ctestStatusValue = Normalize-Status $CTestStatus $CTestExitCode
$ok = $buildStatusValue -eq "passed" -and $ctestStatusValue -eq "passed"

$status = [ordered]@{
    format = "qtnetworkchat-local-verification-status-v1"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    ok = $ok
    build = [ordered]@{
        status = $buildStatusValue
        exitCode = $BuildExitCode
        command = $BuildCommand
        timeoutSeconds = $BuildTimeoutSeconds
    }
    ctest = [ordered]@{
        status = $ctestStatusValue
        exitCode = $CTestExitCode
        count = $CTestCount
        command = $CTestCommand
        timeoutSeconds = $CTestTimeoutSeconds
        logPath = $CTestLogPath
    }
    sensitiveExportProof = [ordered]@{
        noSensitiveExportProof = $true
        credentialsExported = $false
        tokensExported = $false
    }
}

$json = $status | ConvertTo-Json -Depth 6
foreach ($forbidden in @("ghp_", "github_pat_", "Authorization:", "Credential=", "Signature=")) {
    if ($json.Contains($forbidden)) {
        Write-Host ("local verification status leaked forbidden text: {0}" -f $forbidden)
        if ($FailOnSensitive) {
            exit 2
        }
    }
}

$json | Set-Content -LiteralPath $resolvedOutput -Encoding UTF8
Write-Host ("local verification status: {0}" -f $resolvedOutput)
