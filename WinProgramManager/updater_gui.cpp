// WinProgramUpdater GUI
// Single executable that replaces both WinProgramUpdater.exe and WinProgramUpdaterConsole.exe
// Usage: updater_gui.exe          - Shows verbose GUI window
//        updater_gui.exe --hidden - Runs silently in background (logs to file)

#include "WinProgramUpdater.h"
#include "bouncing_ball.h"
#include "keyboard_shortcuts.h"
#include "resource.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <richedit.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <fstream>

// Window class name
const wchar_t CLASS_NAME[] = L"WinProgramUpdaterGUIClass";

// Control IDs
#define IDC_LOG_TEXT 1001
#define IDC_BTN_START 1002
#define IDC_BTN_CANCEL 1003
#define IDC_BTN_DONE 1004
#define IDC_ANIM 1005
#define IDC_STATS_TEXT 1006
#define IDC_BTN_VIEW_LOG 1007
#define IDC_LOG_VIEWER_RICHEDIT 2001
#define IDC_LOG_VIEWER_BTN_CLOSE 2002
#define IDC_CONTEXT_COPY 2003
#define IDC_CONTEXT_SELECT_ALL 2004

// Custom messages
#define WM_UPDATE_LOG (WM_APP + 1)
#define WM_UPDATE_STATS (WM_APP + 2)
#define WM_UPDATE_COMPLETE (WM_APP + 3)

// Timer IDs
#define TIMER_ANIM 0xBEEF

// Global state
static HWND g_hWnd = NULL;
static HWND g_hLogText = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hBtnCancel = NULL;
static HWND g_hBtnDone = NULL;
static HWND g_hBtnViewLog = NULL;
static HWND g_hStatsText = NULL;
static BouncingBall* g_pBall = nullptr;

static std::atomic<bool> g_updateRunning = false;
static std::atomic<bool> g_cancelRequested = false;
static std::thread* g_workerThread = nullptr;
static std::mutex g_logMutex;
static std::string g_logBuffer;
static std::wstring g_dbPath;
static std::wstring g_dbBackupPath;

// Statistics
static std::atomic<int> g_packagesFound = 0;
static std::atomic<int> g_packagesAdded = 0;
static std::atomic<int> g_packagesDeleted = 0;

// i18n - Simple translation map (will be loaded from locale files)
static std::map<std::string, std::wstring> g_translations;

static std::wstring t(const std::string& key) {
    auto it = g_translations.find(key);
    if (it != g_translations.end()) return it->second;
    // Fallback to English
    static std::map<std::string, std::wstring> fallback = {
        {"updater_title", L"WinProgram Database Updater"},
        {"updater_explanation_line1", L"This updater will:"},
        {"updater_explanation_line2", L"  • Query installed packages from winget"},
        {"updater_explanation_line3", L"  • Compare with local database"},
        {"updater_explanation_line4", L"  • Add new packages to the database"},
        {"updater_explanation_line5", L"  • Remove obsolete packages"},
        {"updater_explanation_line6", L""},
        {"updater_explanation_line7", L"Click 'Start Update' to begin..."},
        {"updater_btn_start", L"Start Update"},
        {"updater_btn_cancel", L"Cancel"},
        {"updater_btn_done", L"Done"},
        {"updater_btn_view_log", L"View Log"},
        {"updater_btn_close", L"Close"},
        {"updater_log_viewer_title", L"Updater Log Viewer"},
        {"updater_log_not_found", L"Log file not found or empty."},
        {"updater_stats_format", L"Found: %d  |  Added: %d  |  Deleted: %d"},
        {"updater_cancelled", L"\n\n=== Update Cancelled ===\nDatabase restored to previous state.\n"},
        {"updater_complete", L"\n\n=== Update Complete ===\n"},
        {"updater_backup_failed", L"ERROR: Failed to create database backup!\n"},
        {"updater_restore_failed", L"ERROR: Failed to restore database backup!\n"}
    };
    auto it2 = fallback.find(key);
    if (it2 != fallback.end()) return it2->second;
    return std::wstring(key.begin(), key.end());
}

