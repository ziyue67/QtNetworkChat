param(
    [Parameter(Mandatory=$false)]
    [string]$ProcessName = "QtNetworkChat",

    [Parameter(Mandatory=$false)]
    [string]$ExePath = "$PSScriptRoot\build\Release\QtNetworkChat.exe",

    [Parameter(Mandatory=$false)]
    [string]$WorkingDirectory = "",

    [Parameter(Mandatory=$false)]
    [string]$OutputPath = "$PSScriptRoot\screenshots\desktop-window.png",

    [Parameter(Mandatory=$false)]
    [int]$StartupDelay = 5,

    [Parameter(Mandatory=$false)]
    [switch]$NoStop,

    [Parameter(Mandatory=$false)]
    [switch]$NewInstance
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    $WorkingDirectory = Split-Path -Parent $ExePath
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    public static readonly IntPtr HWND_TOP = IntPtr.Zero;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const int SW_RESTORE = 9;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

$existing = if (-not $NewInstance) {
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
}
$proc = $null
try {
    if ($existing) {
        $proc = $existing
        Write-Host "Using existing process Id=$($proc.Id)"
    } else {
        $proc = Start-Process -FilePath $ExePath -WorkingDirectory $WorkingDirectory -PassThru -WindowStyle Normal
        Start-Sleep -Seconds $StartupDelay
        Write-Host "Started process Id=$($proc.Id)"
    }

    $timeout = 30
    $elapsed = 0
    $hWnd = [IntPtr]::Zero
    while ($elapsed -lt $timeout) {
        $script:largestWindowArea = 0
        [Win32]::EnumWindows([Win32+EnumWindowsProc] {
            param($hwnd, $lParam)
            if (-not [Win32]::IsWindowVisible($hwnd)) { return $true }
            [uint32]$windowProcessId = 0
            [Win32]::GetWindowThreadProcessId($hwnd, [ref]$windowProcessId) | Out-Null
            if ($windowProcessId -ne $proc.Id) { return $true }
            $windowRect = New-Object Win32+RECT
            if (-not [Win32]::GetWindowRect($hwnd, [ref]$windowRect)) { return $true }
            $area = ($windowRect.Right - $windowRect.Left) * ($windowRect.Bottom - $windowRect.Top)
            if ($area -gt $script:largestWindowArea) {
                $script:largestWindowArea = $area
                $script:hWnd = $hwnd
            }
            return $true
        }, [IntPtr]::Zero) | Out-Null

        if ($hWnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Seconds 1
        $elapsed += 1
    }

    if ($hWnd -eq [IntPtr]::Zero) {
        throw "No visible window found for process $($proc.Id)"
    } else {
        [Win32]::ShowWindow($hWnd, [Win32]::SW_RESTORE) | Out-Null
        $positionFlags = [Win32]::SWP_SHOWWINDOW -bor [Win32]::SWP_NOMOVE -bor [Win32]::SWP_NOSIZE
        if (-not [Win32]::SetWindowPos($hWnd, [Win32]::HWND_TOP, 0, 0, 0, 0, $positionFlags)) {
            throw "Unable to raise target window"
        }
        Start-Sleep -Milliseconds 300
        $rc = New-Object Win32+RECT
        [Win32]::GetWindowRect($hWnd, [ref]$rc) | Out-Null
        $x = $rc.Left; $y = $rc.Top; $w = $rc.Right - $rc.Left; $h = $rc.Bottom - $rc.Top
        if ($w -le 0 -or $h -le 0 -or $x -lt -30000) {
            throw "Target window has an invalid capture rectangle"
        } else {
            Write-Host "Found window at x=$x y=$y w=$w h=$h"
        }
    }

    $bitmap = New-Object System.Drawing.Bitmap($w, $h)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen([System.Drawing.Point]::new($x, $y), [System.Drawing.Point]::new(0, 0), [System.Drawing.Size]::new($w, $h))
        $outputDirectory = Split-Path -Parent ([System.IO.Path]::GetFullPath($OutputPath))
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    Write-Host "Saved $OutputPath"
} finally {
    if (-not $NoStop -and $proc -and (-not $existing)) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
