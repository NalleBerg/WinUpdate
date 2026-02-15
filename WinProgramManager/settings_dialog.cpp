#include "settings_dialog.h"
#include "app_details.h" // for Locale definition
#include "task_scheduler.h"
#include "ini_utils.h"
#include "resource.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <commctrl.h>
#include "task_scheduler.h"

extern Locale g_locale;
// Use bold font from main if available
extern HFONT g_hBoldFont;

static const int ID_RUN_BTN = 2001;
static const int ID_SCHED_ENABLE = 2010;
static const int ID_INTERVAL_CB = 2011;
static const int ID_CUSTOM_EDIT = 2012;
static const int ID_FIRSTRUN_EDIT = 2013;
static const int ID_RUN_IF_UNAVAIL = 2014;
static const int ID_USE_BTN = 2020;
static const int ID_OK_BTN = 2021;

// Forward
static void RunUpdaterGUI(HWND parent);
static std::wstring GetIniPath();

static std::wstring GetIniPath() {
    PWSTR roamingPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &roamingPath))) {
        std::wstring dir(roamingPath);
        CoTaskMemFree(roamingPath);
        dir += L"\\WinProgramManager";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\WinProgramManager.ini";
    }
    return L"";
}

// Parameters for background worker
struct TaskWorkerParams {
    HWND parent;
    int days;
    std::wstring startTime;
    bool runIfUnavailable;
    bool enabled;
    bool closeIfOk;
};

