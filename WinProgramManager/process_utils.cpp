#include "process_utils.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <algorithm>

static std::wstring ToLowerW(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), towlower);
    return r;
}

std::vector<DWORD> FindProcessesByExe(const std::wstring& exeNameOrPath) {
    std::vector<DWORD> results;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return results;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    std::wstring needle = ToLowerW(exeNameOrPath);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring exe = ToLowerW(pe.szExeFile);
            if (!needle.empty()) {
                // match by filename or full path substring
                if (exe.find(needle) != std::wstring::npos || needle.find(exe) != std::wstring::npos) {
                    results.push_back(pe.th32ProcessID);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return results;
}

// Enum windows belonging to a PID
struct EnumData { DWORD pid; std::vector<HWND>* out; };
static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lParam) {
    EnumData* d = (EnumData*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == d->pid && IsWindowVisible(hwnd)) {
        d->out->push_back(hwnd);
    }
    return TRUE;
}

std::vector<HWND> EnumProcessTopWindows(DWORD pid) {
    std::vector<HWND> result;
    EnumData d; d.pid = pid; d.out = &result;
    EnumWindows(EnumWndProc, (LPARAM)&d);
    return result;
}

bool IsProcessRunning(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    DWORD code = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return code == WAIT_TIMEOUT;
}

bool SendGracefulClose(DWORD pid, DWORD timeoutMs) {
    auto windows = EnumProcessTopWindows(pid);
    if (windows.empty()) return false;

    for (HWND hwnd : windows) {
        SendMessageTimeoutW(hwnd, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, NULL);
    }

    // Wait up to timeoutMs for process to exit
    DWORD waited = 0;
    const DWORD sleepStep = 200;
    while (waited < timeoutMs) {
        if (!IsProcessRunning(pid)) return true;
        Sleep(sleepStep);
        waited += sleepStep;
    }
    return !IsProcessRunning(pid);
}

bool ForceKillProcess(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    WaitForSingleObject(h, 2000);
    CloseHandle(h);
    return ok == TRUE;
}
