#ifndef CTRLW_H
#define CTRLW_H

#include <windows.h>

// Install Ctrl+W handler for a window
void InstallCtrlWHandler(HWND hwnd);

// Process keyboard input for Ctrl+W in window procedure
// Returns TRUE if Ctrl+W was handled, FALSE otherwise
BOOL HandleCtrlW(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif // CTRLW_H
