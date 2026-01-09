#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

// Show the configuration dialog
// Returns true if settings changed
bool ShowConfigDialog(HWND parent, const std::string &currentLocale);

// Check if "Add to systray now" button was clicked (resets flag after reading)
bool WasAddToTrayNowClicked();

// Load excluded apps from [excluded_apps] section in settings INI
void LoadExcludeSettings(std::unordered_map<std::string, std::string> &excludedApps);

// Save excluded apps to [excluded_apps] section in settings INI
void SaveExcludeSettings(const std::unordered_map<std::string, std::string> &excludedApps);
