param(
    [string]$BuildDir = "build-qt6-mingw",
    [string]$Configuration = "Release",
    [string]$PackageDir = "dist",
    [switch]$SkipBuild,
    [switch]$NoDeploy,
    [switch]$FailOnMissingRuntime
)

$ErrorActionPreference = "Stop"

function Get-ProjectVersion {
    param([string]$RepoRoot)

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (Test-Path $cmakePath) {
        $content = Get-Content -LiteralPath $cmakePath -Raw
        $match = [regex]::Match($content, 'project\s*\(\s*QtNetworkChat\s+VERSION\s+([0-9]+(?:\.[0-9]+){0,2})', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }
    return "0.0.0"
}

function Get-GitCommit {
    param([string]$RepoRoot)

    try {
        $commit = (& git -C $RepoRoot rev-parse --short=12 HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($commit)) {
            return $commit.Trim()
        }
    } catch {
    }
    return "unknown"
}

function Test-RuntimeFiles {
    param(
        [string]$StagePath,
        [bool]$DeployAttempted
    )

    $required = @(
        "QtNetworkChat.exe",
        "README.md"
    )
    if ($DeployAttempted) {
        $required += @(
            "Qt6Core.dll",
            "Qt6Gui.dll",
            "Qt6Network.dll",
            "Qt6Sql.dll",
            "Qt6Widgets.dll"
        )
    }

    $items = @()
    foreach ($name in $required) {
        $path = Join-Path $StagePath $name
        $exists = Test-Path -LiteralPath $path
        $items += [pscustomobject]@{
            name = $name
            exists = $exists
            size = if ($exists) { (Get-Item -LiteralPath $path).Length } else { 0 }
        }
    }

    $missing = @($items | Where-Object { -not $_.exists } | ForEach-Object { $_.name })
    return [pscustomobject]@{
        ok = $missing.Count -eq 0
        deployAttempted = $DeployAttempted
        missing = $missing
        files = $items
    }
}

function Resolve-RepoPath {
    param(
        [string]$RepoRoot,
        [string]$Path
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $RepoRoot $Path
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$version = Get-ProjectVersion -RepoRoot $repoRoot
$gitCommit = Get-GitCommit -RepoRoot $repoRoot
$buildPath = Resolve-RepoPath -RepoRoot $repoRoot -Path $BuildDir
$packageRoot = Resolve-RepoPath -RepoRoot $repoRoot -Path $PackageDir
$stageName = "QtNetworkChat-$version-win-x64"
$stagePath = Join-Path $packageRoot $stageName
$zipPath = Join-Path $packageRoot "$stageName.zip"
$manifestPath = Join-Path $stagePath "manifest.json"

Write-Host "Packaging QtNetworkChat $version from $repoRoot"
if (-not $SkipBuild) {
    cmake --build $buildPath --config $Configuration
} else {
    Write-Host "Skipping build because -SkipBuild was specified"
}

$exe = Get-ChildItem -Path $buildPath -Recurse -Filter "QtNetworkChat.exe" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $exe) {
    throw "QtNetworkChat.exe was not found under $buildPath"
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
if (Test-Path $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagePath | Out-Null

Copy-Item -LiteralPath $exe.FullName -Destination $stagePath
Copy-Item -LiteralPath (Join-Path $repoRoot "README.md") -Destination $stagePath

$deployToolPath = $null
$deployAttempted = $false
if (-not $NoDeploy) {
    $deployTool = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if (-not $deployTool) {
        $qmake = Get-Command "qmake.exe" -ErrorAction SilentlyContinue
        if ($qmake) {
            $candidate = Join-Path (Split-Path $qmake.Source) "windeployqt.exe"
            if (Test-Path $candidate) {
                $deployTool = Get-Item $candidate
            }
        }
    }

    if ($deployTool) {
        $deployToolPath = $deployTool.Source
        $deployAttempted = $true
        Write-Host "Running $deployToolPath"
        & $deployToolPath --release --compiler-runtime (Join-Path $stagePath "QtNetworkChat.exe")
    } else {
        Write-Warning "windeployqt.exe was not found. The package contains the app exe and README only."
    }
} else {
    Write-Host "Skipping windeployqt because -NoDeploy was specified"
}

$runtimeCheck = Test-RuntimeFiles -StagePath $stagePath -DeployAttempted $deployAttempted
if ($FailOnMissingRuntime -and -not $runtimeCheck.ok) {
    throw ("Runtime dependency check failed. Missing: {0}" -f ($runtimeCheck.missing -join ", "))
}

$manifest = [ordered]@{
    packageFormat = "qtnetworkchat-windows-package-v1"
    appName = "QtNetworkChat"
    version = $version
    gitCommit = $gitCommit
    configuration = $Configuration
    platform = "win-x64"
    createdAt = (Get-Date).ToUniversalTime().ToString("o")
    stageDirectory = $stageName
    executable = "QtNetworkChat.exe"
    executableSize = $exe.Length
    deployTool = if ($deployToolPath) { $deployToolPath } else { $null }
    runtimeCheck = $runtimeCheck
    files = @(Get-ChildItem -LiteralPath $stagePath -File -Recurse | ForEach-Object {
        [pscustomobject]@{
            path = $_.FullName.Substring($stagePath.Length).TrimStart('\', '/')
            size = $_.Length
        }
    })
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath

Write-Host "Package created: $zipPath"
Write-Host "Manifest created: $manifestPath"
if (-not $runtimeCheck.ok) {
    Write-Warning ("Runtime dependency check has warnings. Missing: {0}" -f ($runtimeCheck.missing -join ", "))
}
