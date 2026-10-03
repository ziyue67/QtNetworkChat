Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -MemberDefinition '
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
' -Name User32 -PassThru -Namespace Win32 | Out-Null

function Capture-Screenshot($processName, $outputPath) {
    $proc = Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) {
        Write-Host "Process not found: $processName"
        return $false
    }
    
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq 0) {
        Write-Host "No main window handle for $processName"
        return $false
    }
    
    [Win32.User32]::ShowWindow($hwnd, 9) | Out-Null
    [Win32.User32]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 800
    
    $rect = New-Object Win32.User32+RECT
    [Win32.User32]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    
    $ww = $rect.Right - $rect.Left
    $hh = $rect.Bottom - $rect.Top
    
    if ($ww -le 0 -or $hh -le 0) {
        Write-Host "Invalid window size"
        return $false
    }
    
    $bmp = New-Object System.Drawing.Bitmap($ww, $hh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($ww, $hh)))
    $bmp.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
    
    Write-Host "Screenshot saved: $outputPath"
    return $true
}

Capture-Screenshot "QtNetworkChat" "D:\C++VS pro\QtNetworkChat\screenshots\01_login.png"
