param(
    [string]$BuildDir = "build",
    [string]$PackageDir = "dist",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $PackageDir
$versionMatch = [regex]::Match(
    (Get-Content -LiteralPath (Join-Path $repoRoot "CMakeLists.txt") -Raw),
    'project\s*\(\s*QtNetworkChat\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
)
if (-not $versionMatch.Success) {
    throw "Could not determine the QtNetworkChat version"
}
$version = $versionMatch.Groups[1].Value
$stagePath = Join-Path $outputPath "QtNetworkChat-$version-win-x64"

& (Join-Path $PSScriptRoot "package-windows.ps1") `
    -BuildDir $BuildDir -PackageDir $PackageDir -Configuration $Configuration `
    -SkipBuild -FailOnMissingRuntime

foreach ($style in @("style-qqnt.qss", "style-qqnt-dark.qss")) {
    $uiDir = Join-Path $stagePath "ui"
    New-Item -ItemType Directory -Force -Path $uiDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot "ui\$style") -Destination $uiDir -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination $stagePath -Force

$cryptoName = "libcrypto-3-x64.dll"
$cryptoCandidates = New-Object System.Collections.Generic.List[string]
$cryptoCommand = Get-Command $cryptoName -ErrorAction SilentlyContinue
if ($cryptoCommand) {
    $cryptoCandidates.Add($cryptoCommand.Source)
}
foreach ($root in @($env:OPENSSL_ROOT_DIR, "$env:ProgramFiles\OpenSSL", "$env:ProgramFiles\OpenSSL-Win64")) {
    if (-not [string]::IsNullOrWhiteSpace($root)) {
        $cryptoCandidates.Add((Join-Path $root "bin\$cryptoName"))
    }
}
$cachePath = Join-Path $buildPath "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
    $cacheEntry = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^OPENSSL_CRYPTO_LIBRARY(?::[^=]+)?=' } |
        Select-Object -First 1
    if ($cacheEntry) {
        $library = ($cacheEntry -split '=', 2)[1]
        $directory = Split-Path -Parent $library
        while (-not [string]::IsNullOrWhiteSpace($directory)) {
            $cryptoCandidates.Add((Join-Path $directory "bin\$cryptoName"))
            $cryptoCandidates.Add((Join-Path $directory $cryptoName))
            $parent = Split-Path -Parent $directory
            if ($parent -eq $directory) { break }
            $directory = $parent
        }
    }
}
$cryptoSource = $cryptoCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $cryptoSource) {
    throw "Required OpenSSL runtime $cryptoName was not found"
}
Copy-Item -LiteralPath $cryptoSource -Destination $stagePath -Force

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe was not found; cannot locate redistributable MSVC runtime"
}
$vsInstall = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
$redistRoot = Join-Path $vsInstall "VC\Redist\MSVC"
$crtDirectory = Get-ChildItem -LiteralPath $redistRoot -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
    Select-Object -First 1
if (-not $crtDirectory) {
    throw "Could not locate the MSVC 2022 redistributable runtime"
}
foreach ($name in @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")) {
    $source = Join-Path $crtDirectory $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required MSVC runtime $name was not found"
    }
    Copy-Item -LiteralPath $source -Destination $stagePath -Force
}

$isccCommand = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
$isccPath = if ($isccCommand) { $isccCommand.Source } else {
    Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
}
if (-not (Test-Path -LiteralPath $isccPath -PathType Leaf)) {
    throw "Inno Setup 6 compiler ISCC.exe was not found"
}
& $isccPath "/DAppVersion=$version" "/DStageDir=$stagePath" "/DOutputDir=$outputPath" `
    (Join-Path $repoRoot "packaging\windows\QtNetworkChat.iss")
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
}
$installer = Join-Path $outputPath "QtNetworkChat-$version-win-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer was not produced: $installer"
}
Write-Host "Installer created: $installer"
