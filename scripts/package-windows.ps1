param(
    [string]$BuildDir = "build-qt6-mingw",
    [string]$Configuration = "Release",
    [string]$PackageDir = "build-qt6-mingw\\release-package",
    [string]$QtRoot,
    [string]$PostgresBinDir = "D:\Program Files\PostgreSQL\17\bin",
    [switch]$SkipBuild,
    [switch]$NoDeploy,
    [switch]$IncludePostgresSql,
    [switch]$FailOnMissingPostgresSql,
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

function Resolve-CMakeCommand {
    $cmakeCommand = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        return $cmakeCommand.Source
    }

    $fallback = "D:\Qt\Tools\CMake_64\bin\cmake.exe"
    if (Test-Path -LiteralPath $fallback -PathType Leaf) {
        return $fallback
    }

    throw "cmake.exe was not found in PATH and fallback D:\Qt\Tools\CMake_64\bin\cmake.exe does not exist."
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

function Resolve-QtRuntimeRoot {
    param(
        [string]$ExplicitQtRoot,
        [string]$BuildPath,
        [string]$DeployToolPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitQtRoot) -and (Test-Path -LiteralPath $ExplicitQtRoot -PathType Container)) {
        return (Resolve-Path -LiteralPath $ExplicitQtRoot).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($DeployToolPath)) {
        $candidate = Split-Path -Parent $DeployToolPath
        if (Test-Path -LiteralPath (Join-Path $candidate "plugins\sqldrivers")) {
            return $candidate
        }
    }

    $cachePath = Join-Path $BuildPath "CMakeCache.txt"
    if (Test-Path -LiteralPath $cachePath) {
        $cacheContent = Get-Content -LiteralPath $cachePath
        foreach ($line in $cacheContent) {
            if ($line -match '^(?:CMAKE_PREFIX_PATH|QT_DIR|Qt6_DIR)(?::[^=]+)?=(.+)$') {
                $value = $Matches[1].Trim()
                if ($value -match '[\\/]lib[\\/]cmake[\\/]Qt6$') {
                    $value = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $value))
                }
                if (Test-Path -LiteralPath (Join-Path $value "plugins\sqldrivers")) {
                    return (Resolve-Path -LiteralPath $value).Path
                }
            }
        }
    }

    $qmake = Get-Command "qmake.exe" -ErrorAction SilentlyContinue
    if ($qmake) {
        $candidate = Split-Path -Parent $qmake.Source
        if (Test-Path -LiteralPath (Join-Path $candidate "plugins\sqldrivers")) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Copy-PostgresSqlRuntime {
    param(
        [string]$StagePath,
        [string]$QtRuntimeRoot,
        [string]$PostgresBinDir
    )

    $copied = @()
    $missing = @()
    $pluginSource = $null
    $pluginTarget = $null

    if ([string]::IsNullOrWhiteSpace($QtRuntimeRoot)) {
        $missing += "Qt runtime root"
    } else {
        $candidate = Join-Path $QtRuntimeRoot "plugins\sqldrivers\qsqlpsql.dll"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $pluginSource = (Resolve-Path -LiteralPath $candidate).Path
            $targetDir = Join-Path $StagePath "sqldrivers"
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
            $pluginTarget = Join-Path $targetDir "qsqlpsql.dll"
            Copy-Item -LiteralPath $pluginSource -Destination $pluginTarget -Force
            $copied += [pscustomobject]@{
                name = "qsqlpsql.dll"
                source = $pluginSource
                path = "sqldrivers/qsqlpsql.dll"
                size = (Get-Item -LiteralPath $pluginTarget).Length
            }
        } else {
            $missing += "qsqlpsql.dll"
        }
    }

    $postgresDeps = @(
        "libpq.dll",
        "libssl-3-x64.dll",
        "libcrypto-3-x64.dll",
        "libintl-9.dll",
        "libiconv-2.dll",
        "zlib1.dll"
    )
    foreach ($name in $postgresDeps) {
        $source = if ([string]::IsNullOrWhiteSpace($PostgresBinDir)) { $null } else { Join-Path $PostgresBinDir $name }
        if (-not [string]::IsNullOrWhiteSpace($source) -and (Test-Path -LiteralPath $source -PathType Leaf)) {
            $target = Join-Path $StagePath $name
            Copy-Item -LiteralPath $source -Destination $target -Force
            $copied += [pscustomobject]@{
                name = $name
                source = (Resolve-Path -LiteralPath $source).Path
                path = $name
                size = (Get-Item -LiteralPath $target).Length
            }
        } else {
            $missing += $name
        }
    }

    return [pscustomobject]@{
        requested = $true
        ok = $missing.Count -eq 0
        qtRoot = $QtRuntimeRoot
        postgresBinDir = if ([string]::IsNullOrWhiteSpace($PostgresBinDir) -or -not (Test-Path -LiteralPath $PostgresBinDir -PathType Container)) { $PostgresBinDir } else { (Resolve-Path -LiteralPath $PostgresBinDir).Path }
        pluginSource = $pluginSource
        pluginTarget = if ($pluginTarget) { $pluginTarget.Substring($StagePath.Length).TrimStart('\', '/') } else { $null }
        copiedFiles = $copied
        missing = $missing
    }
}

function Find-BuiltExecutable([string]$BuildPath) {
    $candidates = New-Object System.Collections.Generic.List[System.IO.FileInfo]
    $excludedSegments = @(
        "\release-package\",
        "\qpsql-package-probe\",
        "\local-release-review\",
        "\release-delivery-handoff\"
    )

    function Test-IsExcludedExecutable([string]$CandidatePath) {
        $normalized = $CandidatePath.ToLowerInvariant()
        foreach ($segment in $excludedSegments) {
            if ($normalized.Contains($segment)) {
                return $true
            }
        }
        return $false
    }

    foreach ($relative in @(
        "QtNetworkChat.exe",
        "Release\\QtNetworkChat.exe",
        "src\\QtNetworkChat.exe",
        "src\\Release\\QtNetworkChat.exe"
    )) {
        $candidatePath = Join-Path $BuildPath $relative
        if ((Test-Path -LiteralPath $candidatePath -PathType Leaf) -and -not (Test-IsExcludedExecutable $candidatePath)) {
            $candidates.Add((Get-Item -LiteralPath $candidatePath))
        }
    }

    try {
        Get-ChildItem -Path $BuildPath -Recurse -Filter "QtNetworkChat.exe" -File -ErrorAction SilentlyContinue |
            Where-Object { -not (Test-IsExcludedExecutable $_.FullName) } |
            ForEach-Object { $candidates.Add($_) }
    } catch {
    }

    $best = $candidates |
        Sort-Object FullName -Unique |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    return $best
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
$cmakeExe = Resolve-CMakeCommand
$stageName = "QtNetworkChat-$version-win-x64"
$stagePath = Join-Path $packageRoot $stageName
$zipPath = Join-Path $packageRoot "$stageName.zip"
$manifestPath = Join-Path $stagePath "manifest.json"

Write-Host "Packaging QtNetworkChat $version from $repoRoot"
if (-not $SkipBuild) {
    & $cmakeExe --build $buildPath --config $Configuration
} else {
    Write-Host "Skipping build because -SkipBuild was specified"
}

$exe = Find-BuiltExecutable $buildPath

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

$postgresSqlRuntime = [pscustomobject]@{
    requested = [bool]$IncludePostgresSql
    ok = -not [bool]$IncludePostgresSql
    qtRoot = $null
    postgresBinDir = $PostgresBinDir
    pluginSource = $null
    pluginTarget = $null
    copiedFiles = @()
    missing = @()
}
if ($IncludePostgresSql) {
    $resolvedQtRoot = Resolve-QtRuntimeRoot -ExplicitQtRoot $QtRoot -BuildPath $buildPath -DeployToolPath $deployToolPath
    Write-Host "Collecting PostgreSQL Qt SQL runtime"
    $postgresSqlRuntime = Copy-PostgresSqlRuntime -StagePath $stagePath -QtRuntimeRoot $resolvedQtRoot -PostgresBinDir $PostgresBinDir
    if ($FailOnMissingPostgresSql -and -not $postgresSqlRuntime.ok) {
        throw ("PostgreSQL SQL runtime check failed. Missing: {0}" -f ($postgresSqlRuntime.missing -join ", "))
    }
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
    postgresSqlRuntime = $postgresSqlRuntime
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
