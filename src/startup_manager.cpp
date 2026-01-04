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
        
        // Set arguments to --hidden
        pShellLink->SetArguments(L"--hidden");
        
        // Set working directory
        pShellLink->SetWorkingDirectory(workingDir);
        
        // Set description
        pShellLink->SetDescription(L"WinUpdate - Windows Update Manager (Hidden Scan)");
        
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
