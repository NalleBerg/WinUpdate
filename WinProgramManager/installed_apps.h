#ifndef INSTALLED_APPS_H
#define INSTALLED_APPS_H

#include <string>
#include "sqlite3/sqlite3.h"

// Initialize the installed apps module
void InitInstalledApps();

// Load installed package IDs from database into memory
void LoadInstalledPackageIds(sqlite3* db);

// Check if a package is installed
bool IsPackageInstalled(const std::wstring& packageId);

// Get/Set filter active state
bool IsInstalledFilterActive();
void SetInstalledFilterActive(bool active);

// Clear installed apps cache
void ClearInstalledApps();

// Get the count of installed packages (for debugging)
size_t GetInstalledPackageCount();

// Sync installed apps with winget (query actual installed packages and update database)
void SyncInstalledAppsWithWinget(sqlite3* db);

// Cleanup: Remove apps from installed_apps that are no longer installed (registry check only)
void CleanupInstalledApps(sqlite3* db);

// Discover: Add apps to installed_apps that are installed but not tracked (winget list)
// Returns true on success, false if winget command failed
bool DiscoverInstalledApps(sqlite3* db);

#endif // INSTALLED_APPS_H
