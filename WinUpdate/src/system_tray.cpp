#include "system_tray.h"
#include "scan_runner.h"
#include "Config.h"
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <atomic>

// Define notification messages if not already defined (for older SDKs)
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif
#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif

// External references
extern HWND g_hMainWindow;
extern std::atomic<bool> g_refresh_in_progress;
extern std::wstring t(const char *key);

#define WM_REFRESH_ASYNC (WM_APP + 1)

// Global instance
SystemTray* g_systemTray = nullptr;

SystemTray::SystemTray() 
    : m_hwnd(nullptr)
    , m_active(false)
    , m_scanTimerId(0)
    , m_tooltipTimerId(0)
    , m_pollingIntervalHours(2) {
    ZeroMemory(&m_nid, sizeof(m_nid));
    ZeroMemory(&m_nextScanTime, sizeof(m_nextScanTime));
}

SystemTray::~SystemTray() {
    RemoveFromTray();
}

bool SystemTray::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    
    // Load application icon (IDI_APP_ICON = 101)
    m_nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    
    wcscpy_s(m_nid.szTip, L"WinUpdate");
    
    return true;
}

bool SystemTray::AddToTray() {
    if (m_active) return true;
    
    // Debug: Log tray icon setup
    std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
    log << "AddToTray called\n";
    log << "  hwnd=" << m_nid.hWnd << "\n";
    log << "  uID=" << m_nid.uID << "\n";
    log << "  uFlags=0x" << std::hex << m_nid.uFlags << std::dec << "\n";
    log << "  uCallbackMessage=" << m_nid.uCallbackMessage << " (WM_TRAYICON=" << WM_TRAYICON << ")\n";
    log << "  hIcon=" << m_nid.hIcon << "\n";
    
    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        log << "NIM_ADD succeeded\n";
        m_active = true;
        log.close();
        
        return true;
    }
    log << "NIM_ADD failed! Error: " << GetLastError() << "\n";
    log.close();
    return false;
}

bool SystemTray::RemoveFromTray() {
    if (!m_active) return true;
    
    StopScanTimer();
    StopTooltipTimer();
    
    if (Shell_NotifyIconW(NIM_DELETE, &m_nid)) {
        m_active = false;
        return true;
    }
    return false;
}

bool SystemTray::UpdateTooltip(const std::wstring& text) {
    if (!m_active) return false;
    
    wcscpy_s(m_nid.szTip, text.c_str());
    m_nid.uFlags = NIF_TIP;
    
    // Debug logging
    std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
    log << "UpdateTooltip called with text: " << std::string(text.begin(), text.end()) << "\n";
    BOOL result = Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    log << "Shell_NotifyIconW result: " << result << "\n";
    
    // If modify failed, the icon may have been lost (e.g., after Windows update/restart)
    // Try to re-add it
    if (!result) {
        log << "UpdateTooltip failed, attempting to re-add icon\n";
        m_active = false;  // Reset active flag so AddToTray will work
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;  // Restore full flags for add
        
        if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
            log << "Re-add succeeded\n";
            m_active = true;
            result = TRUE;
        } else {
            log << "Re-add failed! Error: " << GetLastError() << "\n";
        }
    }
    
    log.close();
    
    return result != FALSE;
}

bool SystemTray::ShowBalloon(const std::wstring& title, const std::wstring& text) {
    if (!m_active) return false;
    
    m_nid.uFlags = NIF_INFO;
    wcscpy_s(m_nid.szInfoTitle, title.c_str());
    wcscpy_s(m_nid.szInfo, text.c_str());
    m_nid.dwInfoFlags = NIIF_INFO;
    m_nid.uTimeout = 10000; // 10 seconds
    
    bool result = Shell_NotifyIconW(NIM_MODIFY, &m_nid) != FALSE;
    
    // If modify failed, try to re-add the icon
    if (!result) {
        std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
        log << "ShowBalloon failed, attempting to re-add icon\n";
        m_active = false;
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
        
        if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
            log << "Re-add succeeded\n";
            m_active = true;
            result = true;
        } else {
            log << "Re-add failed! Error: " << GetLastError() << "\n";
        }
        log.close();
    }
    
    // Reset flags
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    
    return result;
}

void SystemTray::ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    
    // Add menu items with i18n support
    std::wstring scanText = L"⚡ " + t("tray_menu_scan");
    std::wstring openText = L"🪟 " + t("tray_menu_open");
    std::wstring exitText = L"❌ " + t("tray_menu_exit");
    
    AppendMenuW(hMenu, MF_STRING, IDM_SCAN_NOW, scanText.c_str());
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_WINDOW, openText.c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, exitText.c_str());
    
    // Required for menu to work properly
    SetForegroundWindow(hwnd);
    
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    
    DestroyMenu(hMenu);
    
    // Required for proper menu closure
    PostMessage(hwnd, WM_NULL, 0, 0);
}

void SystemTray::StartScanTimer(int intervalHours) {
    m_pollingIntervalHours = intervalHours;
    
    // Convert hours to milliseconds
    UINT intervalMs = intervalHours * 60 * 60 * 1000;
    
    m_scanTimerId = SetTimer(m_hwnd, TIMER_SCAN, intervalMs, NULL);
    
    CalculateNextScanTime();
    UpdateNextScanTime();
}

