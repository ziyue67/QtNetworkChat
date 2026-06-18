[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutDir,
    [string]$QtBinDir,
    [string]$QtRoot,
    [string]$Triplet = $env:TAURI_TARGET_TRIPLE,
    [string[]]$Sidecars = @('QQNTEngine', 'QQNTServer'),
    [switch]$NoRuntimeDeploy
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

function Resolve-OptionalDirectory {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (Test-Path -LiteralPath $fullPath -PathType Container) {
        return (Resolve-Path -LiteralPath $fullPath).Path
    }

    return $null
}

function Get-UniqueExistingDirectories {
    param([string[]]$Paths)

    $seen = @{}
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($path in $Paths) {
        $resolved = Resolve-OptionalDirectory $path
        if ($resolved -and -not $seen.ContainsKey($resolved.ToLowerInvariant())) {
            $seen[$resolved.ToLowerInvariant()] = $true
            $result.Add($resolved)
        }
    }
    return @($result)
}

function Resolve-DeployTool {
    param([string[]]$RuntimeSourceDirs)

    foreach ($dir in $RuntimeSourceDirs) {
        $candidate = Join-Path $dir 'windeployqt.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command 'windeployqt.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Copy-FirstExistingFile {
    param(
        [string[]]$Directories,
        [string]$RelativePath,
        [string]$DestinationPath
    )

    foreach ($dir in $Directories) {
        $candidate = Join-Path $dir $RelativePath
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestinationPath) | Out-Null
            Copy-Item -LiteralPath $candidate -Destination $DestinationPath -Force
            return $true
        }
    }

    return $false
}

function Deploy-SidecarRuntime {
    param(
        [string]$ExecutablePath,
        [string]$SourceDir
    )

    if ($NoRuntimeDeploy) {
        Write-Host "Skipping Qt runtime deployment because -NoRuntimeDeploy was specified"
        return
    }

    $qtBinCandidates = @(
        $QtBinDir,
        $(if ($QtRoot) { Join-Path $QtRoot 'bin' } else { $null }),
        $SourceDir
    )
    $runtimeSourceDirs = Get-UniqueExistingDirectories $qtBinCandidates
    $deployTool = Resolve-DeployTool $runtimeSourceDirs
    if ($deployTool) {
        Write-Host "Deploying Qt runtime for $ExecutablePath with $deployTool"
        & $deployTool --release --compiler-runtime --no-translations $ExecutablePath
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt failed for $ExecutablePath with exit code $LASTEXITCODE."
        }
    } else {
        Write-Warning "windeployqt.exe was not found. Copying the minimal QQNT sidecar Qt runtime files."
    }

    $destinationDir = Split-Path -Parent $ExecutablePath
    $requiredDlls = @('Qt6Core.dll', 'Qt6Network.dll', 'Qt6Sql.dll')
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($dll in $requiredDlls) {
        $destination = Join-Path $destinationDir $dll
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            if (-not (Copy-FirstExistingFile -Directories $runtimeSourceDirs -RelativePath $dll -DestinationPath $destination)) {
                $missing.Add($dll)
            }
        }
    }

    $pluginSourceDirs = @()
    foreach ($dir in $runtimeSourceDirs) {
        $pluginSourceDirs += Join-Path (Split-Path -Parent $dir) 'plugins'
        $pluginSourceDirs += Join-Path $dir 'plugins'
    }
    if ($QtRoot) {
        $pluginSourceDirs += Join-Path $QtRoot 'plugins'
    }
    $pluginSourceDirs = Get-UniqueExistingDirectories $pluginSourceDirs
    $sqliteDestination = Join-Path $destinationDir 'sqldrivers\qsqlite.dll'
    if (-not (Test-Path -LiteralPath $sqliteDestination -PathType Leaf)) {
        if (-not (Copy-FirstExistingFile -Directories $pluginSourceDirs -RelativePath 'sqldrivers\qsqlite.dll' -DestinationPath $sqliteDestination)) {
            $missing.Add('sqldrivers/qsqlite.dll')
        }
    }

    if ($missing.Count -gt 0) {
        throw ("Missing Qt sidecar runtime files: {0}. Pass -QtBinDir or run from a Qt command prompt." -f ($missing -join ', '))
    }
}

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
    Deploy-SidecarRuntime -ExecutablePath $destination -SourceDir (Split-Path -Parent $source)
}
