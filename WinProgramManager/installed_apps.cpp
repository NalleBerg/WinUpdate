#include "installed_apps.h"
#include <windows.h>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>
#include <sstream>

// Static module-level state
static bool g_installedFilterActive = false;
static std::set<std::wstring> g_installedPackageIds;

void InitInstalledApps() {
    g_installedFilterActive = false;
    g_installedPackageIds.clear();
}

void LoadInstalledPackageIds(sqlite3* db) {
    g_installedPackageIds.clear();
    if (!db) return;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM installed_apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                int wsize = MultiByteToWideChar(CP_UTF8, 0, pkgId, -1, nullptr, 0);
                if (wsize > 0) {
                    std::wstring wstr(wsize - 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, pkgId, -1, &wstr[0], wsize);
                    g_installedPackageIds.insert(wstr);
                }
            }
        }
        sqlite3_finalize(stmt);
    }
}

bool IsPackageInstalled(const std::wstring& packageId) {
    return g_installedPackageIds.count(packageId) > 0;
}

bool IsInstalledFilterActive() {
    return g_installedFilterActive;
}

void SetInstalledFilterActive(bool active) {
    g_installedFilterActive = active;
}

void ClearInstalledApps() {
    g_installedPackageIds.clear();
}

size_t GetInstalledPackageCount() {
    return g_installedPackageIds.size();
}

