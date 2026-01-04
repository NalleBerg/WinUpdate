#ifndef STARTUP_MANAGER_H
#define STARTUP_MANAGER_H

#include <string>

// Create a Windows startup shortcut with --hidden parameter
// Returns true on success, false on failure
bool CreateStartupShortcut();

// Delete the Windows startup shortcut if it exists
// Returns true if shortcut was deleted or didn't exist, false on error
bool DeleteStartupShortcut();

// Check if the Windows startup shortcut exists
// Returns true if shortcut exists, false otherwise
bool StartupShortcutExists();

// Get the full path to the startup shortcut
std::wstring GetStartupShortcutPath();

#endif // STARTUP_MANAGER_H