void SystemTray::StopScanTimer() {
    if (m_scanTimerId) {
        KillTimer(m_hwnd, TIMER_SCAN);
        m_scanTimerId = 0;
    }
}

void SystemTray::StartTooltipTimer() {
    // Update tooltip every minute
    m_tooltipTimerId = SetTimer(m_hwnd, TIMER_TOOLTIP, 60000, NULL);
}

void SystemTray::StopTooltipTimer() {
    if (m_tooltipTimerId) {
        KillTimer(m_hwnd, TIMER_TOOLTIP);
        m_tooltipTimerId = 0;
    }
}

void SystemTray::CalculateNextScanTime() {
    SYSTEMTIME currentTime;
    GetLocalTime(&currentTime);
    
    // Convert to FILETIME for calculation
    FILETIME ftCurrent, ftNext;
    SystemTimeToFileTime(&currentTime, &ftCurrent);
    
    // Convert to ULARGE_INTEGER for addition
    ULARGE_INTEGER uliNext;
    uliNext.LowPart = ftCurrent.dwLowDateTime;
    uliNext.HighPart = ftCurrent.dwHighDateTime;
    
    // Add interval (in 100-nanosecond intervals)
    // 1 hour = 36000000000 hundred-nanoseconds
    ULONGLONG intervalInHundredNanos = (ULONGLONG)m_pollingIntervalHours * 36000000000ULL;
    uliNext.QuadPart += intervalInHundredNanos;
    
    // Convert back
    ftNext.dwLowDateTime = uliNext.LowPart;
    ftNext.dwHighDateTime = uliNext.HighPart;
    
    FileTimeToSystemTime(&ftNext, &m_nextScanTime);
}

void SystemTray::UpdateNextScanTime(const std::wstring& statusLine) {
    // If a new status line is provided, store it; otherwise use the stored one
    if (!statusLine.empty()) {
        m_lastStatusLine = statusLine;
    }
    
    std::wstring timeStr = GetNextScanTimeString();
    
    // Build tooltip with second line from stored status
    std::wstring tooltip = L"WinUpdate - " + t("tray_next_scan") + L" " + timeStr;
    if (!m_lastStatusLine.empty()) {
        tooltip += L"\n" + m_lastStatusLine;
    }
    
    // Debug logging
    std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
    log << "UpdateNextScanTime: tooltip = " << std::string(tooltip.begin(), tooltip.end()) << "\n";
    log.close();
    
    UpdateTooltip(tooltip);
}

std::wstring SystemTray::GetNextScanTimeString() {
    // Return the actual clock time when next scan will run (not countdown)
    std::wstringstream ss;
    ss << std::setfill(L'0') << std::setw(2) << m_nextScanTime.wHour 
       << L":" 
       << std::setfill(L'0') << std::setw(2) << m_nextScanTime.wMinute;
    return ss.str();
}

void SystemTray::TriggerScan(bool manual) {
    // Reset scan timer
    StopScanTimer();
    StartScanTimer(m_pollingIntervalHours);
    
    // Trigger scan by posting refresh message to main window
    // The main window will handle the scan and check skip configuration
    extern HWND g_hMainWindow;
    if (g_hMainWindow) {
        // WM_REFRESH_ASYNC is defined in main.cpp as (WM_APP + 1)
        // wParam: 1 = manual, 0 = automatic
        PostMessageW(g_hMainWindow, WM_APP + 1, manual ? 1 : 0, 0);
    }
}

LRESULT SystemTray::HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Default version: icon ID in wParam, mouse message in lParam
    UINT iconId = (UINT)wParam;
    UINT uMsg = (UINT)lParam;
    
    // Debug logging - write to file what message we received
    {
        std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
        log << "Tray message: wParam=" << wParam << ", lParam=" << lParam 
            << " (iconId=" << iconId << ", uMsg=" << uMsg << ")\n";
        log.close();
    }
    
    // Check if this is for our icon
    if (iconId != 1) return 0;
    
    switch (uMsg) {
        case NIN_SELECT:          // Left click (version 4)
        case NIN_KEYSELECT:       // Keyboard selection (version 4)
        case WM_LBUTTONUP:        // Left click (fallback for older versions)
            // Show main window
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            // Trigger a refresh scan if one isn't already running
            if (!g_refresh_in_progress.load()) {
                PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);  // 1 = manual refresh
            }
            break;
            
        case WM_CONTEXTMENU:      // Right-click (version 4)
        case WM_RBUTTONUP:        // Right-click (fallback for older versions)
            // Show context menu
            if (g_systemTray) {
                g_systemTray->ShowContextMenu(hwnd);
            }
            break;
            
        case NIN_BALLOONUSERCLICK:  // Balloon notification clicked
            // Show main window
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            // Trigger a refresh scan if one isn't already running
            if (!g_refresh_in_progress.load()) {
                PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);  // 1 = manual refresh
            }
            break;
    }
    
    return 0;
}
