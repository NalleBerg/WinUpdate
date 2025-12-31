// Dialog to list skipped entries and allow unskipping
#pragma once
#include <windows.h>

// Show a modal Unskip dialog. Returns true if user performed any unskips.
bool ShowUnskipDialog(HWND parent);
