# Force show WinUpdate window using EnumWindows
$proc = Get-Process WinUpdate -ErrorAction SilentlyContinue
if (!$proc) {
    Write-Host "WinUpdate is not running"
    exit
}

Add-Type @"
    using System;
    using System.Runtime.InteropServices;
    using System.Text;
    
    public class Win32 {
        public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
        
        [DllImport("user32.dll")]
        public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);
        
        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
        
        [DllImport("user32.dll")]
        public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
        
        [DllImport("user32.dll")]
        public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        
        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);
        
        [DllImport("user32.dll")]
        public static extern bool IsWindowVisible(IntPtr hWnd);
    }
"@

$targetPid = $proc.Id
$foundWindows = @()

$callback = {
    param($hwnd, $lParam)
    $pid = 0
    [Win32]::GetWindowThreadProcessId($hwnd, [ref]$pid) | Out-Null
    
    if ($pid -eq $script:targetPid) {
        $sb = New-Object System.Text.StringBuilder 256
        [Win32]::GetWindowText($hwnd, $sb, $sb.Capacity) | Out-Null
        $title = $sb.ToString()
        $visible = [Win32]::IsWindowVisible($hwnd)
        
        $script:foundWindows += [PSCustomObject]@{
            Handle = $hwnd
            Title = $title
            Visible = $visible
        }
    }
    return $true
}

[Win32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null

Write-Host "Found $($foundWindows.Count) windows for WinUpdate process:"
$foundWindows | Format-Table Handle, Title, Visible

# Show the main window (the one with a title)
$mainWindow = $foundWindows | Where-Object { $_.Title -ne "" } | Select-Object -First 1

if ($mainWindow) {
    Write-Host "Showing window: $($mainWindow.Title)"
    [Win32]::ShowWindow($mainWindow.Handle, 9)  # SW_RESTORE
    [Win32]::ShowWindow($mainWindow.Handle, 5)  # SW_SHOW
    [Win32]::SetForegroundWindow($mainWindow.Handle)
    Write-Host "Done!"
} else {
    Write-Host "No window with title found. Trying first window..."
    if ($foundWindows.Count -gt 0) {
        [Win32]::ShowWindow($foundWindows[0].Handle, 9)
        [Win32]::ShowWindow($foundWindows[0].Handle, 5)
        [Win32]::SetForegroundWindow($foundWindows[0].Handle)
    }
}
