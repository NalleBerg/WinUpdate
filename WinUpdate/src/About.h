#pragma once
#include <windows.h>

// Published timestamp (acts as the hidden version identifier)
// Format: yyyy-mm-dd hh:mm
extern const wchar_t ABOUT_PUBLISHED[];

// Show the About dialog/modal. Parent may be NULL.
void ShowAboutDialog(HWND parent);
// Show the license popup (modal)
void ShowLicenseDialog(HWND parent);
