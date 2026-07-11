param(
    [Parameter(Mandatory=$false)]
    [string]$ProcessName = "QtNetworkChat",

    [Parameter(Mandatory=$false)]
    [string]$ExePath = "$PSScriptRoot\build-cmake-ninja\QtNetworkChat.exe",

    [Parameter(Mandatory=$false)]
    [string]$WorkingDirectory = "$PSScriptRoot\build-cmake-ninja",

    [Parameter(Mandatory=$false)]
    [string]$OutputPath = "$PSScriptRoot\screenshots\stage8-focused-window.png",

    [Parameter(Mandatory=$false)]
    [int]$StartupDelay = 5,

    [Parameter(Mandatory=$false)]
    [switch]$NoStop
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    public static readonly IntPtr HWND_TOP = IntPtr.Zero;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const int SW_RESTORE = 9;
    public const int SW_SHOW = 5;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

$existing = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
$proc = $null
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
    [Win32]::EnumWindows([Win32+EnumWindowsProc] {
        param($hwnd, $lParam)
        if (-not [Win32]::IsWindowVisible($hwnd)) { return $true }
        $sb = New-Object System.Text.StringBuilder 256
        [Win32]::GetWindowText($hwnd, $sb, 256) | Out-Null
        $title = $sb.ToString()
        if ($title -like "*QtNetworkChat*") {
            $script:hWnd = $hwnd
            return $false
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null

    if ($hWnd -ne [IntPtr]::Zero) { break }
    Start-Sleep -Seconds 1
    $elapsed += 1
}

if ($hWnd -eq [IntPtr]::Zero) {
    Write-Host "Window not found, capturing full screen instead"
    $rect = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $x = $rect.X; $y = $rect.Y; $w = $rect.Width; $h = $rect.Height
} else {
    [Win32]::ShowWindow($hWnd, [Win32]::SW_RESTORE) | Out-Null
    [Win32]::SetWindowPos($hWnd, [Win32]::HWND_TOP, 0, 0, 0, 0, [Win32]::SWP_SHOWWINDOW) | Out-Null
    Start-Sleep -Milliseconds 300
    $rc = New-Object Win32+RECT
    [Win32]::GetWindowRect($hWnd, [ref]$rc) | Out-Null
    $x = $rc.Left; $y = $rc.Top; $w = $rc.Right - $rc.Left; $h = $rc.Bottom - $rc.Top
    if ($w -le 0 -or $h -le 0 -or $x -lt -30000) {
        $rect = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $x = $rect.X; $y = $rect.Y; $w = $rect.Width; $h = $rect.Height
        Write-Host "Window rect invalid, capturing full screen"
    } else {
        Write-Host "Found window at x=$x y=$y w=$w h=$h"
    }
}

if ($w -le 0) { $w = 1 }
if ($h -le 0) { $h = 1 }
$bitmap = New-Object System.Drawing.Bitmap($w, $h)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen([System.Drawing.Point]::new($x, $y), [System.Drawing.Point]::new(0, 0), [System.Drawing.Size]::new($w, $h))
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Host "Saved $OutputPath"

if (-not $NoStop -and $proc -and (-not $existing)) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}
