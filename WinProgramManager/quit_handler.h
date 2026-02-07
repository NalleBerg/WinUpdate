#pragma once
#include <windows.h>
#include <string>

// Shows quit confirmation dialog and returns true if user confirms
bool ShowQuitConfirmation(HWND hParent, const std::wstring& title, const std::wstring& message, 
                          const std::wstring& yesBtn, const std::wstring& noBtn);

// Handles Ctrl+W: closes child dialogs first, then main window with confirmation
// Returns true if handled
bool HandleCtrlW(HWND hwnd);
