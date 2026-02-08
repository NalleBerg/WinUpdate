#include "app_details.h"
#include "resource.h"
#include "spinner_dialog.h"
#include "install_dialog.h"
#include "installed_apps.h"
#include <sqlite3.h>
#include <commctrl.h>
#include <sstream>
#include <algorithm>
#include <fstream>

// External locale
extern Locale g_locale;

// Button hover tracking
static HWND g_hHoverButton = nullptr;
static WNDPROC g_oldButtonProc = nullptr;

// Button subclass procedure for hover effects
LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (g_hHoverButton != hwnd) {
                g_hHoverButton = hwnd;
                InvalidateRect(hwnd, NULL, TRUE);
                
                // Track mouse leave
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_hHoverButton == hwnd) {
                g_hHoverButton = nullptr;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }
    }
    return CallWindowProc(g_oldButtonProc, hwnd, msg, wParam, lParam);
}

// Helper function to convert UTF-8 to wide string
static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || *utf8 == '\0') return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], size);
    return result;
}

// Helper function to convert wide string to UTF-8
static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Load app details from database
bool LoadAppDetails(sqlite3* db, const std::wstring& packageId, AppDetailsData& data) {
    if (!db) {
        MessageBoxA(NULL, "Database is NULL", "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    std::string packageIdUtf8 = WideToUtf8(packageId);
    
    // Query main app details
    const char* sql = R"(
        SELECT 
            a.package_id, a.name, a.version, a.publisher,
            a.description, a.homepage, a.license,
            a.icon_data, a.icon_type,
            a.source, a.installer_type, a.architecture,
            i.installed_version
        FROM apps a
        LEFT JOIN installed_apps i ON a.package_id = i.package_id
        WHERE a.package_id = ?
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string errMsg = "SQL Prepare failed: ";
        errMsg += sqlite3_errmsg(db);
        MessageBoxA(NULL, errMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, packageIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
    
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        
        data.package_id = Utf8ToWide((const char*)sqlite3_column_text(stmt, 0));
        data.name = Utf8ToWide((const char*)sqlite3_column_text(stmt, 1));
        data.version = Utf8ToWide((const char*)sqlite3_column_text(stmt, 2));
        data.publisher = Utf8ToWide((const char*)sqlite3_column_text(stmt, 3));
        data.description = Utf8ToWide((const char*)sqlite3_column_text(stmt, 4));
        data.homepage = Utf8ToWide((const char*)sqlite3_column_text(stmt, 5));
        data.license = Utf8ToWide((const char*)sqlite3_column_text(stmt, 6));
        
        // Load icon data if present
        const void* iconBlob = sqlite3_column_blob(stmt, 7);
        int iconSize = sqlite3_column_bytes(stmt, 7);
        if (iconBlob && iconSize > 0) {
            const unsigned char* iconBytes = static_cast<const unsigned char*>(iconBlob);
            data.icon_data.assign(iconBytes, iconBytes + iconSize);
        }
        data.icon_type = Utf8ToWide((const char*)sqlite3_column_text(stmt, 8));
        
        // Get source, installer_type, architecture from apps table (columns 9-11)
        const char* sourceStr = (const char*)sqlite3_column_text(stmt, 9);
        data.source = sourceStr ? Utf8ToWide(sourceStr) : L"";
        
        const char* installerTypeStr = (const char*)sqlite3_column_text(stmt, 10);
        data.installer_type = installerTypeStr ? Utf8ToWide(installerTypeStr) : L"";
        
        const char* archStr = (const char*)sqlite3_column_text(stmt, 11);
        data.architecture = archStr ? Utf8ToWide(archStr) : L"";
        
        // Check if installed (column 12)
        const char* installedVer = (const char*)sqlite3_column_text(stmt, 12);
        if (installedVer && *installedVer) {
            data.is_installed = true;
            data.installed_version = Utf8ToWide(installedVer);
        } else {
            data.is_installed = false;
            data.installed_version = L"";
        }
    }
    
    sqlite3_finalize(stmt);
    
    if (!found) {
        std::string errMsg = "Package not found: " + packageIdUtf8;
        MessageBoxA(NULL, errMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    // Load tags
    // Load tags/categories via proper join
    const char* tagSql = R"(
        SELECT c.category_name 
        FROM categories c
        JOIN app_categories ac ON c.id = ac.category_id
        JOIN apps a ON a.id = ac.app_id
        WHERE a.package_id = ?
        ORDER BY c.category_name
    )";
    rc = sqlite3_prepare_v2(db, tagSql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, packageIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::wstring tag = Utf8ToWide((const char*)sqlite3_column_text(stmt, 0));
            data.tags.push_back(tag);
        }
        
        sqlite3_finalize(stmt);
    }
    
    return true;
}

// Action type for confirmation dialog
enum ActionType {
    ACTION_INSTALL,
    ACTION_UNINSTALL,
    ACTION_REINSTALL
};

// Data structure for confirmation dialog
struct ConfirmActionData {
    ActionType action;
    std::wstring appName;
};

// Confirmation dialog procedure
INT_PTR CALLBACK ConfirmActionDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static ConfirmActionData* pConfirmData = nullptr;
    
    switch (message) {
        case WM_INITDIALOG: {
            pConfirmData = reinterpret_cast<ConfirmActionData*>(lParam);
            if (!pConfirmData) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            
            // Set dialog title and message based on action type
            std::wstring title, message, buttonText;
            switch (pConfirmData->action) {
                case ACTION_INSTALL:
                    title = g_locale.confirm_install_title;
                    message = g_locale.confirm_install_msg;
                    buttonText = g_locale.install_btn;
                    break;
                case ACTION_UNINSTALL:
                    title = g_locale.confirm_uninstall_title;
                    message = g_locale.confirm_uninstall_msg;
                    buttonText = g_locale.uninstall_btn;
                    break;
                case ACTION_REINSTALL:
                    title = g_locale.confirm_reinstall_title;
                    message = g_locale.confirm_reinstall_msg;
                    buttonText = g_locale.reinstall_btn;
                    break;
            }
            
            // Replace %s with app name
            size_t pos = message.find(L"%s");
            if (pos != std::wstring::npos) {
                message.replace(pos, 2, pConfirmData->appName);
            }
            
            SetWindowTextW(hDlg, title.c_str());
            SetDlgItemTextW(hDlg, IDC_CONFIRM_MESSAGE, message.c_str());
            SetDlgItemTextW(hDlg, IDOK, buttonText.c_str());
            SetDlgItemTextW(hDlg, IDCANCEL, g_locale.cancel_btn.c_str());
            
            // Center dialog
            RECT rc, rcParent;
            GetWindowRect(hDlg, &rc);
            GetWindowRect(GetParent(hDlg), &rcParent);
            int x = rcParent.left + (rcParent.right - rcParent.left - (rc.right - rc.left)) / 2;
            int y = rcParent.top + (rcParent.bottom - rcParent.top - (rc.bottom - rc.top)) / 2;
            SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            
            return TRUE;
        }
        
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            break;
    }
    
    return FALSE;
}

// Dialog procedure for app details
INT_PTR CALLBACK AppDetailsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static AppDetailsData* pData = nullptr;
    static HFONT hBoldFont = nullptr;
    static HICON hDefaultIcon = nullptr;
    
    switch (message) {
        case WM_INITDIALOG: {
            pData = reinterpret_cast<AppDetailsData*>(lParam);
            if (!pData) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            
            // Set dialog title
            SetWindowTextW(hDlg, g_locale.app_details_title.c_str());
            
            // Set all static labels
            SetDlgItemTextW(hDlg, IDC_LABEL_PUBLISHER, g_locale.publisher_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_VERSION, g_locale.version_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_PACKAGE_ID, g_locale.package_id_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_SOURCE, g_locale.source_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_STATUS, g_locale.status_label.c_str());
            SetDlgItemTextW(hDlg, IDC_GROUPBOX_DESCRIPTION, g_locale.description_label.c_str());
            SetDlgItemTextW(hDlg, IDC_GROUPBOX_TECHNICAL, g_locale.technical_info_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_HOMEPAGE, g_locale.homepage_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_LICENSE, g_locale.license_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_INSTALLER_TYPE, g_locale.installer_type_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_ARCHITECTURE, g_locale.architecture_label.c_str());
            SetDlgItemTextW(hDlg, IDC_LABEL_TAGS, g_locale.tags_label.c_str());
            
            // Center dialog on parent
            HWND hParent = GetParent(hDlg);
            if (hParent) {
                RECT rcParent, rcDlg;
                GetWindowRect(hParent, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
                int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
                SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            
            // Create bold font for app name
            HWND hNameCtrl = GetDlgItem(hDlg, IDC_APP_NAME);
            if (hNameCtrl) {
                HFONT hFont = (HFONT)SendMessage(hNameCtrl, WM_GETFONT, 0, 0);
                LOGFONT lf = {};
                GetObject(hFont, sizeof(lf), &lf);
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = (lf.lfHeight * 5) / 4;  // 25% larger
                hBoldFont = CreateFontIndirect(&lf);
                SendMessage(hNameCtrl, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
            }
            
            // Set app name
            SetDlgItemText(hDlg, IDC_APP_NAME, pData->name.c_str());
            
            // Set publisher
            SetDlgItemText(hDlg, IDC_APP_PUBLISHER, pData->publisher.empty() ? g_locale.unknown.c_str() : pData->publisher.c_str());
            
            // Set version
            SetDlgItemText(hDlg, IDC_APP_VERSION, pData->version.empty() ? g_locale.unknown.c_str() : pData->version.c_str());
            
            // Set package ID
            SetDlgItemText(hDlg, IDC_APP_PACKAGE_ID, pData->package_id.c_str());
            
            // Set source
            SetDlgItemText(hDlg, IDC_APP_SOURCE, pData->source.empty() ? g_locale.unknown.c_str() : pData->source.c_str());
            
            // Set install status
            std::wstring status;
            if (pData->is_installed) {
                status = g_locale.installed;
                if (!pData->installed_version.empty()) {
                    status += L" (v" + pData->installed_version + L")";
                }
            } else {
                status = g_locale.not_installed;
            }
            SetDlgItemText(hDlg, IDC_APP_STATUS, status.c_str());
            
            // Set description
            SetDlgItemText(hDlg, IDC_APP_DESCRIPTION, 
                          pData->description.empty() ? g_locale.no_description.c_str() : pData->description.c_str());
            
            // Set homepage (plain text)
            SetDlgItemText(hDlg, IDC_APP_HOMEPAGE, 
                          pData->homepage.empty() ? g_locale.not_available.c_str() : pData->homepage.c_str());
            
            // Set license
            SetDlgItemText(hDlg, IDC_APP_LICENSE, 
                          pData->license.empty() ? g_locale.unknown.c_str() : pData->license.c_str());
            
            // Set installer type
            SetDlgItemText(hDlg, IDC_APP_INSTALLER, 
                          pData->installer_type.empty() ? g_locale.unknown.c_str() : pData->installer_type.c_str());
            
            // Set architecture
            SetDlgItemText(hDlg, IDC_APP_ARCH, 
                          pData->architecture.empty() ? g_locale.unknown.c_str() : pData->architecture.c_str());
            
            // Format and set tags
            std::wstring tagsText;
            if (pData->tags.empty()) {
                tagsText = g_locale.no_tags;
            } else {
                for (size_t i = 0; i < pData->tags.size(); i++) {
                    if (i > 0) tagsText += L", ";
                    tagsText += pData->tags[i];
                }
            }
            SetDlgItemText(hDlg, IDC_APP_TAGS, tagsText.c_str());
            
            // Enable horizontal scrolling by calculating text width
            HWND hTagsEdit = GetDlgItem(hDlg, IDC_APP_TAGS);
            HDC hdc = GetDC(hTagsEdit);
            SIZE size;
            GetTextExtentPoint32W(hdc, tagsText.c_str(), (int)tagsText.length(), &size);
            ReleaseDC(hTagsEdit, hdc);
            SendMessage(hTagsEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
            SendMessage(hTagsEdit, EM_SETLIMITTEXT, 0, 0);
            // Scroll to beginning to show start of text
            SendMessage(hTagsEdit, EM_SETSEL, 0, 0);
            SendMessage(hTagsEdit, EM_SCROLLCARET, 0, 0);
            
            // Set icon - use passed icon or load default package icon
            HWND hIconCtrl = GetDlgItem(hDlg, IDC_APP_ICON);
            if (pData->hIcon) {
                // Use the already-loaded icon from list view (much faster!)
                SendMessage(hIconCtrl, STM_SETICON, (WPARAM)pData->hIcon, 0);
            } else {
                // Load default package icon from shell32.dll (generic application icon)
                hDefaultIcon = (HICON)LoadImage(nullptr, MAKEINTRESOURCE(2), IMAGE_ICON, 128, 128, LR_SHARED);
                if (!hDefaultIcon) {
                    // Fallback to application's own icon
                    hDefaultIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APP_ICON));
                }
                if (hDefaultIcon) {
                    SendMessage(hIconCtrl, STM_SETICON, (WPARAM)hDefaultIcon, 0);
                }
            }
            
            // Show/hide buttons based on installation status
            if (pData->is_installed) {
                // App is installed - show Reinstall and Uninstall, hide Install
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_INSTALL), SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_REINSTALL), SW_SHOW);
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_UNINSTALL), SW_SHOW);
            } else {
                // App is not installed - show Install, hide Reinstall and Uninstall
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_INSTALL), SW_SHOW);
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_REINSTALL), SW_HIDE);
                ShowWindow(GetDlgItem(hDlg, IDC_BTN_UNINSTALL), SW_HIDE);
            }
            
            // Set button texts dynamically from locale
            SetWindowTextW(GetDlgItem(hDlg, IDC_BTN_INSTALL), g_locale.install_btn.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_BTN_REINSTALL), g_locale.reinstall_btn.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDC_BTN_UNINSTALL), g_locale.uninstall_btn.c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDOK), g_locale.about_close.c_str());
            
            // Prevent description field from being auto-selected
            HWND hDescription = GetDlgItem(hDlg, IDC_APP_DESCRIPTION);
            if (hDescription) {
                SendMessage(hDescription, EM_SETSEL, -1, 0);  // Deselect all
            }
            
            // Subclass owner-drawn buttons for hover effects
            HWND hInstall = GetDlgItem(hDlg, IDC_BTN_INSTALL);
            HWND hReinstall = GetDlgItem(hDlg, IDC_BTN_REINSTALL);
            HWND hUninstall = GetDlgItem(hDlg, IDC_BTN_UNINSTALL);
            HWND hClose = GetDlgItem(hDlg, IDOK);
            if (hInstall && !g_oldButtonProc) {
                g_oldButtonProc = (WNDPROC)SetWindowLongPtr(hInstall, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }
            if (hReinstall && g_oldButtonProc) {
                SetWindowLongPtr(hReinstall, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }
            if (hUninstall && g_oldButtonProc) {
                SetWindowLongPtr(hUninstall, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }
            if (hClose && g_oldButtonProc) {
                SetWindowLongPtr(hClose, GWLP_WNDPROC, (LONG_PTR)ButtonSubclassProc);
            }
            
            // Set focus to dialog itself to prevent field auto-selection
            SetFocus(hDlg);
            
            return FALSE;  // We handled focus ourselves
        }
        
        case WM_CTLCOLORSTATIC: {
            // Set white background and black text for all static and edit controls
            HDC hdcStatic = (HDC)wParam;
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            SetTextColor(hdcStatic, RGB(0, 0, 0));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        
        case WM_CTLCOLOREDIT: {
            // Set white background for edit controls
            HDC hdcEdit = (HDC)wParam;
            SetBkColor(hdcEdit, RGB(255, 255, 255));
            SetTextColor(hdcEdit, RGB(0, 0, 0));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        
        case WM_CTLCOLORDLG: {
            // Set white background for dialog itself
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;
            if (pDIS->CtlType == ODT_BUTTON) {
                HDC hdc = pDIS->hDC;
                RECT rc = pDIS->rcItem;
                bool isPressed = (pDIS->itemState & ODS_SELECTED);
                bool isHover = (g_hHoverButton == pDIS->hwndItem);
                
                // Determine button color based on ID
                COLORREF baseColor, hoverColor, pressedColor;
                if (pDIS->CtlID == IDC_BTN_INSTALL) {
                    // Green for Install
                    baseColor = RGB(40, 180, 40);
                    hoverColor = RGB(60, 200, 60);
                    pressedColor = RGB(30, 140, 30);
                } else if (pDIS->CtlID == IDC_BTN_REINSTALL || pDIS->CtlID == IDOK) {
                    // Blue for Reinstall and Close
                    baseColor = RGB(40, 120, 200);
                    hoverColor = RGB(60, 140, 220);
                    pressedColor = RGB(30, 100, 160);
                } else if (pDIS->CtlID == IDC_BTN_UNINSTALL) {
                    // Red for Uninstall
                    baseColor = RGB(180, 40, 40);
                    hoverColor = RGB(200, 60, 60);
                    pressedColor = RGB(140, 30, 30);
                } else {
                    return FALSE;
                }
                
                // Select color based on state
                COLORREF bgColor = isPressed ? pressedColor : (isHover ? hoverColor : baseColor);
                
                // Draw button background
                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                
                // Draw button text
                wchar_t text[64];
                GetWindowTextW(pDIS->hwndItem, text, 64);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                // Draw focus rectangle if focused
                if (pDIS->itemState & ODS_FOCUS) {
                    RECT rcFocus = rc;
                    InflateRect(&rcFocus, -3, -3);
                    DrawFocusRect(hdc, &rcFocus);
                }
                
                return TRUE;
            }
            break;
        }
        
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                // Clean up resources
                if (hBoldFont) {
                    DeleteObject(hBoldFont);
                    hBoldFont = nullptr;
                }
                // Don't delete hIcon - it's owned by the list view or is shared
                hDefaultIcon = nullptr;
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            else if (LOWORD(wParam) == IDC_BTN_INSTALL) {
                // Show confirmation dialog for install
                ConfirmActionData confirmData;
                confirmData.action = ACTION_INSTALL;
                confirmData.appName = pData->name;
                
                INT_PTR result = DialogBoxParam(GetModuleHandle(nullptr), 
                                               MAKEINTRESOURCE(IDD_CONFIRM_ACTION),
                                               hDlg, ConfirmActionDialogProc, 
                                               reinterpret_cast<LPARAM>(&confirmData));
                
                if (result == IDOK) {
                    // User confirmed - install the application
                    std::vector<std::string> packageIds;
                    // Convert package ID to UTF-8
                    int size = WideCharToMultiByte(CP_UTF8, 0, pData->package_id.c_str(), -1, NULL, 0, NULL, NULL);
                    if (size > 0) {
                        std::string pkgIdUtf8(size - 1, 0);
                        WideCharToMultiByte(CP_UTF8, 0, pData->package_id.c_str(), -1, &pkgIdUtf8[0], size, NULL, NULL);
                        packageIds.push_back(pkgIdUtf8);
                    }
                    
                    // Show install dialog with the WinUpdate UI
                    // Capture values by copy to avoid warnings about static pointer capture
                    sqlite3* db = pData->db;
                    std::wstring pkgId = pData->package_id;
                    HWND hDialog = hDlg;  // Capture dialog handle for UI update
                    
                    ShowInstallDialog(hDlg, packageIds, g_locale.close, 
                                     [](const char* key) -> std::wstring {
                                         // Simple passthrough - we don't have translation keys from WinUpdate
                                         return std::wstring(key, key + strlen(key));
                                     },
                                     [db, pkgId, hDialog, pDataPtr = pData]() {
                                         // Sync installed apps after installation completes
                                         AppDetailsData* pData = pDataPtr;
                                         if (db) {
                                             // Ensure table exists
                                             const char* createTableSql = 
                                                 "CREATE TABLE IF NOT EXISTS installed_apps ("
                                                 "    package_id TEXT PRIMARY KEY,"
                                                 "    installed_date TEXT,"
                                                 "    last_seen TEXT,"
                                                 "    installed_version TEXT,"
                                                 "    source TEXT"
                                                 ");";
                                             sqlite3_exec(db, createTableSql, nullptr, nullptr, nullptr);
                                             
                                             // Add this package to installed_apps
                                             int size = WideCharToMultiByte(CP_UTF8, 0, pkgId.c_str(), -1, NULL, 0, NULL, NULL);
                                             if (size > 0) {
                                                 std::string pkgIdUtf8(size - 1, 0);
                                                 WideCharToMultiByte(CP_UTF8, 0, pkgId.c_str(), -1, &pkgIdUtf8[0], size, NULL, NULL);
                                                 
                                                 char timestamp[32];
                                                 time_t now = time(nullptr);
                                                 struct tm tm_now;
                                                 localtime_s(&tm_now, &now);
                                                 strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
                                                 
                                                 const char* insertSql = "INSERT OR REPLACE INTO installed_apps (package_id, installed_date, last_seen, source) VALUES (?, ?, ?, 'winget')";
                                                 sqlite3_stmt* stmt = nullptr;
                                                 if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
                                                     sqlite3_bind_text(stmt, 1, pkgIdUtf8.c_str(), -1, SQLITE_TRANSIENT);
                                                     sqlite3_bind_text(stmt, 2, timestamp, -1, SQLITE_TRANSIENT);
                                                     sqlite3_bind_text(stmt, 3, timestamp, -1, SQLITE_TRANSIENT);
                                                     sqlite3_step(stmt);
                                                     sqlite3_finalize(stmt);
                                                 }
                                             }
                                             
                                             // Run cleanup to sync installed apps list
                                             CleanupInstalledApps(db);
                                             
                                             // Update in-memory data
                                             pData->is_installed = true;
                                             pData->installed_version = pData->version;
                                             
                                             // Update UI
                                             if (IsWindow(hDialog)) {
                                                 HWND hStatusValue = GetDlgItem(hDialog, IDC_APP_STATUS);
                                                 if (hStatusValue) {
                                                     SetWindowTextW(hStatusValue, g_locale.installed.c_str());
                                                 }
                                                 
                                                 HWND hInstallBtn = GetDlgItem(hDialog, IDC_BTN_INSTALL);
                                                 HWND hUninstallBtn = GetDlgItem(hDialog, IDC_BTN_UNINSTALL);
                                                 HWND hReinstallBtn = GetDlgItem(hDialog, IDC_BTN_REINSTALL);
                                                 
                                                 if (hInstallBtn) EnableWindow(hInstallBtn, FALSE);
                                                 if (hUninstallBtn) EnableWindow(hUninstallBtn, TRUE);
                                                 if (hReinstallBtn) EnableWindow(hReinstallBtn, TRUE);
                                             }
                                         }
                                     });
                }
                return TRUE;
            }
            else if (LOWORD(wParam) == IDC_BTN_UNINSTALL) {
                // Show confirmation dialog for uninstall
                ConfirmActionData confirmData;
                confirmData.action = ACTION_UNINSTALL;
                confirmData.appName = pData->name;
                
                INT_PTR result = DialogBoxParam(GetModuleHandle(nullptr), 
                                               MAKEINTRESOURCE(IDD_CONFIRM_ACTION),
                                               hDlg, ConfirmActionDialogProc, 
                                               reinterpret_cast<LPARAM>(&confirmData));
                
                if (result == IDOK) {
                    // User confirmed - uninstall the application
                    // TODO: Implement actual uninstallation logic
                    // TODO: After uninstall completes, call: SyncInstalledAppsWithWinget(pData->db);
                    MessageBoxW(hDlg, L"Uninstallation functionality will be implemented next.", 
                               g_locale.uninstall_btn.c_str(), MB_OK | MB_ICONINFORMATION);
                }
                return TRUE;
            }
            else if (LOWORD(wParam) == IDC_BTN_REINSTALL) {
                // Show confirmation dialog for reinstall
                ConfirmActionData confirmData;
                confirmData.action = ACTION_REINSTALL;
                confirmData.appName = pData->name;
                
                INT_PTR result = DialogBoxParam(GetModuleHandle(nullptr), 
                                               MAKEINTRESOURCE(IDD_CONFIRM_ACTION),
                                               hDlg, ConfirmActionDialogProc, 
                                               reinterpret_cast<LPARAM>(&confirmData));
                
                if (result == IDOK) {
                    // User confirmed - reinstall the application
                    // TODO: Implement actual reinstallation logic (uninstall + install)
                    // TODO: After reinstall completes, call: SyncInstalledAppsWithWinget(pData->db);
                    MessageBoxW(hDlg, L"Reinstallation functionality will be implemented next.", 
                               g_locale.reinstall_btn.c_str(), MB_OK | MB_ICONINFORMATION);
                }
                return TRUE;
            }
            break;
        
        case WM_CLOSE:
            // Clean up resources
            if (hBoldFont) {
                DeleteObject(hBoldFont);
                hBoldFont = nullptr;
            }
            // Don't delete hIcon - it's owned by the list view or is shared
            hDefaultIcon = nullptr;
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    
    return FALSE;
}

// Show app details dialog
void ShowAppDetailsDialog(HWND hParent, sqlite3* db, const std::wstring& packageId, HICON hAppIcon) {
    // Initialize SysLink control (required for hyperlinks)
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);
    
    AppDetailsData data;
    
    if (!LoadAppDetails(db, packageId, data)) {
        MessageBox(hParent, L"Failed to load application details.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Store database pointer for sync after operations
    data.db = db;
    
    // Pass the already-loaded icon to the dialog
    data.hIcon = hAppIcon;
    
    DialogBoxParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_APP_DETAILS), 
                   hParent, AppDetailsDialogProc, reinterpret_cast<LPARAM>(&data));
}