// Helper to check if a package ID exists in Windows registry
static bool IsInstalledInRegistry(const std::string& packageId) {
    // Convert package ID to wide string
    int wsize = MultiByteToWideChar(CP_UTF8, 0, packageId.c_str(), -1, nullptr, 0);
    if (wsize <= 0) return false;
    std::wstring wPackageId(wsize - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, packageId.c_str(), -1, &wPackageId[0], wsize);
    
    // Normalize package ID for matching (lowercase, remove special chars)
    auto normalize = [](const std::wstring& str) -> std::wstring {
        std::wstring result;
        for (wchar_t c : str) {
            wchar_t lower = towlower(c);
            if ((lower >= L'a' && lower <= L'z') || (lower >= L'0' && lower <= L'9')) {
                result += lower;
            }
        }
        return result;
    };
    
    // Split package ID by dot (e.g., "7zip.7zip" -> ["7zip", "7zip"])
    std::wstring pkgNorm = normalize(wPackageId);
    std::vector<std::wstring> pkgParts;
    size_t dotPos = wPackageId.find(L'.');
    if (dotPos != std::wstring::npos) {
        pkgParts.push_back(normalize(wPackageId.substr(0, dotPos)));
        pkgParts.push_back(normalize(wPackageId.substr(dotPos + 1)));
    } else {
        pkgParts.push_back(pkgNorm);
    }
    
    // Check both registry locations for installed programs
    const wchar_t* uninstallKeys[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    
    for (int i = 0; i < 2; i++) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstallKeys[i], 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD index = 0;
            wchar_t subKeyName[256];
            DWORD subKeyLen = 256;
            
            while (RegEnumKeyExW(hKey, index++, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                HKEY hSubKey;
                if (RegOpenKeyExW(hKey, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                    // Check DisplayName
                    wchar_t displayName[512] = {0};
                    DWORD displayNameSize = sizeof(displayName);
                    RegQueryValueExW(hSubKey, L"DisplayName", NULL, NULL, (LPBYTE)displayName, &displayNameSize);
                    
                    // Check Publisher
                    wchar_t publisher[256] = {0};
                    DWORD publisherSize = sizeof(publisher);
                    RegQueryValueExW(hSubKey, L"Publisher", NULL, NULL, (LPBYTE)publisher, &publisherSize);
                    
                    // Normalize display name and publisher
                    std::wstring nameNorm = normalize(displayName);
                    std::wstring pubNorm = normalize(publisher);
                    
                    // Match if any part of package ID appears in display name or publisher
                    bool matched = false;
                    for (const auto& part : pkgParts) {
                        if (!part.empty() && (nameNorm.find(part) != std::wstring::npos || 
                                             pubNorm.find(part) != std::wstring::npos)) {
                            matched = true;
                            break;
                        }
                    }
                    
                    // Also check if the full normalized package ID is in the name
                    if (!matched && !pkgNorm.empty() && nameNorm.find(pkgNorm) != std::wstring::npos) {
                        matched = true;
                    }
                    
                    RegCloseKey(hSubKey);
                    
                    if (matched) {
                        RegCloseKey(hKey);
                        return true;
                    }
                }
                subKeyLen = 256;
            }
            RegCloseKey(hKey);
        }
    }
    
    // Also check current user registry
    HKEY hUserKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ, &hUserKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t subKeyName[256];
        DWORD subKeyLen = 256;
        
        while (RegEnumKeyExW(hUserKey, index++, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            HKEY hSubKey;
            if (RegOpenKeyExW(hUserKey, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                wchar_t displayName[512] = {0};
                DWORD displayNameSize = sizeof(displayName);
                RegQueryValueExW(hSubKey, L"DisplayName", NULL, NULL, (LPBYTE)displayName, &displayNameSize);
                
                std::wstring nameNorm = normalize(displayName);
                
                // Check any part
                bool matched = false;
                for (const auto& part : pkgParts) {
                    if (!part.empty() && nameNorm.find(part) != std::wstring::npos) {
                        matched = true;
                        break;
                    }
                }
                
                RegCloseKey(hSubKey);
                
                if (matched) {
                    RegCloseKey(hUserKey);
                    return true;
                }
            }
            subKeyLen = 256;
        }
        RegCloseKey(hUserKey);
    }
    
    return false;
}

void SyncInstalledAppsWithWinget(sqlite3* db) {
    if (!db) return;
    
    // Get all package IDs from the main apps table
    std::set<std::string> allPackages;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                allPackages.insert(pkgId);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Get packages currently marked as installed
    std::set<std::string> dbInstalledPackages;
    sql = "SELECT package_id FROM installed_apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                dbInstalledPackages.insert(pkgId);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Check all packages against registry
    std::set<std::string> toAdd;
    std::set<std::string> toRemove;
    
    // Check each package to see if it's actually installed
    for (const std::string& pkgId : allPackages) {
        bool isInstalled = IsInstalledInRegistry(pkgId);
        bool isMarkedInstalled = dbInstalledPackages.count(pkgId) > 0;
        
        if (isInstalled && !isMarkedInstalled) {
            toAdd.insert(pkgId);
        } else if (!isInstalled && isMarkedInstalled) {
            toRemove.insert(pkgId);
        }
    }
    
    // Get timestamp
    char timestamp[32];
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_s(&tm_now, &now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    
    // Add newly discovered installed packages
    if (!toAdd.empty()) {
        const char* insertSql = "INSERT OR REPLACE INTO installed_apps (package_id, installed_date, last_seen, source) VALUES (?, ?, ?, 'winget');";
        sqlite3_stmt* addStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, insertSql, -1, &addStmt, nullptr) == SQLITE_OK) {
            for (const std::string& pkgId : toAdd) {
                sqlite3_reset(addStmt);
                sqlite3_bind_text(addStmt, 1, pkgId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addStmt, 2, timestamp, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addStmt, 3, timestamp, -1, SQLITE_TRANSIENT);
                sqlite3_step(addStmt);
            }
            sqlite3_finalize(addStmt);
        }
    }
    
    // Remove packages that are no longer installed
    if (!toRemove.empty()) {
        const char* deleteSql = "DELETE FROM installed_apps WHERE package_id = ?;";
        sqlite3_stmt* delStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, deleteSql, -1, &delStmt, nullptr) == SQLITE_OK) {
            for (const std::string& pkgId : toRemove) {
                sqlite3_reset(delStmt);
                sqlite3_bind_text(delStmt, 1, pkgId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(delStmt);
            }
            sqlite3_finalize(delStmt);
        }
    }
    
    // Reload the in-memory cache
    LoadInstalledPackageIds(db);
}

// CleanupInstalledApps: Remove apps from installed_apps that are no longer installed
// Uses registry checking only (fast - checks ~50 apps)
void CleanupInstalledApps(sqlite3* db) {
    if (!db) return;
    
    // Get all packages currently in installed_apps
    std::vector<std::string> installedPackages;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM installed_apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                installedPackages.push_back(pkgId);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Check each installed package against registry
    std::vector<std::string> toRemove;
    for (const std::string& pkgId : installedPackages) {
        if (!IsInstalledInRegistry(pkgId)) {
            toRemove.push_back(pkgId);
        }
    }
    
    // Remove packages that are no longer installed
    if (!toRemove.empty()) {
        const char* deleteSql = "DELETE FROM installed_apps WHERE package_id = ?;";
        sqlite3_stmt* delStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, deleteSql, -1, &delStmt, nullptr) == SQLITE_OK) {
            for (const std::string& pkgId : toRemove) {
                sqlite3_reset(delStmt);
                sqlite3_bind_text(delStmt, 1, pkgId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(delStmt);
            }
            sqlite3_finalize(delStmt);
        }
    }
    
    // Reload the in-memory cache
    LoadInstalledPackageIds(db);
}

