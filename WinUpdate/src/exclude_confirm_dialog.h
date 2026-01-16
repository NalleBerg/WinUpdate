#pragma once
#include <windows.h>
#include <string>

// Show a localized confirmation dialog for excluding an app from all future scans.
// Returns true if user confirmed (pressed the affirmative button), false otherwise.
bool ShowExcludeConfirm(HWND parent, const std::wstring &appname);