// Logging callback for WinProgramUpdater
static void LogCallback(const std::string& message, void* userData) {
    (void)userData;
    if (g_hWnd && !message.empty()) {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logBuffer += message;
        if (message.back() != '\n') {
            g_logBuffer += "\n";
        }
        PostMessage(g_hWnd, WM_UPDATE_LOG, 0, 0);
    }
}

// Helper to convert wstring to UTF-8 string
static std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}

// Statistics callback
static void StatsCallback(int found, int added, int deleted, void* userData) {
    (void)userData;
    g_packagesFound = found;
    g_packagesAdded = added;
    g_packagesDeleted = deleted;
    if (g_hWnd) {
        PostMessage(g_hWnd, WM_UPDATE_STATS, 0, 0);
    }
}

// Create database backup
static bool BackupDatabase() {
    try {
        if (std::filesystem::exists(g_dbPath)) {
            g_dbBackupPath = g_dbPath + L".backup";
            std::filesystem::copy_file(g_dbPath, g_dbBackupPath, std::filesystem::copy_options::overwrite_existing);
            return true;
        }
    } catch (...) {
        LogCallback("WARNING: Failed to create database backup - continuing anyway\n", nullptr);
    }
    return true;  // Return true anyway - backup failure shouldn't stop the update
}

// Restore database from backup (on cancel)
static bool RestoreDatabase() {
    try {
        if (std::filesystem::exists(g_dbBackupPath)) {
            // Copy backup over current database (handles locked files)
            std::filesystem::copy(g_dbBackupPath, g_dbPath, 
                std::filesystem::copy_options::overwrite_existing);
            return true;
        }
    } catch (const std::exception& e) {
        // Log to file only via WinProgramUpdater's Log function
        // Don't show error in UI window
    }
    return false;
}

// Delete backup file (on successful completion)
static void DeleteBackup() {
    try {
        if (std::filesystem::exists(g_dbBackupPath)) {
            std::filesystem::remove(g_dbBackupPath);
        }
    } catch (...) {
    }
}

// Worker thread function
static void UpdateWorkerThread() {
    // Create backup first
    if (!BackupDatabase()) {
        LogCallback(WStringToUtf8(t("updater_backup_failed")), nullptr);
        PostMessage(g_hWnd, WM_UPDATE_COMPLETE, 0, 0);
        return;
    }
    
    WinProgramUpdater updater(g_dbPath);
    updater.SetLogCallback(LogCallback, nullptr);
    updater.SetStatsCallback(StatsCallback, nullptr);
    updater.SetCancelFlag(&g_cancelRequested);
    
    UpdateStats stats;
    bool success = updater.UpdateDatabase(stats);
    
    if (g_cancelRequested) {
        // Restore database
        LogCallback(WStringToUtf8(t("updater_cancelled")), nullptr);
        if (!RestoreDatabase()) {
            LogCallback(WStringToUtf8(t("updater_restore_failed")), nullptr);
        }
    } else if (success) {
        LogCallback(WStringToUtf8(t("updater_complete")), nullptr);
        DeleteBackup();  // Success, remove backup
    }
    
    PostMessage(g_hWnd, WM_UPDATE_COMPLETE, success ? 1 : 0, 0);
}

// Helper to get APPDATA log path
static std::wstring GetLogFilePath() {
    wchar_t appDataPath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) == S_OK) {
        std::wstring logPath = appDataPath;
        logPath += L"\\WinProgramManager\\log\\WinProgramUpdater.log";
        return logPath;
    }
    return L"";
}

