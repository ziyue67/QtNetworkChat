[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\tauri-qqnt\src-tauri\binaries'),
    [string]$Triplet = $env:TAURI_TARGET_TRIPLE
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Triplet)) {
    try {
        $hostLine = & rustc -vV 2>$null | Where-Object { $_ -like 'host:*' } | Select-Object -First 1
        if ($hostLine) {
            $Triplet = ($hostLine -replace '^host:\s*', '').Trim()
        }
    } catch {
        $Triplet = $null
    }
}

if ([string]::IsNullOrWhiteSpace($Triplet)) {
    $Triplet = 'x86_64-pc-windows-msvc'
}

$resolvedBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$resolvedOutDir = [System.IO.Path]::GetFullPath($OutDir)

New-Item -ItemType Directory -Force -Path $resolvedOutDir | Out-Null

$sidecars = @('QQNTEngine', 'QQNTServer')
$configDirs = @('', 'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')

foreach ($sidecar in $sidecars) {
    $source = $null

    foreach ($configDir in $configDirs) {
        $candidate = if ([string]::IsNullOrWhiteSpace($configDir)) {
            Join-Path $resolvedBuildDir "$sidecar.exe"
        } else {
            Join-Path (Join-Path $resolvedBuildDir $configDir) "$sidecar.exe"
        }

        if (Test-Path -LiteralPath $candidate) {
            $source = $candidate
            break
        }
    }

    if (-not $source) {
        throw "Missing sidecar '$sidecar.exe' under '$resolvedBuildDir'. Build QQNTEngine and QQNTServer first, or pass -BuildDir."
    }

    $destination = Join-Path $resolvedOutDir "$sidecar-$Triplet.exe"
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Host "Copied $source -> $destination"
}