static HWND CreateProgressModal(HWND parent, const std::wstring& message) {
    // Ensure progress common control is available
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    RECT pr;
    GetClientRect(parent, &pr);
    int w = 360, h = 80;
    int x = pr.left + (pr.right - pr.left - w) / 2;
    int y = pr.top + (pr.bottom - pr.top - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"STATIC", message.c_str(), WS_POPUP | WS_BORDER | WS_VISIBLE | SS_CENTER, x, y, w, h, parent, NULL, (HINSTANCE)GetModuleHandle(NULL), NULL);
    if (!hwnd) return NULL;
    HWND hPb = CreateWindowExW(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 12, 36, w-24, 20, hwnd, NULL, (HINSTANCE)GetModuleHandle(NULL), NULL);
    SendMessageW(hPb, PBM_SETMARQUEE, TRUE, 0);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

static DWORD WINAPI TaskWorkerThread(LPVOID lp) {
    TaskWorkerParams* p = (TaskWorkerParams*)lp;
    bool ok = false;
    if (p->enabled) {
        ok = CreateOrUpdateUpdaterTask(p->days, p->startTime, p->runIfUnavailable);
        if (ok) EnableUpdaterTask();
    } else {
        // user requested disabling the scheduled updater
        ok = DisableUpdaterTask();
    }
    // Post completion back to settings window
    PostMessageW(p->parent, WM_USER + 1000, ok ? 1 : 0, p->closeIfOk ? 1 : 0);
    delete p;
    return 0;
}

// Window procedure for settings dialog to support custom drawing and transparent static
static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hRun = NULL;
    static HWND hSchedEnable = NULL;
    static HWND hInterval = NULL;
    static HWND hIntervalLabel = NULL;
    static HWND hCustom = NULL;
    static HWND hFirstRun = NULL;
    static HWND hFirstRunLabel = NULL;
    static HWND hRunIf = NULL;
    static HWND hUse = NULL;
    static HWND hOk = NULL;
    static HWND hCancel = NULL;

    switch (msg) {
    // Custom message posted when background task worker completes
    case WM_USER + 1000: {
        BOOL ok = (wParam != 0);
        BOOL closeIfOk = (lParam != 0);
        // Close progress window if present
        HWND hProg = (HWND)GetPropW(hWnd, L"WPM_ProgressWnd");
        if (hProg) {
            DestroyWindow(hProg);
            RemovePropW(hWnd, L"WPM_ProgressWnd");
        }
        if (!ok) {
            // Failure creating/disabling task: do not show modal warnings.
            if (hUse) EnableWindow(hUse, TRUE);
            if (hOk) EnableWindow(hOk, TRUE);
            if (closeIfOk) DestroyWindow(hWnd);
        } else {
            // Success: re-enable buttons and close if requested
            if (hUse) EnableWindow(hUse, TRUE);
            if (hOk) EnableWindow(hOk, TRUE);
            if (closeIfOk) DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_CREATE: {
        HINSTANCE hInst = (HINSTANCE)GetModuleHandle(NULL);
        // Create controls
        hSchedEnable = CreateWindowExW(0, L"BUTTON", g_locale.settings_scheduler_enable.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 12, 400, 20, hWnd, (HMENU)ID_SCHED_ENABLE, hInst, NULL);
        hInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 12, 44, 120, 200, hWnd, (HMENU)ID_INTERVAL_CB, hInst, NULL);
        hCustom = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 140, 44, 60, 22, hWnd, (HMENU)ID_CUSTOM_EDIT, hInst, NULL);
        hIntervalLabel = CreateWindowExW(0, L"STATIC", g_locale.settings_scheduler_interval_days_label.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 72, 220, 16, hWnd, NULL, hInst, NULL);
        // Time picker (hour:minute) - use SysDateTimePick32 with up-down control
        hFirstRun = CreateWindowExW(0, DATETIMEPICK_CLASS, NULL, WS_CHILD | WS_VISIBLE | DTS_UPDOWN, 140, 96, 120, 22, hWnd, (HMENU)ID_FIRSTRUN_EDIT, hInst, NULL);
        // Set display format to hours and minutes
        SendMessageW(hFirstRun, DTM_SETFORMAT, 0, (LPARAM)L"HH:mm");
        hFirstRunLabel = CreateWindowExW(0, L"STATIC", g_locale.settings_scheduler_first_run_label.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 96, 220, 16, hWnd, NULL, hInst, NULL);
        hRunIf = CreateWindowExW(0, L"BUTTON", g_locale.settings_scheduler_run_if_fail_label.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 128, 520, 20, hWnd, (HMENU)ID_RUN_IF_UNAVAIL, hInst, NULL);
        // Owner-draw Run button (prominent)
        hRun = CreateWindowExW(0, L"BUTTON", g_locale.settings_run_updater_btn.c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 360, 44, 180, 48, hWnd, (HMENU)ID_RUN_BTN, hInst, NULL);
        hUse = CreateWindowExW(0, L"BUTTON", g_locale.settings_use_button.c_str(), WS_CHILD | WS_VISIBLE, 300, 200, 80, 28, hWnd, (HMENU)ID_USE_BTN, hInst, NULL);
        hOk = CreateWindowExW(0, L"BUTTON", g_locale.settings_ok_button.c_str(), WS_CHILD | WS_VISIBLE, 392, 200, 80, 28, hWnd, (HMENU)ID_OK_BTN, hInst, NULL);
        hCancel = CreateWindowExW(0, L"BUTTON", g_locale.settings_cancel_button.c_str(), WS_CHILD | WS_VISIBLE, 484, 200, 80, 28, hWnd, (HMENU)IDCANCEL, hInst, NULL);

        // Populate interval combo with prime numbers between 2 and 30, plus Custom...
        const wchar_t* primes[] = { L"2", L"3", L"5", L"7", L"11", L"13", L"17", L"19", L"23", L"29" };
        for (int i = 0; i < (int)(sizeof(primes)/sizeof(primes[0])); ++i) SendMessageW(hInterval, CB_ADDSTRING, 0, (LPARAM)primes[i]);
        SendMessageW(hInterval, CB_ADDSTRING, 0, (LPARAM)g_locale.settings_scheduler_custom_days_label.c_str());

        // Load saved values from INI (if present) and apply to controls
        std::wstring ini = GetIniPath();
        if (!ini.empty()) {
            // Read values using UTF-aware reader so INI can be UTF-8 and still editable
            std::wstring sEnabled = ReadIniValue(ini, L"Settings", L"SchedulerEnabled");
            if (!sEnabled.empty() && sEnabled == L"1") SendMessageW(hSchedEnable, BM_SETCHECK, BST_CHECKED, 0);

            std::wstring sd = ReadIniValue(ini, L"Settings", L"SchedulerIntervalDays");
            if (!sd.empty()) {
                int selIndex = CB_ERR;
                const wchar_t* primes[] = { L"2", L"3", L"5", L"7", L"11", L"13", L"17", L"19", L"23", L"29" };
                for (int i = 0; i < (int)(sizeof(primes)/sizeof(primes[0])); ++i) {
                    if (sd == primes[i]) { selIndex = i; break; }
                }
                if (selIndex == CB_ERR) {
                    selIndex = (int)(sizeof(primes)/sizeof(primes[0]));
                    SetWindowTextW(hCustom, sd.c_str());
                }
                if (selIndex != CB_ERR) SendMessageW(hInterval, CB_SETCURSEL, selIndex, 0);
            }

            std::wstring sFirst = ReadIniValue(ini, L"Settings", L"SchedulerFirstRun");
            if (!sFirst.empty()) SetWindowTextW(hFirstRun, sFirst.c_str());

            std::wstring sRunIf = ReadIniValue(ini, L"Settings", L"RunIfSchedulerUnavailable");
            if (!sRunIf.empty() && sRunIf == L"1") SendMessageW(hRunIf, BM_SETCHECK, BST_CHECKED, 0);
            else SendMessageW(hRunIf, BM_SETCHECK, BST_CHECKED, 0);
        } else {
            // Default values when no INI: interval 7, first run 22:00, run-if checked
            // Index of '7' in primes array is 3
            SendMessageW(hInterval, CB_SETCURSEL, 3, 0); // 7 days
            SetWindowTextW(hFirstRun, L"22:00");
            SendMessageW(hRunIf, BM_SETCHECK, BST_CHECKED, 0);
        }

        // Query Task Scheduler to reflect actual task state and sync INI.
        // This ensures the dialog shows the real scheduler state (not only the INI).
        auto ApplyTaskInfoToControls = [&](const TaskInfo& ti) {
            // Update enable checkbox based on intervalDays (most reliable indicator)
            // Only check the box if the task exists with a valid interval
            bool shouldCheck = (ti.intervalDays > 0);
            SendMessageW(hSchedEnable, BM_SETCHECK, shouldCheck ? BST_CHECKED : BST_UNCHECKED, 0);

            // If task doesn't exist, leave interval/time controls as-is (INI or defaults)
            if (!ti.exists) return;

            // Update interval: try to match primes in combobox; otherwise use Custom
            const wchar_t* primes[] = { L"2", L"3", L"5", L"7", L"11", L"13", L"17", L"19", L"23", L"29" };
            int selIndex = CB_ERR;
            if (ti.intervalDays > 0) {
                std::wstring sd = std::to_wstring(ti.intervalDays);
                for (int i = 0; i < (int)(sizeof(primes)/sizeof(primes[0])); ++i) if (sd == primes[i]) { selIndex = i; break; }
                if (selIndex == CB_ERR) {
                    // custom
                    selIndex = (int)(sizeof(primes)/sizeof(primes[0]));
                    SetWindowTextW(hCustom, sd.c_str());
                }
            }
            if (selIndex != CB_ERR) SendMessageW(hInterval, CB_SETCURSEL, selIndex, 0);

            // Parse nextRun for time (HH:MM) heuristically and set time picker
            auto parseTime = [&](const std::wstring& s)->std::wstring{
                for (size_t i = 0; i + 4 < s.size(); ++i) {
                    if (iswdigit(s[i]) && iswdigit(s[i+1]) && s[i+2]==L':' && iswdigit(s[i+3]) && iswdigit(s[i+4])) {
                        return s.substr(i,5);
                    }
                }
                return L"";
            };
            std::wstring timeStr = parseTime(ti.nextRun);
            if (!timeStr.empty()) {
                int hh = 22, mm = 0;
                swscanf_s(timeStr.c_str(), L"%d:%d", &hh, &mm);
                SYSTEMTIME stt{}; GetLocalTime(&stt);
                stt.wHour = (WORD)hh; stt.wMinute = (WORD)mm; stt.wSecond = 0;
                SendMessageW(hFirstRun, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&stt);
            }
        };

        TaskInfo ti = QueryUpdaterTaskInfo();
        ApplyTaskInfoToControls(ti);
        // No status control (removed)
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        int padding = 12;
        int runW = 180, runH = 48;
        // Place prominent Run button top-right
        MoveWindow(hRun, rc.right - padding - runW, 44, runW, runH, TRUE);
        // Expand scheduler checkbox to fill left space
        MoveWindow(hSchedEnable, 12, 12, rc.right - runW - (padding*3), 20, TRUE);
        // Keep interval controls left
        MoveWindow(hInterval, 12, 44, 120, 24, TRUE);
        MoveWindow(hCustom, 140, 44, 60, 22, TRUE);
        MoveWindow(hIntervalLabel, 12, 72, 220, 16, TRUE);
        MoveWindow(hFirstRunLabel, 12, 96, 220, 16, TRUE);
        MoveWindow(hFirstRun, 140, 96, 120, 22, TRUE);
        MoveWindow(hRunIf, 12, 128, rc.right - 24, 20, TRUE);
        // Anchor footer buttons
        int btnW = 80, gap = 12;
        int btnY = rc.bottom - 44;
        // Center the three footer buttons
        int totalW = btnW * 3 + gap * 2;
        int startX = (rc.right - totalW) / 2;
        MoveWindow(hUse, startX, btnY, btnW, 28, TRUE);
        MoveWindow(hOk, startX + btnW + gap, btnY, btnW, 28, TRUE);
        MoveWindow(hCancel, startX + (btnW + gap) * 2, btnY, btnW, 28, TRUE);
        // No status control to position
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        // Make status background transparent
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlID == ID_RUN_BTN) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF base = RGB(10,57,129);
            COLORREF pressCol = RGB(6,34,80);
            HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : base);
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
            // Draw white bold text centered
            SetTextColor(hdc, RGB(255,255,255));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HGDIOBJ oldf = SelectObject(hdc, hf);
            DrawTextW(hdc, g_locale.settings_run_updater_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldf);
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_RUN_BTN:
            RunUpdaterGUI(hWnd);
            return 0;
        case ID_SCHED_ENABLE:
            if (HIWORD(wParam) == BN_CLICKED) {
                // If user enabled scheduling and controls are empty, populate reasonable defaults
                BOOL checked = (SendMessageW(hSchedEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (checked) {
                    if (SendMessageW(hInterval, CB_GETCURSEL, 0, 0) == CB_ERR) {
                        // default to 7 days (index 3 in primes list)
                        SendMessageW(hInterval, CB_SETCURSEL, 3, 0);
                    }
                    wchar_t tb[8]; GetWindowTextW(hFirstRun, tb, _countof(tb));
                    if (wcslen(tb) == 0) SetWindowTextW(hFirstRun, L"22:00");
                    SendMessageW(hRunIf, BM_SETCHECK, BST_CHECKED, 0);
                }
                // Do not persist enabled state to INI here; Task Scheduler is authoritative.
                // Attempt to create/enable or disable the scheduled task immediately.
                // Failures are ignored (no modal warnings) because most home users
                // manage their own account and we avoid intrusive dialogs.
                // Read current scheduler settings from controls to compute task details.
                int sel = (int)SendMessageW(hInterval, CB_GETCURSEL, 0, 0);
                int days = 1;
                wchar_t buf[64] = {0};
                if (sel == CB_ERR) days = 1;
                else {
                    SendMessageW(hInterval, CB_GETLBTEXT, sel, (LPARAM)buf);
                    std::wstring s = buf;
                    if (s == g_locale.settings_scheduler_custom_days_label) {
                        wchar_t cbuf[32] = {0};
                        GetWindowTextW(hCustom, cbuf, 31);
                        days = _wtoi(cbuf);
                        if (days <= 0) days = 1;
                    } else {
                        days = _wtoi(s.c_str()); if (days <= 0) days = 1;
                    }
                }
                // Read time
                SYSTEMTIME st{};
                wchar_t startTimeBuf[16] = L"22:00";
                if (SendMessageW(hFirstRun, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID) {
                    swprintf_s(startTimeBuf, L"%02d:%02d", st.wHour, st.wMinute);
                }
                BOOL runIfUnavailable = (SendMessageW(hRunIf, BM_GETCHECK, 0, 0) == BST_CHECKED);

                if (checked) {
                    CreateOrUpdateUpdaterTask(days, startTimeBuf, (runIfUnavailable != 0));
                    EnableUpdaterTask();
                } else {
                    DisableUpdaterTask();
                }
            }
            break;
        case ID_USE_BTN:
        case ID_OK_BTN: {
            // Read controls and apply settings
            BOOL enabled = (SendMessageW(hSchedEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
            int sel = (int)SendMessageW(hInterval, CB_GETCURSEL, 0, 0);
            int days = 1;
            wchar_t buf[64] = {0};
            if (sel == CB_ERR) days = 1;
            else {
                SendMessageW(hInterval, CB_GETLBTEXT, sel, (LPARAM)buf);
                std::wstring s = buf;
                if (s == g_locale.settings_scheduler_custom_days_label) {
                    wchar_t cbuf[32] = {0};
                    GetWindowTextW(hCustom, cbuf, 31);
                    std::wstring customStr = cbuf;
                    // Trim whitespace
                    size_t start = customStr.find_first_not_of(L" \t\r\n");
                    size_t end = customStr.find_last_not_of(L" \t\r\n");
                    if (start != std::wstring::npos && end != std::wstring::npos) {
                        customStr = customStr.substr(start, end - start + 1);
                    }
                    // Check if it's a valid integer (only digits)
                    bool isValidInt = !customStr.empty();
                    for (wchar_t c : customStr) {
                        if (!iswdigit(c)) {
                            isValidInt = false;
                            break;
                        }
                    }
                    if (!isValidInt) {
                        MessageBoxW(hWnd, g_locale.settings_days_invalid_integer.c_str(), g_locale.settings_title.c_str(), MB_OK | MB_ICONWARNING);
                        SetFocus(hCustom);
                        return 0;
                    }
                    days = _wtoi(cbuf);
                    if (days <= 0 || days > 365) {
                        MessageBoxW(hWnd, g_locale.settings_days_invalid_integer.c_str(), g_locale.settings_title.c_str(), MB_OK | MB_ICONWARNING);
                        SetFocus(hCustom);
                        return 0;
                    }
                } else {
                    days = _wtoi(s.c_str()); if (days <= 0) days = 1;
                }
            }
            // Read time from time picker control
            SYSTEMTIME stTime{};
            if (SendMessageW(hFirstRun, DTM_GETSYSTEMTIME, 0, (LPARAM)&stTime) == GDT_VALID) {
                wchar_t timeBuf[16]; swprintf_s(timeBuf, L"%02d:%02d", stTime.wHour, stTime.wMinute);
                std::wstring startTime = timeBuf;
                // continue below with startTime in scope via lambda capture
                // Validate custom days
                if (sel == CB_ERR) {
                    // shouldn't happen; fallback
                }
                // Validate custom days value if custom selected
                if (sel != CB_ERR) {
                    // nothing here
                }
            } else {
                // Invalid time selected — fall back to default 22:00 rather than show a modal dialog
                SetWindowTextW(hFirstRun, L"22:00");
                // continue using fallback time
            }
            BOOL runIfUnavailable = (SendMessageW(hRunIf, BM_GETCHECK, 0, 0) == BST_CHECKED);

            // Validate custom days: must be >=2 and reasonable
            const int CUSTOM_MIN = 1;
            const int CUSTOM_MAX = 365;
            // If custom option selected, ensure value within range
            int customIndex = (int)SendMessageW(hInterval, CB_GETCOUNT, 0, 0) - 1; // last is Custom
            if ((int)SendMessageW(hInterval, CB_GETCURSEL, 0, 0) == customIndex) {
                if (days < CUSTOM_MIN) days = CUSTOM_MIN;
                if (days > CUSTOM_MAX) days = CUSTOM_MAX;
                // write clamped value back to the edit control
                SetWindowTextW(hCustom, std::to_wstring(days).c_str());
            }

            // Do not persist scheduler settings to INI; Task Scheduler is the source-of-truth.

            // Create worker params
            // Recompute startTime from control
            SYSTEMTIME stTime2{};
            wchar_t startTimeBuf[16] = L"22:00";
            if (SendMessageW(hFirstRun, DTM_GETSYSTEMTIME, 0, (LPARAM)&stTime2) == GDT_VALID) {
                swprintf_s(startTimeBuf, L"%02d:%02d", stTime2.wHour, stTime2.wMinute);
            }

            TaskWorkerParams* p = new TaskWorkerParams();
            p->parent = hWnd;
            p->days = days;
            p->startTime = startTimeBuf;
            p->runIfUnavailable = (runIfUnavailable != 0);
            p->enabled = (enabled != 0);
            p->closeIfOk = (LOWORD(wParam) == ID_OK_BTN);

            // Disable action buttons while running
            EnableWindow(hUse, FALSE);
            EnableWindow(hOk, FALSE);

            // Show modal progress spinner
            HWND hProg = CreateProgressModal(hWnd, g_locale.settings_working_message.empty() ? L"Working..." : g_locale.settings_working_message);
            if (hProg) SetPropW(hWnd, L"WPM_ProgressWnd", hProg);

            // Start worker thread
            HANDLE th = CreateThread(NULL, 0, TaskWorkerThread, p, 0, NULL);
            if (th) CloseHandle(th);
            return 0;
        }
        case IDCANCEL: {
            DestroyWindow(hWnd);
            return 0;
        }
        }
        break;
    }
    // No custom client-area painting for icons; icon shown in title bar
    case WM_CLOSE: {
        DestroyWindow(hWnd);
        return 0;
    }
    case WM_DESTROY: {
        // Bring main application window to front so it doesn't stay behind other apps
        HWND hMain = FindWindowW(L"WinProgramManagerClass", NULL);
        if (hMain) {
            SetForegroundWindow(hMain);
            BringWindowToTop(hMain);
            SetFocus(hMain);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// QueryUpdaterTaskStatus was removed — task presence is no longer queried here.

static void RunUpdaterGUI(HWND parent)
{
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return;
    // Replace filename with WinProgramUpdaterGUI.exe (no absolute install path)
    PathRemoveFileSpecW(exePath);
    std::wstring updater = std::wstring(exePath) + L"\\WinProgramUpdaterGUI.exe";
    // Launch GUI mode (no --hidden) so it opens visible; do not close manager
    HINSTANCE h = ShellExecuteW(parent, L"open", updater.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) {
        MessageBoxW(parent, L"Failed to launch updater.", g_locale.error_title.c_str(), MB_ICONERROR | MB_OK);
    }
}

// Simple modal window for settings
void ShowSettingsDialog(HWND parent)
{
    HINSTANCE hInst = (HINSTANCE)GetModuleHandle(NULL);
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WPMSettingsTempClass";
    // Set window class icon so it appears in the title bar
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, g_locale.settings_title.c_str(), WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 260, parent, NULL, hInst, NULL);
    if (!hWnd) return;

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Modal loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!IsWindow(hWnd)) break; // exit when window closed
    }
}
