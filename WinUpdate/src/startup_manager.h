#ifndef STARTUP_MANAGER_H
#define STARTUP_MANAGER_H

#include <string>

// Create a Windows startup shortcut with --hidden parameter
// Returns true on success, false on failure
bool CreateStartupShortcut();

// Create a Windows startup shortcut with custom arguments
// Returns true on success, false on failure
bool CreateStartupShortcut(const wchar_t* arguments, const wchar_t* description);

// Delete the Windows startup shortcut if it exists
// Returns true if shortcut was deleted or didn't exist, false on error
bool DeleteStartupShortcut();

// Check if the Windows startup shortcut exists
// Returns true if shortcut exists, false otherwise
bool StartupShortcutExists();

// Get the full path to the startup shortcut
std::wstring GetStartupShortcutPath();

// Verify startup shortcut matches mode and fix if needed
// mode: 0=Manual (delete), 1=Startup (--hidden), 2=SysTray (--systray)
// Returns true if shortcut is correct or was fixed, false on error
bool VerifyStartupShortcut(int mode);

#endif // STARTUP_MANAGER_H
