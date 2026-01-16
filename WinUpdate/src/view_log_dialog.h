#pragma once
#include <windows.h>
#include <string>

// Show install log dialog with the last installation output
// Returns true if user closed the dialog
bool ShowInstallLogDialog(HWND parent, const std::string &locale);
