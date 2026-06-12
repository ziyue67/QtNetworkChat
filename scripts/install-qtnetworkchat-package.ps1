param(
    [Parameter(Mandatory = $true)]
    [string]$PackageZipPath,

    [Parameter(Mandatory = $true)]
    [string]$InstallDir,

    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredFile([string]$PathValue, [string]$Label) {
    if ([string]::IsNullOrWhiteSpace($PathValue) -or -not (Test-Path -LiteralPath $PathValue -PathType Leaf)) {
        throw ("{0} not found: {1}" -f $Label, $PathValue)
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Resolve-TargetDirectory([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        throw "InstallDir is required."
    }
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PathValue)
}

function Get-Sha256Hex([string]$PathValue) {
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

$resolvedPackageZipPath = Resolve-RequiredFile $PackageZipPath "PackageZipPath"
$resolvedInstallDir = Resolve-TargetDirectory $InstallDir

if (Test-Path -LiteralPath $resolvedInstallDir) {
    if (-not $Force) {
        throw "InstallDir already exists. Re-run with -Force to replace it."
    }
    Remove-Item -LiteralPath $resolvedInstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedInstallDir -Force | Out-Null

Expand-Archive -LiteralPath $resolvedPackageZipPath -DestinationPath $resolvedInstallDir -Force

$manifestPath = Join-Path $resolvedInstallDir "manifest.json"
$exePath = Join-Path $resolvedInstallDir "QtNetworkChat.exe"
$readmePath = Join-Path $resolvedInstallDir "README.md"

foreach ($requiredPath in @($manifestPath, $exePath, $readmePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw ("Installed package is missing required file: {0}" -f (Split-Path -Leaf $requiredPath))
    }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
$installRecordPath = Join-Path $resolvedInstallDir "install-record.json"
$installRecord = [ordered]@{
    format = "qtnetworkchat-install-record-v1"
    installedAt = (Get-Date).ToUniversalTime().ToString("o")
    installDir = $resolvedInstallDir
    packageZipName = Split-Path -Leaf $resolvedPackageZipPath
    packageSha256 = Get-Sha256Hex $resolvedPackageZipPath
    appName = [string]$manifest.appName
    version = [string]$manifest.version
    gitCommit = [string]$manifest.gitCommit
    executable = [string]$manifest.executable
    executableSize = $manifest.executableSize
}
$installRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $installRecordPath -Encoding UTF8

Write-Host "installed QtNetworkChat package"
Write-Host ("  package: {0}" -f $resolvedPackageZipPath)
Write-Host ("  install dir: {0}" -f $resolvedInstallDir)
Write-Host ("  version: {0}" -f $installRecord.version)
Write-Host ("  git commit: {0}" -f $installRecord.gitCommit)
