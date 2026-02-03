#include "app_details.h"
#include "resource.h"
#include <sqlite3.h>
#include <commctrl.h>
#include <sstream>
#include <algorithm>

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

// Helper function to create HBITMAP from icon data
static HBITMAP CreateBitmapFromIconData(const std::vector<unsigned char>& iconData, const std::wstring& /* iconType */) {
    if (iconData.empty()) return nullptr;
    
    // For now, return nullptr - we'll implement icon rendering later
    // This would need to handle PNG, ICO, etc. formats based on iconType
    return nullptr;
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

// Dialog procedure for app details
INT_PTR CALLBACK AppDetailsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static AppDetailsData* pData = nullptr;
    static HFONT hBoldFont = nullptr;
    static HBITMAP hIconBitmap = nullptr;
    
    switch (message) {
        case WM_INITDIALOG: {
            pData = reinterpret_cast<AppDetailsData*>(lParam);
            if (!pData) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            
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
            SetDlgItemText(hDlg, IDC_APP_PUBLISHER, pData->publisher.empty() ? L"Unknown" : pData->publisher.c_str());
            
            // Set version
            SetDlgItemText(hDlg, IDC_APP_VERSION, pData->version.empty() ? L"Unknown" : pData->version.c_str());
            
            // Set package ID
            SetDlgItemText(hDlg, IDC_APP_PACKAGE_ID, pData->package_id.c_str());
            
            // Set source
            SetDlgItemText(hDlg, IDC_APP_SOURCE, pData->source.empty() ? L"Unknown" : pData->source.c_str());
            
            // Set install status
            std::wstring status;
            if (pData->is_installed) {
                status = L"Installed";
                if (!pData->installed_version.empty()) {
                    status += L" (v" + pData->installed_version + L")";
                }
            } else {
                status = L"Not Installed";
            }
            SetDlgItemText(hDlg, IDC_APP_STATUS, status.c_str());
            
            // Set description
            SetDlgItemText(hDlg, IDC_APP_DESCRIPTION, 
                          pData->description.empty() ? L"No description available." : pData->description.c_str());
            
            // Set homepage
            SetDlgItemText(hDlg, IDC_APP_HOMEPAGE, 
                          pData->homepage.empty() ? L"Not available" : pData->homepage.c_str());
            
            // Set license
            SetDlgItemText(hDlg, IDC_APP_LICENSE, 
                          pData->license.empty() ? L"Unknown" : pData->license.c_str());
            
            // Set installer type
            SetDlgItemText(hDlg, IDC_APP_INSTALLER, 
                          pData->installer_type.empty() ? L"Unknown" : pData->installer_type.c_str());
            
            // Set architecture
            SetDlgItemText(hDlg, IDC_APP_ARCH, 
                          pData->architecture.empty() ? L"Unknown" : pData->architecture.c_str());
            
            // Format and set tags
            std::wstring tagsText;
            if (pData->tags.empty()) {
                tagsText = L"No tags available";
            } else {
                for (size_t i = 0; i < pData->tags.size(); i++) {
                    if (i > 0) tagsText += L", ";
                    tagsText += pData->tags[i];
                }
            }
            SetDlgItemText(hDlg, IDC_APP_TAGS, tagsText.c_str());
            
            // Load and set icon if available
            if (!pData->icon_data.empty()) {
                hIconBitmap = CreateBitmapFromIconData(pData->icon_data, pData->icon_type);
                if (hIconBitmap) {
                    HWND hIconCtrl = GetDlgItem(hDlg, IDC_APP_ICON);
                    SendMessage(hIconCtrl, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hIconBitmap);
                }
            }
            
            // Disable Install/Uninstall buttons for now (will implement later)
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_INSTALL), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNINSTALL), FALSE);
            
            return TRUE;
        }
        
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                // Clean up resources
                if (hBoldFont) {
                    DeleteObject(hBoldFont);
                    hBoldFont = nullptr;
                }
                if (hIconBitmap) {
                    DeleteObject(hIconBitmap);
                    hIconBitmap = nullptr;
                }
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            break;
        
        case WM_CLOSE:
            // Clean up resources
            if (hBoldFont) {
                DeleteObject(hBoldFont);
                hBoldFont = nullptr;
            }
            if (hIconBitmap) {
                DeleteObject(hIconBitmap);
                hIconBitmap = nullptr;
            }
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    
    return FALSE;
}

// Show app details dialog
void ShowAppDetailsDialog(HWND hParent, sqlite3* db, const std::wstring& packageId) {
    AppDetailsData data;
    
    if (!LoadAppDetails(db, packageId, data)) {
        MessageBox(hParent, L"Failed to load application details.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    DialogBoxParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_APP_DETAILS), 
                   hParent, AppDetailsDialogProc, reinterpret_cast<LPARAM>(&data));
}