// DiscoverInstalledApps: Query winget list and add newly discovered apps
// Returns true on success, false if winget command failed
bool DiscoverInstalledApps(sqlite3* db) {
    if (!db) return false;
    
    // Execute winget list command
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        return false;
    }
    
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStdoutWrite;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    
    std::wstring cmdLine = L"winget list --accept-source-agreements";
    
    if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, 
                        NULL, NULL, &si, &pi)) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStdoutRead);
        return false;
    }
    
    CloseHandle(hStdoutWrite);
    
    // Read output
    std::string output;
    char buffer[4096];
    DWORD bytesRead;
    
    while (ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRead);
    
    if (output.empty()) {
        return false;
    }
    
    // Parse winget list output to extract package IDs
    std::set<std::string> wingetInstalledIds;
    std::istringstream stream(output);
    std::string line;
    bool pastHeader = false;
    
    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.length() < 10) continue;
        
        // Skip header
        if (line.find("Name") != std::string::npos && line.find("Id") != std::string::npos) {
            pastHeader = true;
            continue;
        }
        
        // Skip separator
        if (line.find("---") != std::string::npos) {
            pastHeader = true;
            continue;
        }
        
        if (!pastHeader) continue;
        
        // Extract package ID (format: vendor.product)
        // Look for pattern with dot and letters on both sides
        std::istringstream lineStream(line);
        std::string token;
        std::string foundId;
        
        while (lineStream >> token) {
            // Check if token matches package ID pattern (has dot, letters before and after)
            size_t dotPos = token.find('.');
            if (dotPos != std::string::npos && dotPos > 0 && dotPos < token.length() - 1) {
                bool hasLetterBefore = false, hasLetterAfter = false;
                for (size_t i = 0; i < dotPos; i++) {
                    if (isalpha(token[i])) { hasLetterBefore = true; break; }
                }
                for (size_t i = dotPos + 1; i < token.length(); i++) {
                    if (isalpha(token[i])) { hasLetterAfter = true; break; }
                }
                if (hasLetterBefore && hasLetterAfter) {
                    foundId = token;
                    break;
                }
            }
        }
        
        if (!foundId.empty()) {
            wingetInstalledIds.insert(foundId);
        }
    }
    
    if (wingetInstalledIds.empty()) {
        return false; // No valid package IDs found
    }
    
    // Get packages currently in installed_apps
    std::set<std::string> dbInstalledIds;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM installed_apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                dbInstalledIds.insert(pkgId);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Get packages in our apps table (only add if they're in our supported list)
    std::set<std::string> supportedPackages;
    sql = "SELECT package_id FROM apps;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* pkgId = (const char*)sqlite3_column_text(stmt, 0);
            if (pkgId) {
                supportedPackages.insert(pkgId);
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Find packages to add: in winget list, in our supported list, but not in installed_apps
    std::vector<std::string> toAdd;
    for (const std::string& pkgId : wingetInstalledIds) {
        if (supportedPackages.count(pkgId) > 0 && dbInstalledIds.count(pkgId) == 0) {
            toAdd.push_back(pkgId);
        }
    }
    
    // Add newly discovered packages
    if (!toAdd.empty()) {
        char timestamp[32];
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_s(&tm_now, &now);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
        
        const char* insertSql = "INSERT OR REPLACE INTO installed_apps (package_id, installed_date, last_seen, source) VALUES (?, ?, ?, 'winget');";
        sqlite3_stmt* addStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, insertSql, -1, &addStmt, nullptr) == SQLITE_OK) {
            for (const std::string& pkgId : toAdd) {
                sqlite3_reset(addStmt);
                sqlite3_bind_text(addStmt, 1, pkgId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addStmt, 2, timestamp, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addStmt, 3, timestamp, -1, SQLITE_TRANSIENT);
                sqlite3_step(addStmt);
            }
            sqlite3_finalize(addStmt);
        }
    }
    
    // Reload the in-memory cache
    LoadInstalledPackageIds(db);
    
    return true;
}
