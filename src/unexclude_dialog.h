#ifndef UNEXCLUDE_DIALOG_H
#define UNEXCLUDE_DIALOG_H

#include <windows.h>
#include <string>

// Show the "Manage Excluded Apps" dialog
// Returns true if any apps were unexcluded
bool ShowUnexcludeDialog(HWND parent, const std::string& locale);

#endif // UNEXCLUDE_DIALOG_H
