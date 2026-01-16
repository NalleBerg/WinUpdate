#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// Show modal install dialog for the given package IDs
// Returns true if install completed (user clicked Done), false if cancelled
// translateFunc: function to get translated text for a key
bool ShowInstallDialog(HWND hParent, const std::vector<std::string>& packageIds, 
                      const std::wstring& doneButtonText = L"Done!",
                      std::function<std::wstring(const char*)> translateFunc = nullptr);
