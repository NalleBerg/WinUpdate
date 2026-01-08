// Simple helper to run winget commands with elevation (single UAC prompt)
// Outputs all winget output to a file specified as first argument
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::wcerr << L"Usage: winget_helper.exe <output_file> <package_id1> [package_id2] ..." << std::endl;
        return 1;
    }

    // First argument is the output file
    std::wstring outputFile = argv[1];
    
    // Collect all package IDs from remaining arguments
    std::vector<std::wstring> packageIds;
    for (int i = 2; i < argc; i++) {
        packageIds.push_back(argv[i]);
    }

    // Open output file for writing
    std::wofstream outFile(outputFile.c_str(), std::ios::out | std::ios::trunc);
    if (!outFile) {
        std::wcerr << L"Failed to open output file: " << outputFile << std::endl;
        return 1;
    }

    outFile << L"WinUpdate Helper - Installing " << packageIds.size() << L" package(s)" << std::endl;
    outFile << L"========================================" << std::endl << std::endl;
    outFile.flush();

    int successCount = 0;
    int failCount = 0;

    for (size_t i = 0; i < packageIds.size(); i++) {
        outFile << L"[" << (i+1) << L"/" << packageIds.size() << L"] " << packageIds[i] << std::endl;
        outFile.flush();
        
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
            outFile << L"Failed to create pipe" << std::endl;
            outFile.flush();
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
            outFile << L"Failed to start winget" << std::endl;
            outFile.flush();
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);
            failCount++;
            continue;
        }
        
        CloseHandle(hWritePipe);
        
        // Read and forward output to file
        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            // Convert UTF-8 to wide and write to file
            int needed = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
            if (needed > 0) {
                std::wstring wbuffer(needed, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &wbuffer[0], needed);
                outFile << wbuffer;
                outFile.flush();
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
            outFile << L"✓ Success" << std::endl;
        } else {
            failCount++;
            outFile << L"✗ Failed (exit code: " << exitCode << L")" << std::endl;
        }
        outFile << std::endl;
        outFile.flush();
    }

    outFile << L"========================================" << std::endl;
    outFile << L"=== Installation Complete ===" << std::endl;
    outFile << successCount << L" package(s) processed." << std::endl;
    outFile.flush();
    outFile.close();
    
    return (failCount > 0) ? 1 : 0;
}
