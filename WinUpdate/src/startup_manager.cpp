#include "startup_manager.h"
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <comdef.h>
#include <comutil.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

std::wstring GetStartupShortcutPath() {
    wchar_t startupFolder[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, startupFolder))) {
        std::wstring path = startupFolder;
        path += L"\\WinUpdate.lnk";
        return path;
    }
    return L"";
}

bool StartupShortcutExists() {
    std::wstring shortcutPath = GetStartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }
    
    DWORD attrib = GetFileAttributesW(shortcutPath.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool CreateStartupShortcut() {
    return CreateStartupShortcut(L"--hidden", L"WinUpdate - Windows Update Manager (Hidden Scan)");
}

bool CreateStartupShortcut(const wchar_t* arguments, const wchar_t* description) {
    // Get the executable path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    // Get working directory
    wchar_t workingDir[MAX_PATH];
    wcscpy_s(workingDir, exePath);
    PathRemoveFileSpecW(workingDir);
    
    // Get shortcut path
    std::wstring shortcutPath = GetStartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }
    
    // Initialize COM
    CoInitialize(NULL);
    
    bool success = false;
    IShellLinkW* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, 
                                   IID_IShellLinkW, (void**)&pShellLink);
    
    if (SUCCEEDED(hr)) {
        // Set the executable path
        pShellLink->SetPath(exePath);
        
        // Set arguments
        pShellLink->SetArguments(arguments);
        
        // Set working directory
        pShellLink->SetWorkingDirectory(workingDir);
        
        // Set description
        pShellLink->SetDescription(description);
        
        // Get the IPersistFile interface
        hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        
        if (SUCCEEDED(hr)) {
            // Save the shortcut
            hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
            success = SUCCEEDED(hr);
            pPersistFile->Release();
        }
        
        pShellLink->Release();
    }
    
    CoUninitialize();
    return success;
}

bool DeleteStartupShortcut() {
    std::wstring shortcutPath = GetStartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }
    
    // If shortcut doesn't exist, consider it success
    if (!StartupShortcutExists()) {
        return true;
    }
    
    // Delete the shortcut
    return DeleteFileW(shortcutPath.c_str()) != 0;
}
bool VerifyStartupShortcut(int mode) {
    // mode: 0=Manual (no shortcut), 1=Startup (--hidden), 2=SysTray (--systray)
    
    bool shortcutExists = StartupShortcutExists();
    
    if (mode == 0) {
        // Manual mode: shortcut should not exist
        if (shortcutExists) {
            return DeleteStartupShortcut();
        }
        return true; // Already correct
    } else if (mode == 1) {
        // Startup mode: shortcut should exist with --hidden
        // Always recreate to ensure it's correct
        if (shortcutExists) {
            DeleteStartupShortcut();
        }
        return CreateStartupShortcut();
    } else if (mode == 2) {
        // SysTray mode: shortcut should exist with --systray
        // Always recreate to ensure it's correct
        if (shortcutExists) {
            DeleteStartupShortcut();
        }
        return CreateStartupShortcut(L"--systray", L"WinUpdate - Windows Update Manager (System Tray)");
    }
    
    return false; // Invalid mode
}