// Log Viewer Window Procedure
static LRESULT CALLBACK LogViewerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Store RichEdit handle in window user data
    static HWND s_hRichEdit = NULL;
    
    switch (uMsg) {
        case WM_CREATE: {
            return 0;
        }
        
        case WM_INITDIALOG: {
            HWND hwndDlg = hwnd;
            // Load and initialize RichEdit library
            LoadLibraryW(L"Msftedit.dll");
            
            // Create RichEdit control
            HWND hRichEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 10, 760, 520, hwndDlg, (HMENU)IDC_LOG_VIEWER_RICHEDIT, NULL, NULL);
            
            // Store RichEdit handle for keyboard shortcuts
            s_hRichEdit = hRichEdit;
            
            if (hRichEdit) {
                // Read log file
                std::wstring logPath = GetLogFilePath();
                std::ifstream logFile;
                
                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, NULL, 0, NULL, NULL);
                if (sizeNeeded > 0) {
                    std::string logPathUtf8(sizeNeeded, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, &logPathUtf8[0], sizeNeeded, NULL, NULL);
                    logFile.open(logPathUtf8, std::ios::in);
                }
                
                if (logFile.is_open()) {
                    // Build RTF content with colors
                    std::string rtfContent = "{\\rtf1\\ansi\\ansicpg1252\\deff0"
                        "{\\fonttbl{\\f0\\fmodern\\fcharset0 Consolas;}}"
                        "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green0\\blue0;\\red0\\green128\\blue0;\\red0\\green51\\blue153;}"
                        "\\f0\\fs20 ";
                    
                    std::string line;
                    while (std::getline(logFile, line)) {
                        // Determine color based on content
                        int colorIndex = 1; // Default black
                        bool isBold = false;
                        
                        if (line.find("ERROR") != std::string::npos || line.find("Failed") != std::string::npos) {
                            colorIndex = 2; // Red
                            isBold = true;
                        } else if (line.find("[202") != std::string::npos && line.find("]") != std::string::npos) {
                            // Timestamp line - bold black
                            colorIndex = 1;
                            isBold = true;
                        } else if (line.find("Time update took:") != std::string::npos) {
                            // Time update took line - blue
                            colorIndex = 4;
                        } else if (line.find("===") != std::string::npos || line.find("Step") != std::string::npos) {
                            colorIndex = 4; // Blue
                            isBold = true;
                        } else if (line.find("✓") != std::string::npos || line.find("Added") != std::string::npos || 
                                   line.find("Complete") != std::string::npos) {
                            colorIndex = 3; // Green
                        }
                        
                        if (isBold) rtfContent += "\\b ";
                        rtfContent += "\\cf" + std::to_string(colorIndex) + " ";
                        
                        // Escape RTF special characters
                        for (char ch : line) {
                            if (ch == '\\') rtfContent += "\\\\";
                            else if (ch == '{') rtfContent += "\\{";
                            else if (ch == '}') rtfContent += "\\}";
                            else rtfContent += ch;
                        }
                        
                        if (isBold) rtfContent += "\\b0 ";
                        rtfContent += "\\par\n";
                    }
                    rtfContent += "}";
                    logFile.close();
                    
                    // Set RTF content using EM_STREAMIN
                    EDITSTREAM es = {};
                    es.dwCookie = (DWORD_PTR)&rtfContent;
                    es.pfnCallback = [](DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) -> DWORD {
                        std::string* pContent = (std::string*)dwCookie;
                        *pcb = (LONG)std::min((size_t)cb, pContent->size());
                        if (*pcb > 0) {
                            memcpy(pbBuff, pContent->data(), *pcb);
                            pContent->erase(0, *pcb);
                        }
                        return 0;
                    };
                    SendMessageA(hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
                } else {
                    // No log file
                    SetWindowTextW(hRichEdit, t("updater_log_not_found").c_str());
                }
            }
            
            // Create Close button
            HWND hBtnClose = CreateWindowExW(0, L"Button", t("updater_btn_close").c_str(),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                690, 540, 80, 30, hwndDlg, (HMENU)IDC_LOG_VIEWER_BTN_CLOSE, NULL, NULL);
            
            HFONT hBtnFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
            SendMessage(hBtnClose, WM_SETFONT, (WPARAM)hBtnFont, TRUE);
            
            return 0;
        }
        
        case WM_COMMAND: {
            int cmdId = LOWORD(wParam);
            if (cmdId == IDC_LOG_VIEWER_BTN_CLOSE || cmdId == IDCANCEL || cmdId == IDM_CLOSE_WINDOW) {
                DestroyWindow(hwnd);
                return 0;
            }
            else if (cmdId == IDM_SELECT_ALL || cmdId == IDC_CONTEXT_SELECT_ALL) {
                // Select all text in RichEdit control
                if (s_hRichEdit) {
                    SendMessage(s_hRichEdit, EM_SETSEL, 0, -1);
                    SetFocus(s_hRichEdit);
                }
                return 0;
            }
            else if (cmdId == IDM_COPY || cmdId == IDC_CONTEXT_COPY) {
                // Copy selected text using RichEdit EM_EXGETSEL and manual clipboard
                if (s_hRichEdit) {
                    CHARRANGE cr;
                    SendMessageW(s_hRichEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
                    
                    if (cr.cpMin != cr.cpMax) {
                        // Get selected text using EM_GETSELTEXT
                        int len = cr.cpMax - cr.cpMin;
                        std::wstring selected(len + 1, 0);
                        SendMessageW(s_hRichEdit, EM_GETSELTEXT, 0, (LPARAM)&selected[0]);
                        selected.resize(wcslen(selected.c_str()));
                        
                        // Copy to clipboard
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (selected.length() + 1) * sizeof(wchar_t));
                            if (hMem) {
                                wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                                if (pMem) {
                                    wcscpy_s(pMem, selected.length() + 1, selected.c_str());
                                    GlobalUnlock(hMem);
                                    SetClipboardData(CF_UNICODETEXT, hMem);
                                }
                            }
                            CloseClipboard();
                        }
                    }
                }
                return 0;
            }
            break;
        }
        
        case WM_CONTEXTMENU: {
            HWND hTarget = (HWND)wParam;
            // Show context menu for RichEdit control
            if (hTarget == s_hRichEdit) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDC_CONTEXT_COPY, L"Copy\tCtrl+C");
                AppendMenuW(hMenu, MF_STRING, IDC_CONTEXT_SELECT_ALL, L"Select All\tCtrl+A");
                
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                
                // If invoked via keyboard (lParam == -1), use cursor position
                if (lParam == -1) {
                    GetCursorPos(&pt);
                }
                
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Blue button owner-draw helper
static void DrawButton(LPDRAWITEMSTRUCT dis, const wchar_t* text, bool enabled) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    
    // Colors
    COLORREF bgColor = enabled ? RGB(0, 120, 215) : RGB(160, 160, 160);
    COLORREF textColor = RGB(255, 255, 255);
    
    // Draw background
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);
    
    // Draw text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    // Draw focus rectangle if needed
    if (dis->itemState & ODS_FOCUS) {
        DrawFocusRect(hdc, &rc);
    }
}

