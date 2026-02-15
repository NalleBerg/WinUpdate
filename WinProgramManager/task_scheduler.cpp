#include "task_scheduler.h"
#include "ini_utils.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdio>

// Forward declarations
static std::wstring RunCommandCapture(const std::wstring& cmd, int* exitCode = nullptr);

static void AppendFirstRunLogInternal(const std::wstring& msg) {
    // Write to Roaming AppData
    PWSTR roamingPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &roamingPath))) {
        std::wstring dir(roamingPath);
        CoTaskMemFree(roamingPath);
        dir += L"\\WinProgramManager";
        CreateDirectoryW(dir.c_str(), NULL);
        std::wstring logPath = dir + L"\\first_run_debug.txt";
        HANDLE h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t ts[64]; swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring line = std::wstring(L"[") + ts + L"] " + msg + L"\r\n";
            DWORD written = 0;
            WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &written, NULL);
            CloseHandle(h);
        }
    }

    // Write copy next to executable (workspace/package folder)
    wchar_t exePathBuf[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePathBuf, MAX_PATH) > 0) {
        std::wstring exePath(exePathBuf);
        size_t pos = exePath.find_last_of(L"\\/");
        std::wstring folder = (pos == std::wstring::npos) ? std::wstring(L".") : exePath.substr(0, pos);
        std::wstring wsLogPath = folder + L"\\first_run_debug_workspace.txt";
        HANDLE hw = CreateFileW(wsLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hw != INVALID_HANDLE_VALUE) {
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t ts[64]; swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring line = std::wstring(L"[") + ts + L"] " + msg + L"\r\n";
            DWORD written = 0;
            WriteFile(hw, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &written, NULL);
            CloseHandle(hw);
        }
    }
}

