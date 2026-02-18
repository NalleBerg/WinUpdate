// Helper to run winget commands with elevation (single UAC prompt)
// Supports: install, reinstall, uninstall
// Outputs via named pipe for in-memory IPC with parent process
#include <windows.h>
#include <string>
#include <vector>

// Write to pipe helper
void WriteToPipe(HANDLE hPipe, const std::wstring& text) {
    if (!hPipe || hPipe == INVALID_HANDLE_VALUE) return;
    
    // Convert wide to UTF-8
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return;
    
    std::string utf8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], needed, NULL, NULL);
    
    DWORD written;
    WriteFile(hPipe, utf8.c_str(), (DWORD)utf8.length(), &written, NULL);
}

// Run a single winget command and stream output to pipe
bool RunWingetCommand(HANDLE hPipe, const std::wstring& cmd, std::wstring& appName) {
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        WriteToPipe(hPipe, L"Failed to create pipe\r\n");
        return false;
    }
    
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi{};
    
    DWORD creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    
    if (!CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, TRUE, creationFlags, NULL, NULL, &si, &pi)) {
        WriteToPipe(hPipe, L"Failed to start winget\r\n");
        CloseHandle(hWritePipe);
        CloseHandle(hReadPipe);
        return false;
    }
    
    CloseHandle(hWritePipe);
    
    // Read and forward output to pipe
    char buffer[4096];
    DWORD bytesRead;
    DWORD totalBytesAvail;
    
    bool processRunning = true;
    while (processRunning) {
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &totalBytesAvail, NULL) && totalBytesAvail > 0) {
            if (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                int needed = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                if (needed > 0) {
                    std::wstring wbuffer(needed, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &wbuffer[0], needed);
                    
                    // Parse app name from "Found AppName [PackageID]" lines
                    if (appName.empty()) {
                        size_t foundPos = wbuffer.find(L"Found ");
                        if (foundPos != std::wstring::npos) {
                            size_t nameStart = foundPos + 6;
                            size_t bracketPos = wbuffer.find(L"[", nameStart);
                            if (bracketPos != std::wstring::npos) {
                                appName = wbuffer.substr(nameStart, bracketPos - nameStart);
                                while (!appName.empty() && iswspace(appName.front())) appName.erase(0, 1);
                                while (!appName.empty() && iswspace(appName.back())) appName.pop_back();
                            }
                        }
                    }
                    
                    WriteToPipe(hPipe, wbuffer);
                }
            }
        }
        
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 100);
        if (waitResult == WAIT_OBJECT_0) {
            processRunning = false;
            // Final read
            while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &totalBytesAvail, NULL) && totalBytesAvail > 0) {
                if (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    int needed = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                    if (needed > 0) {
                        std::wstring wbuffer(needed, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &wbuffer[0], needed);
                        WriteToPipe(hPipe, wbuffer);
                    }
                } else {
                    break;
                }
            }
        } else if (waitResult == WAIT_FAILED) {
            processRunning = false;
        }
        if (processRunning && totalBytesAvail == 0) {
            Sleep(50);
        }
    }
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);
    
    return (exitCode == 0);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Parse: helper.exe <pipe_name> <operation> <pkg1> <pkg2> ...
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argc < 4) {
        return 1;  // Need: program, pipe, operation, at least one package
    }
    
    std::wstring pipeName = argv[1];
    std::wstring operation = argv[2];  // "install", "reinstall", or "uninstall"
    
    // Collect package IDs
    std::vector<std::wstring> packageIds;
    for (int i = 3; i < argc; i++) {
        packageIds.push_back(argv[i]);
    }
    
    LocalFree(argv);
    
    // Connect to pipe
    HANDLE hPipe = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Determine operation title
    std::wstring operationTitle;
    if (operation == L"install") {
        operationTitle = L"Installing";
    } else if (operation == L"reinstall") {
        operationTitle = L"Reinstalling";
    } else if (operation == L"uninstall") {
        operationTitle = L"Uninstalling";
    } else {
        WriteToPipe(hPipe, L"Unknown operation: " + operation + L"\r\n");
        CloseHandle(hPipe);
        return 1;
    }
    
    int successCount = 0;
    int failCount = 0;

    for (size_t i = 0; i < packageIds.size(); i++) {
        std::wstring appName;
        
        bool success = true;
        
        if (operation == L"reinstall") {
            // Uninstall first
            WriteToPipe(hPipe, L"\r\nStep 1: Uninstalling...\r\n");
            std::wstring uninstallCmd = L"winget.exe uninstall --id \"" + packageIds[i] + 
                                       L"\" --silent --accept-source-agreements";
            bool uninstallSuccess = RunWingetCommand(hPipe, uninstallCmd, appName);
            
            if (uninstallSuccess) {
                WriteToPipe(hPipe, L"\r\nStep 2: Installing...\r\n");
                std::wstring installCmd = L"winget.exe install --id \"" + packageIds[i] + 
                                         L"\" --exact --silent --accept-source-agreements --accept-package-agreements";
                success = RunWingetCommand(hPipe, installCmd, appName);
            } else {
                WriteToPipe(hPipe, L"\r\n❌ Uninstall failed, skipping install\r\n");
                success = false;
            }
        } else if (operation == L"uninstall") {
            std::wstring cmd = L"winget.exe uninstall --id \"" + packageIds[i] + 
                              L"\" --silent --accept-source-agreements";
            success = RunWingetCommand(hPipe, cmd, appName);
        } else {  // install
            std::wstring cmd = L"winget.exe install --id \"" + packageIds[i] + 
                              L"\" --exact --silent --accept-source-agreements --accept-package-agreements";
            success = RunWingetCommand(hPipe, cmd, appName);
        }
        
        if (success) {
            WriteToPipe(hPipe, L"\r\n✅ Success\r\n\r\n");
            successCount++;
        } else {
            WriteToPipe(hPipe, L"\r\n❌ Failed\r\n\r\n");
            failCount++;
        }
    }

    WriteToPipe(hPipe, L"========================================\r\n");
    WriteToPipe(hPipe, L"=== " + operationTitle + L" Complete ===\r\n");
    
    if (successCount > 0) {
        WriteToPipe(hPipe, L"✅ " + std::to_wstring(successCount) + L" package(s) succeeded\r\n");
    }
    if (failCount > 0) {
        WriteToPipe(hPipe, L"❌ " + std::to_wstring(failCount) + L" package(s) failed\r\n");
    }
    
    WriteToPipe(hPipe, L"\r\n" + std::to_wstring(packageIds.size()) + L" package(s) processed.\r\n");
    
    CloseHandle(hPipe);
    
    return (failCount > 0) ? 1 : 0;
}
