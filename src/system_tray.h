#ifndef SYSTEM_TRAY_H
#define SYSTEM_TRAY_H

#include <windows.h>
#include <shellapi.h>
#include <string>

// System tray manager class
class SystemTray {
public:
    SystemTray();
    ~SystemTray();
    
    // Initialize tray icon with main window handle
    bool Initialize(HWND hwnd);
    
    // Add icon to system tray
    bool AddToTray();
    
    // Remove icon from system tray
    bool RemoveFromTray();
    
    // Update tooltip text (shows next scan time)
    bool UpdateTooltip(const std::wstring& text);
    
    // Show balloon notification
    bool ShowBalloon(const std::wstring& title, const std::wstring& text);
    
    // Show right-click context menu
    void ShowContextMenu(HWND hwnd);
    
    // Start periodic scan timer
    void StartScanTimer(int intervalHours);
    
    // Stop scan timer
    void StopScanTimer();
    
    // Start tooltip update timer (updates countdown every minute)
    void StartTooltipTimer();
    
    // Stop tooltip timer
    void StopTooltipTimer();
    
    // Update next scan time display
    void UpdateNextScanTime(const std::wstring& statusLine = L"");
    
    // Get next scan time as formatted string
    std::wstring GetNextScanTimeString();
    
    // Trigger immediate scan
    // manual: true for user-initiated scans, false for automatic timer scans
    void TriggerScan(bool manual = true);
    
    // Check if tray icon is active
    bool IsActive() const { return m_active; }
    
    // Handle tray icon messages
    static LRESULT HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);
    
private:
    HWND m_hwnd;
    NOTIFYICONDATAW m_nid;
    bool m_active;
    UINT_PTR m_scanTimerId;
    UINT_PTR m_tooltipTimerId;
    SYSTEMTIME m_nextScanTime;
    int m_pollingIntervalHours;
    
    // Calculate next scan time
    void CalculateNextScanTime();
};

// Global system tray instance
extern SystemTray* g_systemTray;

// Tray message ID
#define WM_TRAYICON (WM_USER + 1)

// Timer IDs
#define TIMER_SCAN 1001
#define TIMER_TOOLTIP 1002

// Menu item IDs
#define IDM_SCAN_NOW 2001
#define IDM_OPEN_WINDOW 2002
#define IDM_EXIT 2003

#endif // SYSTEM_TRAY_H