void TryCreateUpdaterTaskIfFirstRun(const std::wstring& currentLang) {
    PWSTR roamingPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &roamingPath))) {
        AppendFirstRunLogInternal(L"SHGetKnownFolderPath FAILED");
        return;
    }
    std::wstring dir(roamingPath);
    CoTaskMemFree(roamingPath);
    dir += L"\\WinProgramManager";
    std::wstring iniPath = dir + L"\\WinProgramManager.ini";

    // If INI file already exists at all, do not touch it on startup
    DWORD attr = GetFileAttributesW(iniPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        // INI already present -> respect existing settings, do nothing
        AppendFirstRunLogInternal(L"INI already present; skipping first-run task creation");
        return;
    }

    // Ensure directory exists
    if (!CreateDirectoryW(dir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            AppendFirstRunLogInternal(L"CreateDirectoryW failed: " + std::to_wstring(err));
        }
    }

    // If INI does not exist, create it now so we have a stable state even if
    // task creation fails. Mark UpdaterTaskCreated=0 initially; will update to 1 on success.
    DWORD attrCheck = GetFileAttributesW(iniPath.c_str());
    if (attrCheck == INVALID_FILE_ATTRIBUTES) {
        // write initial INI entries as readable UTF-8 so users can edit them with common editors
        if (WriteSettingsIniUtf8(iniPath, currentLang, L"0")) {
            AppendFirstRunLogInternal(L"Initial INI written (UTF-8) UpdaterTaskCreated=0");
            AppendFirstRunLogInternal(L"INI path: " + iniPath);
        } else {
            AppendFirstRunLogInternal(L"Initial INI write failed (UTF-8)");
            AppendFirstRunLogInternal(L"Attempted INI path: " + iniPath);
        }
    }

    // Build XML in program folder next to exe
    wchar_t exePathBuf[MAX_PATH]; GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::wstring exePath(exePathBuf);
    size_t pos = exePath.find_last_of(L"\\/");
    std::wstring folder = (pos == std::wstring::npos) ? std::wstring(L".") : exePath.substr(0, pos);
    std::wstring xmlPath = folder + L"\\wpm_updater_task.xml";

    // Build a minimal XML (UTF-16)
    std::wstring xml;
    xml += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n";
    xml += L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n";
    xml += L"  <RegistrationInfo><Author>WinProgramSuite</Author></RegistrationInfo>\r\n";
    xml += L"  <Triggers>\r\n";
    xml += L"    <CalendarTrigger>\r\n";
    SYSTEMTIME st; GetLocalTime(&st); st.wDay += 1;
    wchar_t startBoundary[64];
    swprintf_s(startBoundary, 64, L"%04d-%02d-%02dT%02d:%02d:00", st.wYear, st.wMonth, st.wDay, 3, 0);
    xml += std::wstring(L"      <StartBoundary>") + startBoundary + L"</StartBoundary>\r\n";
    xml += L"      <Enabled>true</Enabled>\r\n";
    xml += L"      <ScheduleByWeek>\r\n";
    xml += L"        <DaysOfWeek><Sunday /></DaysOfWeek>\r\n";
    xml += L"        <WeeksInterval>1</WeeksInterval>\r\n";
    xml += L"      </ScheduleByWeek>\r\n";
    xml += L"    </CalendarTrigger>\r\n";
    xml += L"  </Triggers>\r\n";
    xml += L"  <Principals>\r\n";
    xml += L"    <Principal id=\"Author\">\r\n";
    xml += L"      <LogonType>InteractiveToken</LogonType>\r\n";
    xml += L"      <RunLevel>LeastPrivilege</RunLevel>\r\n";
    xml += L"    </Principal>\r\n";
    xml += L"  </Principals>\r\n";
    xml += L"  <Settings>\r\n";
    xml += L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n";
    xml += L"  </Settings>\r\n";
    xml += L"  <Actions Context=\"Author\">\r\n";
    xml += L"    <Exec>\r\n";
    xml += L"      <Command>" + (folder + L"\\WinProgramUpdaterGUI.exe") + L"</Command>\r\n";
    xml += L"      <Arguments>--hidden</Arguments>\r\n";
    xml += L"    </Exec>\r\n";
    xml += L"  </Actions>\r\n";
    xml += L"</Task>\r\n";

    // Write UTF-16 LE with BOM
    HANDLE hFile = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool schtasksOk = false;
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        AppendFirstRunLogInternal(L"CreateFileW(xml) failed: " + std::to_wstring(err));
    } else {
        DWORD written;
        WORD bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &written, NULL);
        WriteFile(hFile, xml.c_str(), (DWORD)(xml.size() * sizeof(wchar_t)), &written, NULL);
        CloseHandle(hFile);

        // Create task via schtasks through cmd.exe so we can redirect output to a file for diagnostics
        std::wstring outPath = folder + L"\\schtasks_output.txt";
        std::wstring cmd = L"schtasks /Create /TN \"WinProgramUpdaterWeekly\" /XML \"" + xmlPath + L"\" /F";
        AppendFirstRunLogInternal(L"Running via cmd.exe: " + cmd + L" -> output: " + outPath);
        // Use RunCommandCapture to execute schtasks and capture output reliably
        int ec = -1;
        std::wstring out = RunCommandCapture(cmd, &ec);
        AppendFirstRunLogInternal(std::wstring(L"schtasks create output: ") + out);
        if (ec == 0 || out.find(L"SUCCESS") != std::wstring::npos || out.find(L"has successfully been created") != std::wstring::npos) schtasksOk = true;
    }

    if (schtasksOk) {
        // Update INI to record success; write readable UTF-8 file
        if (!WriteSettingsIniUtf8(iniPath, currentLang, L"1")) {
            AppendFirstRunLogInternal(L"WriteSettingsIniUtf8 UpdaterTaskCreated=1 failed");
            AppendFirstRunLogInternal(L"INI path: " + iniPath);
        } else {
            AppendFirstRunLogInternal(L"Task created and INI written (UTF-8)");
            AppendFirstRunLogInternal(L"INI path: " + iniPath);
        }
    } else {
        AppendFirstRunLogInternal(L"Task creation failed - showing non-UI error flow (no INI)");
    }
}

static void Trim(std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b-1])) --b;
    if (a == 0 && b == s.size()) return;
    s = s.substr(a, b-a);
}

static std::wstring RunCommandCapture(const std::wstring& cmd, int* exitCode) {
    // Create a temporary file to capture stdout/stderr, and run the provided
    // command line without showing a console window. This avoids the flashing
    // terminal while keeping nearly the same capture semantics as before.
    wchar_t tmpPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmpPath) == 0) return L"";
    wchar_t tmpFile[MAX_PATH];
    if (GetTempFileNameW(tmpPath, L"wpm", 0, tmpFile) == 0) return L"";

    // Build a shell command that redirects output to the tmp file
    std::wstring captureCmd = cmd + L" > \"" + tmpFile + L"\" 2>&1";

    // Run through cmd.exe /c so shell redirection works reliably
    std::wstring fullCmd = L"cmd.exe /c " + captureCmd;

    // CreateProcess requires a mutable buffer
    std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOW si{}; PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    // CREATE_NO_WINDOW prevents a console from being shown
    BOOL ok = CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!ok) {
        DeleteFileW(tmpFile);
        return L"";
    }
    // Wait up to 15s for the command to complete. This prevents indefinite UI hangs.
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD procExitCode = 0;
    GetExitCodeProcess(pi.hProcess, &procExitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    std::wstring out;
    std::FILE* f = nullptr;
    _wfopen_s(&f, tmpFile, L"rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::vector<char> buf(sz + 1);
            std::fread(buf.data(), 1, sz, f);
            buf[sz] = '\0';
            int needed = MultiByteToWideChar(CP_OEMCP, 0, buf.data(), -1, NULL, 0);
            if (needed > 0) {
                std::vector<wchar_t> wbuf(needed);
                MultiByteToWideChar(CP_OEMCP, 0, buf.data(), -1, wbuf.data(), needed);
                out.assign(wbuf.data());
            }
        }
        fclose(f);
    }
    DeleteFileW(tmpFile);
    if (exitCode) *exitCode = (int)procExitCode;
    return out;
}

