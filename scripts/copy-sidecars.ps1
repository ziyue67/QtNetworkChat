[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutDir,
    [string]$Triplet = $env:TAURI_TARGET_TRIPLE,
    [string[]]$Sidecars = @('QQNTEngine', 'QQNTServer')
)

$ErrorActionPreference = 'Stop'

$scriptRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptRoot)) {
    $scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrWhiteSpace($scriptRoot)) {
    $scriptRoot = (Get-Location).Path
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $scriptRoot '..\build'
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $scriptRoot '..\tauri-qqnt\src-tauri\binaries'
}

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

$configDirs = @('', 'Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')

foreach ($sidecar in $Sidecars) {
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
        throw "Missing sidecar '$sidecar.exe' under '$resolvedBuildDir'. Build $sidecar first, or pass -BuildDir."
    }

    $destination = Join-Path $resolvedOutDir "$sidecar-$Triplet.exe"
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Host "Copied $source -> $destination"
}
