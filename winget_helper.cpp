// Simple helper to run winget commands with elevation (single UAC prompt)
// Outputs all winget output to stdout for parent process to capture via pipe
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcerr << L"Usage: winget_helper.exe <package_id1> [package_id2] ..." << std::endl;
        return 1;
    }

    // Collect all package IDs from arguments
    std::vector<std::wstring> packageIds;
    for (int i = 1; i < argc; i++) {
        packageIds.push_back(argv[i]);
    }

    std::wcout << L"WinUpdate Helper - Installing " << packageIds.size() << L" package(s)" << std::endl;
    std::wcout << L"========================================" << std::endl << std::endl;

    int successCount = 0;
    int failCount = 0;

    for (size_t i = 0; i < packageIds.size(); i++) {
        std::wcout << L"[" << (i+1) << L"/" << packageIds.size() << L"] " << packageIds[i] << std::endl;
        
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
            std::wcerr << L"Failed to create pipe" << std::endl;
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
        
        if (!CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            std::wcerr << L"Failed to start winget" << std::endl;
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);
            failCount++;
            continue;
        }
        
        CloseHandle(hWritePipe);
        
        // Read and forward output
        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::cout << buffer;
            std::cout.flush();
        }
        
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        
        if (exitCode == 0) {
            successCount++;
            std::wcout << L"✓ Success" << std::endl;
        } else {
            failCount++;
            std::wcout << L"✗ Failed (exit code: " << exitCode << L")" << std::endl;
        }
        
        std::wcout << std::endl;
    }

    std::wcout << L"========================================" << std::endl;
    std::wcout << L"Summary: " << successCount << L" succeeded, " << failCount << L" failed" << std::endl;
    
    return (failCount > 0) ? 1 : 0;
}
