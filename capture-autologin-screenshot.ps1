param(
    [string]$ExePath = "$PSScriptRoot\bin\QtNetworkChat.exe",
    [string]$WorkingDirectory = "$PSScriptRoot\bin",
    [string]$OutputPath = "$PSScriptRoot\screenshots\autologin-verify.png",
    [int]$StartupDelay = 8
)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W32 {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    public const int SW_RESTORE = 9;
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Start-Process -FilePath $ExePath -WorkingDirectory $WorkingDirectory -PassThru -WindowStyle Normal
Start-Sleep -Seconds $StartupDelay
$hWnd = [IntPtr]::Zero
$maxArea = 0
[W32]::EnumWindows([W32+EnumProc]{
    param($hwnd, $lParam)
    if (-not [W32]::IsWindowVisible($hwnd)) { return $true }
    $sb = New-Object System.Text.StringBuilder 256
    [W32]::GetWindowText($hwnd, $sb, 256) | Out-Null
    $title = $sb.ToString()
    if (-not ($title -like "*QtNetworkChat*")) { return $true }
    $r = New-Object W32+RECT
    [W32]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $area = ($r.Right - $r.Left) * ($r.Bottom - $r.Top)
    if ($area -gt $maxArea) { $maxArea = $area; $script:hWnd = $hwnd }
    return $true
}, [IntPtr]::Zero) | Out-Null
if ($hWnd -eq [IntPtr]::Zero) { Write-Error "No QtNetworkChat window found"; return }
[W32]::SetForegroundWindow($hWnd) | Out-Null
[W32]::ShowWindow($hWnd, [W32]::SW_RESTORE) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object W32+RECT
[W32]::GetWindowRect($hWnd, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, [System.Drawing.Size]::new($w, $h))
$g.Dispose()
$dir = Split-Path $OutputPath
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bmp.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "Saved $OutputPath (${w}x${h})"
