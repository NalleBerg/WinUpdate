# Show WinUpdate window by sending it a message
$proc = Get-Process WinUpdate -ErrorAction SilentlyContinue
if ($proc) {
    Add-Type @"
        using System;
        using System.Runtime.InteropServices;
        public class Win32 {
            [DllImport("user32.dll")]
            public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
            [DllImport("user32.dll")]
            public static extern bool SetForegroundWindow(IntPtr hWnd);
            [DllImport("user32.dll")]
            public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
        }
"@
    
    # Try to find WinUpdate window
    $hwnd = [Win32]::FindWindow($null, "WinUpdate - winget GUI-oppdaterer")
    if ($hwnd -eq [IntPtr]::Zero) {
        $hwnd = [Win32]::FindWindow($null, "WinUpdate - winget GUI updater")
    }
    
    if ($hwnd -ne [IntPtr]::Zero) {
        Write-Host "Found WinUpdate window, showing it..."
        [Win32]::ShowWindow($hwnd, 9)  # SW_RESTORE
        [Win32]::ShowWindow($hwnd, 5)  # SW_SHOW
        [Win32]::SetForegroundWindow($hwnd)
        Write-Host "Window should now be visible"
    } else {
        Write-Host "Could not find WinUpdate window"
    }
} else {
    Write-Host "WinUpdate is not running"
}
