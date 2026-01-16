#include "exclude.h"
#include "Config.h"
#include <windows.h>

bool ExcludeApp(const std::string& packageId, const std::string& reason) {
    if (packageId.empty()) {
        return false;
    }

    WaitForSingleObject(g_excluded_mutex, INFINITE);
    g_excluded_apps[packageId] = reason;
    ReleaseMutex(g_excluded_mutex);

    // Save to INI file
    SaveExcludeSettings(g_excluded_apps);
    return true;
}

bool UnexcludeApp(const std::string& packageId) {
    if (packageId.empty()) {
        return false;
    }

    WaitForSingleObject(g_excluded_mutex, INFINITE);
    auto it = g_excluded_apps.find(packageId);
    if (it != g_excluded_apps.end()) {
        g_excluded_apps.erase(it);
        ReleaseMutex(g_excluded_mutex);
        
        // Save to INI file
        SaveExcludeSettings(g_excluded_apps);
        return true;
    }
    ReleaseMutex(g_excluded_mutex);
    return false;
}

bool IsExcluded(const std::string& packageId) {
    if (packageId.empty()) {
        return false;
    }

    WaitForSingleObject(g_excluded_mutex, INFINITE);
    bool excluded = (g_excluded_apps.find(packageId) != g_excluded_apps.end());
    ReleaseMutex(g_excluded_mutex);
    return excluded;
}

std::string GetExcludeReason(const std::string& packageId) {
    if (packageId.empty()) {
        return "";
    }

    WaitForSingleObject(g_excluded_mutex, INFINITE);
    auto it = g_excluded_apps.find(packageId);
    std::string reason = (it != g_excluded_apps.end()) ? it->second : "";
    ReleaseMutex(g_excluded_mutex);
    return reason;
}