static std::wstring TaskName() { return L"WinProgramUpdaterWeekly"; }

TaskInfo QueryUpdaterTaskInfo() {
    TaskInfo info;
    std::wstring cmd = L"schtasks /Query /V /FO LIST /TN \"" + TaskName() + L"\"";
    std::wstring out = RunCommandCapture(cmd);
    info.rawInfo = out;
    if (out.find(L"ERROR: The system cannot find the file specified.") != std::wstring::npos ||
        out.find(L"ERROR: The specified task name ") != std::wstring::npos) {
        info.exists = false;
        return info;
    }
    if (out.empty()) { info.exists = false; return info; }
    info.exists = true;

    // Quick, case-insensitive check for the common "Scheduled Task State: Enabled" line
    std::wstring outLower = out;
    for (auto &ch : outLower) ch = towlower(ch);
    size_t sstatePos = outLower.find(L"scheduled task state:");
    if (sstatePos != std::wstring::npos) {
        size_t epos = outLower.find(L"enabled", sstatePos);
        if (epos != std::wstring::npos) {
            info.enabled = true;
        } else {
            size_t dpos = outLower.find(L"disabled", sstatePos);
            if (dpos != std::wstring::npos) info.enabled = false;
        }
    }

    // More robust, localized-aware parsing: split output into lines and inspect
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < out.size()) {
        size_t e = out.find(L"\r\n", start);
        if (e == std::wstring::npos) { lines.push_back(out.substr(start)); break; }
        lines.push_back(out.substr(start, e - start));
        start = e + 2;
    }

    bool scheduleWeekly = false;
    for (const auto& rawLine : lines) {
        std::wstring line = rawLine;
        Trim(line);
        if (line.empty()) continue;
        // Try to find time patterns anywhere (fallback for Next Run Time in any language)
        for (size_t i = 0; i + 4 < line.size(); ++i) {
            if (iswdigit(line[i]) && iswdigit(line[i+1]) && line[i+2] == L':' && iswdigit(line[i+3]) && iswdigit(line[i+4])) {
                info.nextRun = line.substr(i,5);
                break;
            }
        }
        // Split at first ':' to get a key/value if present
        size_t cpos = line.find(L':');
        if (cpos == std::wstring::npos) continue;
        std::wstring key = line.substr(0, cpos);
        std::wstring val = line.substr(cpos + 1);
        Trim(key); Trim(val);
        // lowercase copies for comparisons
        std::wstring lkey = key; std::wstring lval = val;
        for (auto &ch : lkey) ch = towlower(ch);
        for (auto &ch : lval) ch = towlower(ch);

        // Enabled detection: look for common true tokens in the value
        if (lkey.find(L"enable") != std::wstring::npos || lkey.find(L"aktiv") != std::wstring::npos || lkey.find(L"aktivert") != std::wstring::npos || lkey.find(L"scheduled") != std::wstring::npos || lkey.find(L"task") != std::wstring::npos || lkey.find(L"state") != std::wstring::npos || lkey.find(L"status") != std::wstring::npos) {
            if (lval.find(L"enabled") != std::wstring::npos || lval.find(L"yes") != std::wstring::npos || lval.find(L"true") != std::wstring::npos || lval.find(L"ja") != std::wstring::npos || lval.find(L"aktiv") != std::wstring::npos || lval.find(L"aktivert") != std::wstring::npos || lval.find(L"1") != std::wstring::npos) {
                info.enabled = true;
            } else if (lval.find(L"disabled") != std::wstring::npos || lval.find(L"deaktiv") != std::wstring::npos || lval.find(L"ikke") != std::wstring::npos || lval.find(L"0") != std::wstring::npos) {
                info.enabled = false;
            }
        }

        // Schedule type
        if (lkey.find(L"schedule") != std::wstring::npos) {
            if (lval.find(L"weekly") != std::wstring::npos || lval.find(L"ukentlig") != std::wstring::npos) scheduleWeekly = true;
            if (lval.find(L"daily") != std::wstring::npos || lval.find(L"daglig") != std::wstring::npos) scheduleWeekly = false;
        }

        // Modifier / interval value (English or localized)
        if (lkey.find(L"modifier") != std::wstring::npos || lkey.find(L"modifier:") != std::wstring::npos || lkey.find(L"modifier") != std::wstring::npos) {
            Trim(val);
            try { info.intervalDays = std::stoi(val); } catch(...) { /* ignore */ }
        }
        // Some locales print "weeks" or "weeksinterval" etc.
        if (lkey.find(L"weeks") != std::wstring::npos) {
            Trim(val);
            try { int weeks = std::stoi(val); if (weeks>0) info.intervalDays = weeks * 7; } catch(...) { /* ignore */ }
        }
        // Some locales print "daysinterval" or "days".
        if (lkey.find(L"daysinterval") != std::wstring::npos || lkey.find(L"daysinterval") != std::wstring::npos) {
            Trim(val);
            try { info.intervalDays = std::stoi(val); } catch(...) { /* ignore */ }
        }
        
        // Universal fallback: if value contains pattern with reasonable number (1-365)
        // This works for ANY language: "Every 5 day(s)", "hver 5 dag", "cada 5 día(s)", "每 5 天", etc.
        // Only apply if we haven't found intervalDays yet to avoid overwriting good data
        if (info.intervalDays <= 0) {
            for (size_t i = 0; i < val.size(); ++i) {
                if (iswdigit(val[i])) {
                    size_t end = i;
                    while (end < val.size() && iswdigit(val[end])) ++end;
                    try { 
                        int num = std::stoi(val.substr(i, end - i));
                        // Accept numbers 1-365 as likely day intervals (exclude years like 2026, large times)
                        if (num >= 1 && num <= 365) {
                            // Extra validation: value shouldn't look like a date (contain ., -, /) or time (contain :)
                            bool looksLikeDate = (val.find(L'.') != std::wstring::npos || val.find(L'-') != std::wstring::npos || val.find(L'/') != std::wstring::npos);
                            bool looksLikeTime = (val.find(L':') != std::wstring::npos);
                            if (!looksLikeDate && !looksLikeTime) {
                                info.intervalDays = num;
                                break;
                            }
                        }
                    } catch(...) { /* ignore */ }
                }
            }
        }
    }

    // If schedule was weekly and no explicit interval found, map to 7 days
    if (scheduleWeekly && info.intervalDays <= 0) info.intervalDays = 7;

    // Diagnostic: record parsed info for debugging
    try {
        std::wstring msg = L"Parsed TaskInfo: exists=" + std::wstring(info.exists?L"1":L"0") + L" enabled=" + std::wstring(info.enabled?L"1":L"0") + L" intervalDays=" + (info.intervalDays>0?std::to_wstring(info.intervalDays):std::wstring(L"-1")) + L" nextRun=" + info.nextRun;
        AppendFirstRunLogInternal(msg);
    } catch(...) {}

    return info;
}

