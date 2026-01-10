// Helper to run winget commands with elevation (single UAC prompt)
// Outputs via named pipe for in-memory IPC with parent process
#include <windows.h>
#include <string>
#include <vector>
#include "src/winget_errors.h"

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
    int skipCount = 0;
    int warningCount = 0;
    
    std::vector<std::pair<std::wstring, DWORD>> results; // packageId, exitCode

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
        
        // Use CREATE_NO_WINDOW | DETACHED_PROCESS to completely hide console windows
        // DETACHED_PROCESS prevents any console window from appearing, even briefly
        DWORD creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
        
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
        
        // Store result for summary
        results.push_back({packageIds[i], exitCode});
        
        // Categorize result
        if (exitCode == WingetErrors::SUCCESS) {
            successCount++;
        } else if (WingetErrors::IsSkipped(exitCode)) {
            skipCount++;
        } else if (exitCode == WingetErrors::INSTALL_CANCELLED_BY_USER || 
                   exitCode == WingetErrors::WINDOWS_ERROR_CANCELLED) {
            warningCount++;
        } else if (WingetErrors::IsFailure(exitCode)) {
            failCount++;
        }
        
        // Write status and detailed message
        WriteToPipe(hPipe, WingetErrors::GetStatusText(exitCode) + L"\r\n");
        
        // Add detailed error message if not successful
        if (exitCode != WingetErrors::SUCCESS) {
            WriteToPipe(hPipe, WingetErrors::GetErrorMessage(exitCode) + L"\r\n");
        }
        
        WriteToPipe(hPipe, L"\r\n");
    }

    WriteToPipe(hPipe, L"========================================\r\n");
    WriteToPipe(hPipe, L"=== Installation Complete ===\r\n");
    
    // Summary with breakdown by category
    if (successCount > 0) {
        WriteToPipe(hPipe, L"✅ " + std::to_wstring(successCount) + L" package(s) installed successfully\r\n");
    }
    if (skipCount > 0) {
        WriteToPipe(hPipe, L"ℹ️ " + std::to_wstring(skipCount) + L" package(s) skipped (not applicable)\r\n");
    }
    if (warningCount > 0) {
        WriteToPipe(hPipe, L"⚠️ " + std::to_wstring(warningCount) + L" package(s) cancelled by user\r\n");
    }
    if (failCount > 0) {
        WriteToPipe(hPipe, L"❌ " + std::to_wstring(failCount) + L" package(s) failed\r\n");
    }
    
    // Detailed breakdown if there were non-success results
    if (!results.empty() && (skipCount > 0 || warningCount > 0 || failCount > 0)) {
        WriteToPipe(hPipe, L"\r\n");
        
        if (successCount > 0) {
            WriteToPipe(hPipe, L"Successful:\r\n");
            for (const auto& [pkgId, exitCode] : results) {
                if (exitCode == WingetErrors::SUCCESS) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L"\r\n");
                }
            }
        }
        
        if (skipCount > 0) {
            WriteToPipe(hPipe, L"\r\nSkipped:\r\n");
            for (const auto& [pkgId, exitCode] : results) {
                if (WingetErrors::IsSkipped(exitCode)) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L" (" + WingetErrors::GetStatusText(exitCode).substr(3) + L")\r\n");
                }
            }
        }
        
        if (warningCount > 0) {
            WriteToPipe(hPipe, L"\r\nCancelled:\r\n");
            for (const auto& [pkgId, exitCode] : results) {
                if (exitCode == WingetErrors::INSTALL_CANCELLED_BY_USER || 
                    exitCode == WingetErrors::WINDOWS_ERROR_CANCELLED) {
                    WriteToPipe(hPipe, L"  ⚠️ " + pkgId + L" (cancelled by user or by the installer itself)\r\n");
                }
            }
        }
        
        if (failCount > 0) {
            WriteToPipe(hPipe, L"\r\nFailed:\r\n");
            for (const auto& [pkgId, exitCode] : results) {
                if (WingetErrors::IsFailure(exitCode)) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L" (" + WingetErrors::GetStatusText(exitCode).substr(3) + L")\r\n");
                }
            }
        }
    }
    
    CloseHandle(hPipe);
    
    return (failCount > 0) ? 1 : 0;
}
