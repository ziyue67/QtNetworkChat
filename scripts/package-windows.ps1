param(
    [string]$BuildDir = "build-qt6-mingw",
    [string]$Configuration = "Release",
    [string]$PackageDir = "dist"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $repoRoot $BuildDir
$packageRoot = Join-Path $repoRoot $PackageDir
$stagePath = Join-Path $packageRoot "QtNetworkChat-win-x64"
$zipPath = Join-Path $packageRoot "QtNetworkChat-win-x64.zip"

Write-Host "Building QtNetworkChat from $repoRoot"
cmake --build $buildPath --config $Configuration

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
    Write-Host "Running $($deployTool.Source)"
    & $deployTool.Source --release --compiler-runtime (Join-Path $stagePath "QtNetworkChat.exe")
} else {
    Write-Warning "windeployqt.exe was not found. The package contains the app exe and README only."
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath

Write-Host "Package created: $zipPath"
