param(
    [string]$AppName = "Vulkan SDK 1.4.328.1",
    [string]$Available = "1.4.335.0",
    [string]$WindowClass = "WinUpdateClass"
)

$payload = "WUP_SKIP`n$AppName`n$Available`n"

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Win32 {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern IntPtr SendMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, ref COPYDATASTRUCT lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct COPYDATASTRUCT { public IntPtr dwData; public int cbData; public IntPtr lpData; }

    public static bool SendCopyData(string className, string payload) {
        IntPtr hwnd = FindWindowW(className, null);
        if (hwnd == IntPtr.Zero) return false;
        byte[] bytes = System.Text.Encoding.ASCII.GetBytes(payload);
        IntPtr pData = Marshal.AllocHGlobal(bytes.Length + 1);
        Marshal.Copy(bytes, 0, pData, bytes.Length);
        Marshal.WriteByte(pData, bytes.Length, 0);
        COPYDATASTRUCT cds = new COPYDATASTRUCT() { dwData = IntPtr.Zero, cbData = bytes.Length + 1, lpData = pData };
        try {
            SendMessageW(hwnd, 0x4, IntPtr.Zero, ref cds);
        } finally {
            Marshal.FreeHGlobal(pData);
        }
        return true;
    }
}
'@

Add-Type -TypeDefinition $cs -Language CSharp -ErrorAction Stop

if (-not [Win32]::SendCopyData($WindowClass, $payload)) {
    Write-Error "Failed to find window with class '$WindowClass'. Is WinUpdate running?"
    exit 1
}
Write-Output "WM_COPYDATA sent to window class '$WindowClass' with payload: $payload"
