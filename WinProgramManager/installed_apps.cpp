#include "installed_apps.h"
#include <windows.h>
#include <set>

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
