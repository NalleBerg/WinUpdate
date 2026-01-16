#include "ctrlw.h"

void InstallCtrlWHandler(HWND hwnd) {
    // Nothing to install - handled in window procedure
    // This function exists for future expansion if needed
}

BOOL HandleCtrlW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        if (wParam == 'W' || wParam == 'w') {
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl+W pressed - close the window
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return TRUE;
            }
        }
    }
    return FALSE;
}
