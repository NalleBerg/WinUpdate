#include <windows.h>
#include <iostream>
int main() {
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L".\\build\\WinUpdate.exe";
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::cout << "CreateProcessW failed, err=" << GetLastError() << "\n";
        return 1;
    }
    // Give process some time to create its window
    HWND h = NULL;
    for (int i = 0; i < 100; ++i) {
        h = FindWindowW(L"WinUpdateClass", NULL);
        if (h) break;
        Sleep(100);
    }
    if (!h) {
        std::cout << "Window not found\n";
        return 1;
    }
    PostMessageW(h, WM_APP + 200, (WPARAM)0, (LPARAM)3);
    std::cout << "Posted WM_APP+200 to window\n";
    return 0;
}
