// Helper to run winget commands with elevation (single UAC prompt)
// Outputs via named pipe for in-memory IPC with parent process
#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
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
    // Load package ID->Name map from file
    std::unordered_map<std::wstring, std::wstring> packageNameMap;
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        std::wstring mapFile = exeDir + L"\\wup_pkg_names.txt";
        std::wifstream ifs(mapFile.c_str());
        if (ifs) {
            std::wstring line;
            while (std::getline(ifs, line)) {
                size_t sep = line.find(L'|');
                if (sep != std::wstring::npos) {
                    std::wstring id = line.substr(0, sep);
                    std::wstring name = line.substr(sep + 1);
                    packageNameMap[id] = name;
                }
            }
        }
    }
    
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
    
    // Store packageId, appName, exitCode
    std::vector<std::tuple<std::wstring, std::wstring, DWORD>> results;

    for (size_t i = 0; i < packageIds.size(); i++) {
        std::wstring currentAppName = L""; // Track app name from "Found" lines
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
        
        // Read and forward output to pipe with non-blocking check
        char buffer[4096];
        DWORD bytesRead;
        DWORD totalBytesAvail;
        
        // Keep reading until process ends AND no more data available
        bool processRunning = true;
        while (processRunning) {
            // Check if data is available without blocking
            if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &totalBytesAvail, NULL) && totalBytesAvail > 0) {
                if (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    // Convert UTF-8 to wide and write to pipe
                    int needed = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                    if (needed > 0) {
                        std::wstring wbuffer(needed, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &wbuffer[0], needed);
                        
                        // Parse app name from "Found AppName [PackageID]" lines
                        if (currentAppName.empty()) {
                            size_t foundPos = wbuffer.find(L"Found ");
                            if (foundPos != std::wstring::npos) {
                                size_t nameStart = foundPos + 6;
                                size_t bracketPos = wbuffer.find(L"[", nameStart);
                                if (bracketPos != std::wstring::npos) {
                                    currentAppName = wbuffer.substr(nameStart, bracketPos - nameStart);
                                    // Trim leading whitespace
                                    while (!currentAppName.empty() && iswspace(currentAppName.front())) {
                                        currentAppName.erase(0, 1);
                                    }
                                    // Trim trailing whitespace
                                    while (!currentAppName.empty() && iswspace(currentAppName.back())) {
                                        currentAppName.pop_back();
                                    }
                                }
                            }
                        }
                        
                        WriteToPipe(hPipe, wbuffer);
                    }
                }
            }
            
            // Check if process is still running
            DWORD waitResult = WaitForSingleObject(pi.hProcess, 100); // 100ms timeout
            if (waitResult == WAIT_OBJECT_0) {
                processRunning = false;
                // Process ended, do one final read to get any remaining data
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
            // If still running and no data, sleep briefly to avoid busy-waiting
            if (processRunning && totalBytesAvail == 0) {
                Sleep(50);
            }
        }
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);
        
        // Store result for summary
        if (currentAppName.empty()) {
            currentAppName = packageIds[i]; // Fallback to ID if name not found
        }
        
        // Try to get display name from map first
        std::wstring displayName;
        if (packageNameMap.count(packageIds[i])) {
            displayName = packageNameMap[packageIds[i]];
        } else if (!currentAppName.empty()) {
            displayName = currentAppName;
        } else {
            displayName = packageIds[i];
        }
        
        // Trim any leading/trailing whitespace before storing
        while (!displayName.empty() && iswspace(displayName.front())) {
            displayName.erase(0, 1);
        }
        while (!displayName.empty() && iswspace(displayName.back())) {
            displayName.pop_back();
        }
        results.push_back({packageIds[i], displayName, exitCode});
        
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
            for (const auto& [pkgId, appName, exitCode] : results) {
                if (exitCode == WingetErrors::SUCCESS) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L"\r\n");
                }
            }
        }
        
        if (skipCount > 0) {
            WriteToPipe(hPipe, L"\r\nSkipped:\r\n");
            for (const auto& [pkgId, appName, exitCode] : results) {
                if (WingetErrors::IsSkipped(exitCode)) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L" (" + WingetErrors::GetStatusText(exitCode).substr(3) + L")\r\n");
                }
            }
        }
        
        if (warningCount > 0) {
            WriteToPipe(hPipe, L"\r\nCancelled:\r\n");
            for (const auto& [pkgId, appName, exitCode] : results) {
                if (exitCode == WingetErrors::INSTALL_CANCELLED_BY_USER || 
                    exitCode == WingetErrors::WINDOWS_ERROR_CANCELLED) {
                    WriteToPipe(hPipe, L"  ⚠️ " + pkgId + L" (cancelled by user or by the installer itself)\r\n");
                }
            }
        }
        
        if (failCount > 0) {
            WriteToPipe(hPipe, L"\r\nFailed:\r\n");
            for (const auto& [pkgId, appName, exitCode] : results) {
                if (WingetErrors::IsFailure(exitCode)) {
                    WriteToPipe(hPipe, L"  • " + pkgId + L" (" + WingetErrors::GetStatusText(exitCode).substr(3) + L")\r\n");
                }
            }
        }
    }
    
    WriteToPipe(hPipe, L"\r\n" + std::to_wstring(results.size()) + L" package(s) processed.\r\n");
    
    // Detailed error summary at the very end (for ALL non-success results)
    if (!results.empty() && (skipCount > 0 || warningCount > 0 || failCount > 0)) {
        WriteToPipe(hPipe, L"\r\n========================================\r\n");
        WriteToPipe(hPipe, L"\r\nErrors:\r\n");
        
        for (const auto& [pkgId, appName, exitCode] : results) {
            // Include skipped, cancelled, and failed packages
            if (exitCode != WingetErrors::SUCCESS) {
                // Trim any leading/trailing whitespace from app name
                std::wstring trimmedAppName = appName;
                while (!trimmedAppName.empty() && iswspace(trimmedAppName.front())) {
                    trimmedAppName.erase(0, 1);
                }
                while (!trimmedAppName.empty() && iswspace(trimmedAppName.back())) {
                    trimmedAppName.pop_back();
                }
                
                // Add blank line before each entry, then app name with ID (will be colored by UI)
                WriteToPipe(hPipe, L"\r\n" + trimmedAppName + L" -- " + pkgId + L":\r\n");
                
                // Error number (hex and decimal)
                wchar_t codeHex[32];
                swprintf(codeHex, 32, L"0x%08X", exitCode);
                int signedCode = (int)exitCode;
                WriteToPipe(hPipe, L"Error number: " + std::wstring(codeHex) + L" (" + std::to_wstring(signedCode) + L")\r\n");
                
                // Reason
                std::wstring reason;
                if (WingetErrors::IsSkipped(exitCode)) {
                    if (exitCode == WingetErrors::UPDATE_NOT_APPLICABLE) {
                        reason = L"No applicable upgrade - package version doesn't match system requirements";
                    } else if (exitCode == WingetErrors::NO_APPLICABLE_INSTALLER) {
                        reason = L"No compatible installer available for your system configuration";
                    } else if (exitCode == WingetErrors::PACKAGE_ALREADY_INSTALLED) {
                        reason = L"Package is already up to date";
                    } else {
                        reason = L"Package skipped - not applicable";
                    }
                } else if (exitCode == WingetErrors::INSTALL_CANCELLED_BY_USER || 
                           exitCode == WingetErrors::WINDOWS_ERROR_CANCELLED) {
                    reason = L"Installation cancelled by user or installer";
                } else if (exitCode == WingetErrors::DOWNLOAD_FAILED) {
                    reason = L"Download failed due to network or server issues";
                } else if (exitCode == WingetErrors::NO_APPLICATIONS_FOUND) {
                    reason = L"Package not found in any configured source";
                } else if (exitCode == WingetErrors::TIMEOUT) {
                    reason = L"Installation timed out";
                } else {
                    reason = L"Installation failed with unknown error";
                }
                WriteToPipe(hPipe, L"Reason: " + reason + L"\r\n");
                
                // Recommendation
                std::wstring recommendation;
                if (WingetErrors::IsSkipped(exitCode)) {
                    recommendation = L"Skip this package and wait for the next version";
                } else if (exitCode == WingetErrors::INSTALL_CANCELLED_BY_USER || 
                           exitCode == WingetErrors::WINDOWS_ERROR_CANCELLED) {
                    recommendation = L"Exclude this package if you want to stop update prompts";
                } else if (exitCode == WingetErrors::DOWNLOAD_FAILED) {
                    recommendation = L"Retry the installation when network conditions improve";
                } else if (exitCode == WingetErrors::NO_APPLICATIONS_FOUND) {
                    recommendation = L"Exclude this package - it's no longer available";
                } else if (exitCode == WingetErrors::TIMEOUT) {
                    recommendation = L"Check Task Manager for stuck installer processes and try again";
                } else {
                    recommendation = L"Search online for this error code to find specific information";
                }
                WriteToPipe(hPipe, L"Recommendation: " + recommendation + L"\r\n");
                WriteToPipe(hPipe, L"----------------------------------------\r\n\r\n");
            }
        }
    }
    
    CloseHandle(hPipe);
    
    return (failCount > 0) ? 1 : 0;
}
