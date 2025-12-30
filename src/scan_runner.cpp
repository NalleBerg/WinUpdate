// Centralized scanner implementation - moved here so all scanning/parsing
// logic lives in one compilation unit as requested.

// Simple scanner: run `winget upgrade` and parse the aligned table by columns.
#include "scan_runner.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <regex>
#include <filesystem>
#include "winget_versions.h"

static inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)isspace((unsigned char)s[a])) ++a;
    while (b > a && (unsigned char)isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

static std::string WideToUtf8_local(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::pair<int,std::string> RunProcessCaptureExitCodeLocal(const std::wstring &cmd, int timeoutMs = 5000) {
    std::pair<int,std::string> res = {-1, std::string()};
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return res;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite; si.hStdInput = NULL;
    PROCESS_INFORMATION pi{};
    std::wstring cmdCopy = cmd;
    if (!CreateProcessW(NULL, &cmdCopy[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(hWrite); CloseHandle(hRead); return res; }
    CloseHandle(hWrite);
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); res.first = -2; }
    else { DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode); res.first = (int)exitCode; }
    std::string out; const DWORD bufSize = 4096; char buf[bufSize]; DWORD read = 0;
    while (ReadFile(hRead, buf, bufSize, &read, NULL) && read > 0) out.append(buf, buf + read);
    CloseHandle(hRead); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    res.second = out; return res;
}

bool ScanAndPopulateMaps(std::unordered_map<std::string,std::string> &avail, std::unordered_map<std::string,std::string> &inst) {
    avail.clear(); inst.clear();
    try {
        // Use centralized, normalized parsing implemented in winget_versions.cpp
        avail = MapAvailableVersions();
        inst = MapInstalledVersions();
        try { AppendLog(std::string("ScanAndPopulateMaps: avail count=") + std::to_string((int)avail.size()) + " inst count=" + std::to_string((int)inst.size()) + "\n"); } catch(...) {}
        return !avail.empty() && !inst.empty();
    } catch(...) { return false; }
}

bool RunFullScan(std::vector<std::pair<std::string,std::string>> &outResults,
                 std::unordered_map<std::string,std::string> &avail,
                 std::unordered_map<std::string,std::string> &inst,
                 int timeoutMs) {
    outResults.clear(); avail.clear(); inst.clear();
    try {
        avail = MapAvailableVersions();
        inst = MapInstalledVersions();
        if (avail.empty() || inst.empty()) return false;
        // Build outResults using normalized ids as keys; display name uses id (can be improved later)
        for (auto &p : avail) outResults.emplace_back(p.first, p.first);
        try { AppendLog(std::string("RunFullScan: avail count=") + std::to_string((int)avail.size()) + " inst count=" + std::to_string((int)inst.size()) + "\n"); } catch(...) {}
        return true;
    } catch(...) { return false; }
}
