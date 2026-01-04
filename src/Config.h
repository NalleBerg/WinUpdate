#pragma once
#include <windows.h>
#include <string>

// Show the configuration dialog
// Returns true if settings changed
bool ShowConfigDialog(HWND parent, const std::string &currentLocale);
