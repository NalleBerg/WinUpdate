#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// Show modal install/reinstall/uninstall dialog for the given package IDs
// operation: "install", "reinstall", or "uninstall"
// Returns true if operation completed (user clicked Done), false if cancelled
// translateFunc: function to get translated text for a key
// onInstallComplete: optional callback to run after operation completes (e.g., sync database)
bool ShowInstallDialog(HWND hParent, const std::vector<std::string>& packageIds, 
                      const std::wstring& operation = L"install",
                      const std::wstring& doneButtonText = L"Done!",
                      std::function<std::wstring(const char*)> translateFunc = nullptr,
                      std::function<void()> onInstallComplete = nullptr);