// Window procedure
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Load and set window icon
            HICON hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APP_ICON));
            if (hIcon) {
                SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
            
            // Create bouncing ball animation at top (hidden initially)
            g_pBall = new BouncingBall(hwnd, 10, 10, 680, 40);
            g_pBall->Hide();  // Don't show until update starts
            
            // Create multi-line edit control for log (with scrollbar)
            g_hLogText = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", 
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                10, 60, 680, 290, hwnd, (HMENU)IDC_LOG_TEXT, NULL, NULL);
            
            // Set font
            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FF_DONTCARE, L"Segoe UI");
            SendMessage(g_hLogText, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Show initial explanation
            std::wstring explanation = t("updater_explanation_line1") + L"\r\n" +
                                      t("updater_explanation_line2") + L"\r\n" +
                                      t("updater_explanation_line3") + L"\r\n" +
                                      t("updater_explanation_line4") + L"\r\n" +
                                      t("updater_explanation_line5") + L"\r\n" +
                                      t("updater_explanation_line6") + L"\r\n" +
                                      t("updater_explanation_line7");
            SetWindowTextW(g_hLogText, explanation.c_str());
            
            // Create buttons in a row: View Log | Start Update | Close/Cancel
            g_hBtnViewLog = CreateWindowExW(0, L"Button", t("updater_btn_view_log").c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                50, 360, 120, 30, hwnd, (HMENU)IDC_BTN_VIEW_LOG, NULL, NULL);
            
            g_hBtnStart = CreateWindowExW(0, L"Button", t("updater_btn_start").c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                290, 360, 140, 30, hwnd, (HMENU)IDC_BTN_START, NULL, NULL);
            
            // Close button (shows "Close" initially, changes to "Cancel" during update)
            g_hBtnCancel = CreateWindowExW(0, L"Button", t("updater_btn_close").c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                560, 360, 90, 30, hwnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
            
            // Done button hidden (not used anymore)
            g_hBtnDone = CreateWindowExW(0, L"Button", t("updater_btn_done").c_str(),
                WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,  // Hidden
                630, 360, 70, 30, hwnd, (HMENU)IDC_BTN_DONE, NULL, NULL);
            
            // Create statistics text below buttons, centered with transparent background
            g_hStatsText = CreateWindowExW(0, L"Static", L"", 
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                200, 400, 300, 25, hwnd, (HMENU)IDC_STATS_TEXT, NULL, NULL);
            SendMessage(g_hStatsText, WM_SETFONT, (WPARAM)hFont, TRUE);
            // Make background transparent
            SetWindowLongW(g_hStatsText, GWL_EXSTYLE, GetWindowLongW(g_hStatsText, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
            
            return 0;
        }
        
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            
            // Handle keyboard shortcuts and context menu
            if (id == IDM_CLOSE_WINDOW) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            else if (id == IDM_SELECT_ALL || id == IDC_CONTEXT_SELECT_ALL) {
                // Select all text in log control
                if (g_hLogText) {
                    SendMessage(g_hLogText, EM_SETSEL, 0, -1);
                    SetFocus(g_hLogText);
                }
                return 0;
            }
            else if (id == IDM_COPY || id == IDC_CONTEXT_COPY) {
                // Copy selected text from log control using manual clipboard approach
                if (g_hLogText) {
                    // Get selection range
                    DWORD start, end;
                    SendMessageW(g_hLogText, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                    
                    if (start != end) {
                        // Get text length
                        int len = GetWindowTextLengthW(g_hLogText);
                        if (len > 0) {
                            std::wstring text(len + 1, 0);
                            GetWindowTextW(g_hLogText, &text[0], len + 1);
                            
                            // Extract selected portion
                            std::wstring selected = text.substr(start, end - start);
                            
                            // Copy to clipboard
                            if (OpenClipboard(hwnd)) {
                                EmptyClipboard();
                                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (selected.length() + 1) * sizeof(wchar_t));
                                if (hMem) {
                                    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                                    if (pMem) {
                                        wcscpy_s(pMem, selected.length() + 1, selected.c_str());
                                        GlobalUnlock(hMem);
                                        SetClipboardData(CF_UNICODETEXT, hMem);
                                    }
                                }
                                CloseClipboard();
                            }
                        }
                    }
                }
                return 0;
            }
            
            if (id == IDC_BTN_START && !g_updateRunning) {
                // Start update
                g_updateRunning = true;
                g_cancelRequested = false;
                g_packagesFound = 0;
                g_packagesAdded = 0;
                g_packagesDeleted = 0;
                
                // Clear log and show we're starting
                SetWindowTextW(g_hLogText, L"");
                
                // Start ball animation (1px per 1ms)
                if (g_pBall) {
                    g_pBall->Show();
                    g_pBall->Start(1);  // 1ms timer = 1px per millisecond
                }
                
                // Change Close button to Cancel
                SetWindowTextW(g_hBtnCancel, t("updater_btn_cancel").c_str());
                InvalidateRect(g_hBtnCancel, NULL, TRUE);
                
                // Disable Start button during update
                EnableWindow(g_hBtnStart, FALSE);
                
                // Start worker thread
                if (g_workerThread) {
                    if (g_workerThread->joinable()) g_workerThread->join();
                    delete g_workerThread;
                }
                g_workerThread = new std::thread(UpdateWorkerThread);
            }
            else if (id == IDC_BTN_CANCEL) {
                if (g_updateRunning) {
                    // Instant cancel: terminate thread and restore backup
                    LogCallback("Cancelling update...\n", nullptr);
                    
                    // Forcefully terminate the worker thread
                    if (g_workerThread) {
                        // Terminate thread immediately (we have backup)
                        TerminateThread(g_workerThread->native_handle(), 0);
                        g_workerThread->detach();
                        delete g_workerThread;
                        g_workerThread = nullptr;
                    }
                    
                    // Restore database from backup immediately
                    LogCallback("Restoring database from backup...\n", nullptr);
                    if (RestoreDatabase()) {
                        LogCallback("Database restored successfully.\n", nullptr);
                    }
                    // If restore fails, it's already logged to file
                    
                    // Stop and hide ball animation
                    if (g_pBall) {
                        g_pBall->Stop();
                        g_pBall->Hide();
                    }
                    
                    // Reset UI: enable Start button, change Cancel back to Close
                    g_updateRunning = false;
                    EnableWindow(g_hBtnStart, TRUE);
                    SetWindowTextW(g_hBtnCancel, t("updater_btn_close").c_str());
                } else {
                    // Not running, just close
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
            }
            else if (id == IDC_BTN_VIEW_LOG) {
                // Create modal dialog using a simple window-based approach
                WNDCLASSEXW wc = {};
                wc.cbSize = sizeof(WNDCLASSEXW);
                wc.lpfnWndProc = LogViewerWindowProc;
                wc.hInstance = GetModuleHandle(NULL);
                wc.lpszClassName = L"LogViewerDialog";
                wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
                
                // Unregister if already registered
                UnregisterClassW(L"LogViewerDialog", GetModuleHandle(NULL));
                
                if (!RegisterClassExW(&wc)) {
                    MessageBoxW(hwnd, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
                    return 0;
                }
                
                HWND hLogDlg = CreateWindowExW(
                    WS_EX_DLGMODALFRAME,
                    L"LogViewerDialog",
                    t("updater_log_viewer_title").c_str(),
                    WS_POPUP | WS_CAPTION | WS_SYSMENU,
                    CW_USEDEFAULT, CW_USEDEFAULT, 800, 620,
                    hwnd, NULL, GetModuleHandle(NULL), NULL);
                
                if (hLogDlg) {
                    // Send WM_INITDIALOG to initialize controls
                    SendMessage(hLogDlg, WM_INITDIALOG, 0, 0);
                    
                    // Center window on parent
                    RECT rcParent, rcDlg;
                    GetWindowRect(hwnd, &rcParent);
                    GetWindowRect(hLogDlg, &rcDlg);
                    int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
                    int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
                    SetWindowPos(hLogDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                    
                    // Show dialog
                    ShowWindow(hLogDlg, SW_SHOW);
                    UpdateWindow(hLogDlg);
                    
                    // Create accelerator table for log viewer
                    HACCEL hLogAccel = CreateKeyboardAccelerators();
                    
                    // Modal message loop
                    EnableWindow(hwnd, FALSE);
                    MSG msg;
                    while (GetMessage(&msg, NULL, 0, 0)) {
                        if (!IsWindow(hLogDlg)) break;
                        if (!ProcessAccelerator(hLogAccel, &msg)) {
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                        }
                    }
                    EnableWindow(hwnd, TRUE);
                    SetForegroundWindow(hwnd);
                    
                    if (hLogAccel) {
                        DestroyAcceleratorTable(hLogAccel);
                    }
                }
            }
            return 0;
        }
        
        case WM_UPDATE_LOG: {
            // Append log messages to text control
            std::string logText;
            {
                std::lock_guard<std::mutex> lock(g_logMutex);
                logText = g_logBuffer;
                g_logBuffer.clear();
            }
            
            if (!logText.empty()) {
                // Convert UTF-8 to wide string
                int needed = MultiByteToWideChar(CP_UTF8, 0, logText.c_str(), -1, NULL, 0);
                if (needed > 0) {
                    std::wstring wideLog(needed, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, logText.c_str(), -1, &wideLog[0], needed);
                    
                    // Append to existing text
                    int len = GetWindowTextLengthW(g_hLogText);
                    SendMessageW(g_hLogText, EM_SETSEL, len, len);
                    SendMessageW(g_hLogText, EM_REPLACESEL, FALSE, (LPARAM)wideLog.c_str());
                    
                    // Scroll to bottom
                    SendMessageW(g_hLogText, EM_SCROLLCARET, 0, 0);
                }
            }
            return 0;
        }
        
        case WM_UPDATE_STATS: {
            // Update statistics text
            wchar_t statsText[256];
            swprintf(statsText, 256, t("updater_stats_format").c_str(),
                     g_packagesFound.load(), g_packagesAdded.load(), g_packagesDeleted.load());
            SetWindowTextW(g_hStatsText, statsText);
            return 0;
        }
        
        case WM_UPDATE_COMPLETE: {
            // Update finished
            g_updateRunning = false;
            
            // Stop and hide animation
            if (g_pBall) {
                g_pBall->Stop();
                g_pBall->Hide();
            }
            
            // Re-enable Start button, change Cancel back to Close
            EnableWindow(g_hBtnStart, TRUE);
            SetWindowTextW(g_hBtnCancel, t("updater_btn_close").c_str());
            InvalidateRect(g_hBtnCancel, NULL, TRUE);
            
            // Wait for worker thread
            if (g_workerThread && g_workerThread->joinable()) {
                g_workerThread->join();
            }
            
            return 0;
        }
        
        case WM_CTLCOLORSTATIC: {
            // BEGIN: Make stats text background transparent
            if ((HWND)lParam == g_hStatsText) {
                HDC hdcStatic = (HDC)wParam;
                SetTextColor(hdcStatic, RGB(0, 0, 0));  // Black text
                SetBkMode(hdcStatic, TRANSPARENT);       // Transparent background
                return (INT_PTR)GetStockObject(NULL_BRUSH);  // No background brush
            }
            // END: Make stats text background transparent
            break;
        }
        
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlID == IDC_BTN_START) {
                DrawButton(dis, t("updater_btn_start").c_str(), IsWindowEnabled(g_hBtnStart));
            }
            else if (dis->CtlID == IDC_BTN_CANCEL) {
                // Get current button text (changes between Close/Cancel)
                wchar_t btnText[256];
                GetWindowTextW(g_hBtnCancel, btnText, 256);
                DrawButton(dis, btnText, IsWindowEnabled(g_hBtnCancel));
            }
            else if (dis->CtlID == IDC_BTN_DONE) {
                DrawButton(dis, t("updater_btn_done").c_str(), IsWindowEnabled(g_hBtnDone));
            }
            else if (dis->CtlID == IDC_BTN_VIEW_LOG) {
                DrawButton(dis, t("updater_btn_view_log").c_str(), IsWindowEnabled(g_hBtnViewLog));
            }
            return TRUE;
        }
        
        case WM_CONTEXTMENU: {
            HWND hTarget = (HWND)wParam;
            // Only show context menu for log text control
            if (hTarget == g_hLogText) {
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, IDC_CONTEXT_COPY, L"Copy\tCtrl+C");
                AppendMenuW(hMenu, MF_STRING, IDC_CONTEXT_SELECT_ALL, L"Select All\tCtrl+A");
                
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                
                // If invoked via keyboard (lParam == -1), use cursor position
                if (lParam == -1) {
                    GetCursorPos(&pt);
                }
                
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }
        
        case WM_CLOSE: {
            // Don't allow close while update is running
            if (g_updateRunning) {
                MessageBoxW(hwnd, L"Please wait for the update to complete or click Cancel.", 
                           t("updater_title").c_str(), MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        
        case WM_DESTROY: {
            // Cleanup
            if (g_pBall) {
                delete g_pBall;
                g_pBall = nullptr;
            }
            if (g_workerThread) {
                if (g_workerThread->joinable()) g_workerThread->join();
                delete g_workerThread;
                g_workerThread = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Silent mode - log to file
static int RunSilentMode(const std::wstring& dbPath) {
    // Set up file logging
    std::wstring logPath = dbPath;
    size_t lastSlash = logPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        logPath = logPath.substr(0, lastSlash + 1) + L"updater_log.txt";
    } else {
        logPath = L"updater_log.txt";
    }
    
    std::ofstream logFile;
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, NULL, 0, NULL, NULL);
    if (sizeNeeded > 0) {
        std::string logPathUtf8(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, &logPathUtf8[0], sizeNeeded, NULL, NULL);
        logFile.open(logPathUtf8, std::ios::out | std::ios::trunc);
    }
    
    // Backup database
    std::wstring backupPath = dbPath + L".backup";
    try {
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::copy_file(dbPath, backupPath, std::filesystem::copy_options::overwrite_existing);
        }
    } catch (...) {
        if (logFile.is_open()) {
            logFile << "WARNING: Failed to create database backup - continuing anyway" << std::endl;
        }
        // Continue anyway - backup failure shouldn't stop the update
    }
    
    // Run updater with file logging
    WinProgramUpdater updater(dbPath);
    
    if (logFile.is_open()) {
        updater.SetLogCallback([](const std::string& msg, void* userData) {
            std::ofstream* pFile = (std::ofstream*)userData;
            if (pFile && pFile->is_open()) {
                (*pFile) << msg;
                if (msg.back() != '\n') (*pFile) << std::endl;
                pFile->flush();
            }
        }, &logFile);
    }
    
    UpdateStats stats;
    bool success = updater.UpdateDatabase(stats);
    
    if (success) {
        // Delete backup on success
        try {
            std::filesystem::remove(backupPath);
        } catch (...) {}
    }
    
    if (logFile.is_open()) {
        logFile << "\n=== Update " << (success ? "Complete" : "Failed") << " ===" << std::endl;
        logFile.close();
    }
    
    return success ? 0 : 1;
}

// Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)nCmdShow;
    
    // Check for --hidden flag (silent mode)
    bool silentMode = false;
    if (pCmdLine && (wcsstr(pCmdLine, L"--hidden") || wcsstr(pCmdLine, L"/hidden"))) {
        silentMode = true;
    }
    
    // Get database path (same directory as executable)
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    g_dbPath = exePath;
    size_t lastSlash = g_dbPath.find_last_of(L"\\/");
    std::wstring exeDir;
    if (lastSlash != std::wstring::npos) {
        exeDir = g_dbPath.substr(0, lastSlash + 1);
        g_dbPath = exeDir + L"WinProgramManager.db";
    } else {
        g_dbPath = L"WinProgramManager.db";
    }
    
    // Set working directory
    if (!exeDir.empty()) {
        SetCurrentDirectoryW(exeDir.c_str());
    }
    
    // Run silent mode if requested
    if (silentMode) {
        return RunSilentMode(g_dbPath);
    }
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
    
    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    
    // Create window
    g_hWnd = CreateWindowExW(0, CLASS_NAME, t("updater_title").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 480,
        NULL, NULL, hInstance, NULL);
    
    if (g_hWnd == NULL) {
        return 1;
    }
    
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
    
    // Create accelerator table for keyboard shortcuts
    HACCEL hAccel = CreateKeyboardAccelerators();
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!ProcessAccelerator(hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    if (hAccel) {
        DestroyAcceleratorTable(hAccel);
    }
    
    return (int)msg.wParam;
}
