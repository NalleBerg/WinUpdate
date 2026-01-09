#ifndef EXCLUDE_H
#define EXCLUDE_H

#include <string>
#include <unordered_map>
#include <windows.h>

// Forward declarations of globals (defined in main.cpp)
extern std::unordered_map<std::string,std::string> g_excluded_apps;
extern HANDLE g_excluded_mutex;

// Exclude an app from all future scans
// reason: "auto" for automatically excluded (e.g., MS Store apps)
//         "manual" for user-initiated exclusions
bool ExcludeApp(const std::string& packageId, const std::string& reason);

// Remove an app from the exclusion list
bool UnexcludeApp(const std::string& packageId);

// Check if an app is excluded
bool IsExcluded(const std::string& packageId);

// Get the reason for exclusion ("auto" or "manual")
std::string GetExcludeReason(const std::string& packageId);

#endif // EXCLUDE_H
