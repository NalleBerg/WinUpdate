// Helper to run winget commands with elevation (single UAC prompt)
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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Parse command line manually (first arg is pipe name, rest are package IDs)
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argc < 3) {
        // Not enough arguments
        return 1;
    }
    
    // First argument is the pipe name
    std::wstring pipeName = argv[1];
    
    // Collect all package IDs from remaining arguments
    std::vector<std::wstring> packageIds;
    for (int i = 2; i < argc; i++) {
        packageIds.push_back(argv[i]);
    }
    
    LocalFree(argv);
    
    // Connect to the named pipe created by parent process
    HANDLE hPipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    WriteToPipe(hPipe, L"WinUpdate Helper - Installing " + std::to_wstring(packageIds.size()) + L" package(s)\r\n");
    WriteToPipe(hPipe, L"========================================\r\n\r\n");

    int successCount = 0;
    int failCount = 0;

    for (size_t i = 0; i < packageIds.size(); i++) {
        WriteToPipe(hPipe, L"[" + std::to_wstring(i+1) + L"/" + std::to_wstring(packageIds.size()) + L"] " + packageIds[i] + L"\r\n");
        
        // Build winget command
        std::wstring cmd = L"winget.exe upgrade --id \"" + packageIds[i] + 
                          L"\" --accept-package-agreements --accept-source-agreements";
        
        // Run winget and capture output
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;
        
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            WriteToPipe(hPipe, L"Failed to create pipe\r\n");
            failCount++;
            continue;
        }
        
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
        
        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        
        PROCESS_INFORMATION pi{};
        
        // Use CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP to hide all console windows
        DWORD creationFlags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
        
        if (!CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, TRUE, creationFlags, NULL, NULL, &si, &pi)) {
            WriteToPipe(hPipe, L"Failed to start winget\r\n");
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);
            failCount++;
            continue;
        }
        
        CloseHandle(hWritePipe);
        
        // Read and forward output to pipe
        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            // Convert UTF-8 to wide and write to pipe
            int needed = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
            if (needed > 0) {
                std::wstring wbuffer(needed, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &wbuffer[0], needed);
                WriteToPipe(hPipe, wbuffer);
            }
        }
        
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        
        if (exitCode == 0) {
            successCount++;
            WriteToPipe(hPipe, L"✓ Success\r\n");
        } else {
            failCount++;
            WriteToPipe(hPipe, L"✗ Failed (exit code: " + std::to_wstring(exitCode) + L")\r\n");
        }
        WriteToPipe(hPipe, L"\r\n");
    }

    WriteToPipe(hPipe, L"========================================\r\n");
    WriteToPipe(hPipe, L"=== Installation Complete ===\r\n");
    WriteToPipe(hPipe, std::to_wstring(successCount) + L" package(s) processed.\r\n");
    
    CloseHandle(hPipe);
    
    return (failCount > 0) ? 1 : 0;
}
