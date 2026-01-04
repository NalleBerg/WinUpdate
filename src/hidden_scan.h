#ifndef HIDDEN_SCAN_H
#define HIDDEN_SCAN_H

#include <windows.h>
#include <string>

// Perform silent winget scan and only show UI if updates are available
// Returns true if updates were found and UI was shown, false otherwise
bool PerformHiddenScan();

#endif // HIDDEN_SCAN_H