bool CreateOrUpdateUpdaterTask(int intervalDays, const std::wstring& startTime, bool runIfUnavailable) {
    // Build an XML task near the exe (so paths are correct) and import it via schtasks
    wchar_t exePath[MAX_PATH]; GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t p = exeDir.find_last_of(L"\\/");
    if (p != std::wstring::npos) exeDir = exeDir.substr(0, p+1);
    std::wstring xmlPath = exeDir + L"\\wpm_updater_task.xml";

    // Build XML (UTF-16)
    std::wstring xml;
    xml += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n";
    xml += L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n";
    xml += L"  <RegistrationInfo><Author>WinProgramSuite</Author></RegistrationInfo>\r\n";
    xml += L"  <Triggers>\r\n";
    xml += L"    <CalendarTrigger>\r\n";
    // StartBoundary - use today's date with provided time (or fallback now)
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t startBoundary[64];
    int hh = 22, mm = 0;
    // parse startTime "HH:MM"
    if (startTime.size() >= 4) {
        swscanf_s(startTime.c_str(), L"%d:%d", &hh, &mm);
    }
    swprintf_s(startBoundary, 64, L"%04d-%02d-%02dT%02d:%02d:00", st.wYear, st.wMonth, st.wDay, hh, mm);
    xml += std::wstring(L"      <StartBoundary>") + startBoundary + L"</StartBoundary>\r\n";
    xml += L"      <Enabled>true</Enabled>\r\n";
    xml += L"      <ScheduleByDay>\r\n";
    xml += L"        <DaysInterval>" + std::to_wstring(intervalDays) + L"</DaysInterval>\r\n";
    xml += L"      </ScheduleByDay>\r\n";
    xml += L"    </CalendarTrigger>\r\n";
    xml += L"  </Triggers>\r\n";
    xml += L"  <Principals>\r\n";
    xml += L"    <Principal id=\"Author\">\r\n";
    xml += L"      <LogonType>InteractiveToken</LogonType>\r\n";
    xml += L"      <RunLevel>LeastPrivilege</RunLevel>\r\n";
    xml += L"    </Principal>\r\n";
    xml += L"  </Principals>\r\n";
    xml += L"  <Settings>\r\n";
    xml += std::wstring(L"    <StartWhenAvailable>") + (runIfUnavailable ? L"true" : L"false") + L"</StartWhenAvailable>\r\n";
    xml += L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n";
    xml += L"  </Settings>\r\n";
    xml += L"  <Actions Context=\"Author\">\r\n";
    xml += L"    <Exec>\r\n";
    xml += L"      <Command>" + (exeDir + L"WinProgramUpdaterGUI.exe") + L"</Command>\r\n";
    xml += L"      <Arguments>--hidden</Arguments>\r\n";
    xml += L"    </Exec>\r\n";
    xml += L"  </Actions>\r\n";
    xml += L"</Task>\r\n";

    HANDLE hFile = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool schtasksOk = false;
    if (hFile == INVALID_HANDLE_VALUE) {
        AppendFirstRunLogInternal(L"CreateFileW(xml) failed when creating updater task xml");
    } else {
        DWORD written;
        WORD bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &written, NULL);
        WriteFile(hFile, xml.c_str(), (DWORD)(xml.size() * sizeof(wchar_t)), &written, NULL);
        CloseHandle(hFile);

        std::wstring outPath = exeDir + L"\\schtasks_create_output.txt";
        std::wstring cmd = L"schtasks /Create /TN \"" + TaskName() + L"\" /XML \"" + xmlPath + L"\" /F";
        AppendFirstRunLogInternal(L"Creating task via: " + cmd);
        int ec = -1;
        std::wstring out = RunCommandCapture(cmd, &ec);
        AppendFirstRunLogInternal(std::wstring(L"schtasks create output: ") + out);
        if (ec == 0 || out.find(L"SUCCESS") != std::wstring::npos) schtasksOk = true;
    }
    return schtasksOk;
}

