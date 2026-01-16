#pragma once
#include <windows.h>
#include <string>

// Show a localized confirmation dialog for skipping a specific update version.
// Returns true if user confirmed (pressed the affirmative button), false otherwise.
bool ShowSkipConfirm(HWND parent, const std::wstring &appname, const std::wstring &availableVersion);
