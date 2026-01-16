$exe = Start-Process -FilePath '.\build\WinUpdate.exe' -PassThru
Add-Type -MemberDefinition @'
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam);
}
'@ -Name Win32 -Namespace Win32PInvoke
$hwnd = [IntPtr]::Zero
for ($i=0; $i -lt 100 -and $hwnd -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $hwnd = [Win32PInvoke.Win32]::FindWindowW('WinUpdateClass', $null) }
if ($hwnd -eq [IntPtr]::Zero) { Write-Host 'Window not found'; exit 1 }
$WM_APP = 0x8000
[Win32PInvoke.Win32]::PostMessageW($hwnd, $WM_APP + 200, [UIntPtr]0, [IntPtr]3)
Start-Sleep -Seconds 1
Get-Content -Path 'wup_run_log.txt' -Tail 40 -Raw