bool EnableUpdaterTask() {
    std::wstring cmd = L"schtasks /Change /TN \"" + TaskName() + L"\" /ENABLE";
    int ec = -1;
    std::wstring out = RunCommandCapture(cmd, &ec);
    AppendFirstRunLogInternal(std::wstring(L"schtasks enable: ") + out);
    return (ec == 0) || (out.find(L"SUCCESS") != std::wstring::npos);
}

bool DisableUpdaterTask() {
    // Delete the scheduled task so its settings are removed when user disables
    std::wstring cmd = L"schtasks /Delete /TN \"" + TaskName() + L"\" /F";
    int ec = -1;
    std::wstring out = RunCommandCapture(cmd, &ec);
    AppendFirstRunLogInternal(std::wstring(L"schtasks delete: ") + out);
    return (ec == 0) || (out.find(L"SUCCESS") != std::wstring::npos);
}

bool RunUpdaterNow() {
    std::wstring cmd = L"schtasks /Run /TN \"" + TaskName() + L"\"";
    int ec = -1;
    std::wstring out = RunCommandCapture(cmd, &ec);
    AppendFirstRunLogInternal(std::wstring(L"schtasks run: ") + out);
    if (ec == 0 || out.find(L"SUCCESS") != std::wstring::npos) return true;
    wchar_t exePath[MAX_PATH]; GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t p = exeDir.find_last_of(L"\\/");
    if (p != std::wstring::npos) exeDir = exeDir.substr(0, p+1);
    std::wstring upd = exeDir + L"WinProgramUpdaterGUI.exe";
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = upd.c_str();
    sei.lpParameters = L"--hidden";
    sei.nShow = SW_HIDE;
    BOOL ok = ShellExecuteExW(&sei);
    AppendFirstRunLogInternal(std::wstring(L"fallback launch: ") + (ok?L"ok":L"fail"));
    return ok == TRUE;
}
