#include "hidden_scan.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <vector>

// Load skip configuration from %APPDATA%\WinUpdate\wup_settings.ini
static std::unordered_map<std::string, std::string> LoadSkipConfig() {
    std::unordered_map<std::string, std::string> skipped;
    try {
        wchar_t appData[MAX_PATH];
        if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            return skipped;
        }
        
        std::wstring settingsPath = std::wstring(appData) + L"\\WinUpdate\\wup_settings.ini";
        std::ifstream ifs(std::string(settingsPath.begin(), settingsPath.end()));
        if (!ifs) return skipped;
        
        std::string ln;
        bool inSkipped = false;
        
        while (std::getline(ifs, ln)) {
            // trim line endings
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();
            
            // Check for section headers
            if (!ln.empty() && ln[0] == '[') {
                inSkipped = (ln == "[skipped]");
                continue;
            }
            
            // If we're in the [skipped] section, parse the entries
            if (inSkipped && !ln.empty()) {
                // Format is: "PackageId  Version" (whitespace-separated)
                // Trim leading/trailing whitespace
                auto ltrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); };
                auto rtrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
                ltrim(ln); rtrim(ln);
                
                // Split on whitespace
                size_t spacePos = ln.find_first_of(" \t");
                if (spacePos != std::string::npos) {
                    std::string id = ln.substr(0, spacePos);
                    std::string ver = ln.substr(spacePos);
                    ltrim(ver); rtrim(ver);
                    if (!id.empty() && !ver.empty()) {
                        skipped[id] = ver;
                    }
                }
            }
        }
    } catch(...) {}
    return skipped;
}

// Helper to run a process and capture output
static std::string RunWingetUpgrade(int timeoutMs) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return std::string();
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = { };
    std::wstring cmd = L"cmd /C winget upgrade";
    
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return std::string();
    }
    
    CloseHandle(hWritePipe);
    
    // Wait for process with timeout
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }
    
    // Read output
    std::string output;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return output;
}

// Parse output to extract package IDs and check if non-skipped updates exist
static bool HasNonSkippedUpdates(const std::string &output, const std::unordered_map<std::string, std::string> &skipped) {
    if (output.empty()) return false;
    
    // Look for "no updates" indicators
    if (output.find("No applicable update found") != std::string::npos) {
        return false;
    }
    if (output.find("No installed package found") != std::string::npos) {
        return false;
    }
    if (output.find("No package found matching input criteria") != std::string::npos) {
        return false;
    }
    
    // The actual upgrade table has a header line with "Name", "Id", "Version", "Available"
    // followed by a separator line with "----"
    // We need to find BOTH to ensure we're looking at the upgrade table
    
    size_t sepPos = output.find("----");
    if (sepPos == std::string::npos) {
        // No separator line = no upgrade table = no updates
        try {
            wchar_t appData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
                std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
                if (log) {
                    log << "No separator line found - no upgrade table" << std::endl;
                }
            }
        } catch(...) {}
        return false;
    }
    
    // Find the header line (should be just before the separator, skip empty lines)
    size_t headerEnd = sepPos;
    while (headerEnd > 0 && (output[headerEnd - 1] == '\n' || output[headerEnd - 1] == '\r' || output[headerEnd - 1] == ' ')) {
        headerEnd--;
    }
    
    size_t headerStart = output.rfind('\n', headerEnd);
    if (headerStart == std::string::npos) headerStart = 0;
    else headerStart++;
    
    std::string headerLine = output.substr(headerStart, headerEnd - headerStart);
    
    // Debug log header line
    try {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
            std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
            if (log) {
                log << "Header line: [" << headerLine << "]" << std::endl;
            }
        }
    } catch(...) {}
    
    // Upgrade table MUST have "Available" column (list table only has "Id")
    if (headerLine.find("Available") == std::string::npos) {
        // This is not an upgrade table, probably a list table
        try {
            wchar_t appData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
                std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
                if (log) {
                    log << "No 'Available' column found - not an upgrade table" << std::endl;
                }
            }
        } catch(...) {}
        return false;
    }
    
    // Now parse the actual upgrade table lines
    // Parse from right to left since only package name can have spaces
    // Format: Name | Id | Version | Available | Source
    // From right: Source(1) Available(2) Version(3) Id(4) Name(rest)
    
    std::istringstream iss(output.substr(sepPos));
    std::string line;
    
    // Skip the separator line itself
    std::getline(iss, line);
    
    while (std::getline(iss, line)) {
        // Trim line endings
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Stop at "X upgrades available" line
        if (line.find("upgrade") != std::string::npos && line.find("available") != std::string::npos) {
            break;
        }
        
        // Split line into tokens
        std::istringstream lineStream(line);
        std::string token;
        std::vector<std::string> tokens;
        
        while (lineStream >> token) {
            tokens.push_back(token);
        }
        
        // Need at least 4 tokens: Id, Version, Available, Source
        if (tokens.size() < 4) continue;
        
        // Id is the 4th token from the end
        std::string packageId = tokens[tokens.size() - 4];
        
        // Trim whitespace
        auto ltrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); };
        auto rtrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
        ltrim(packageId); rtrim(packageId);
        
        if (packageId.empty()) continue;
        
        // Debug log
        try {
            wchar_t appData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
                std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
                std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
                if (log) {
                    log << "Found package ID: " << packageId << std::endl;
                    if (skipped.find(packageId) != skipped.end()) {
                        log << "  -> SKIPPED" << std::endl;
                    } else {
                        log << "  -> NOT SKIPPED (will show UI)" << std::endl;
                    }
                }
            }
        } catch(...) {}
        
        // Check if it's NOT in the skipped list
        if (skipped.find(packageId) == skipped.end()) {
            // Found a non-skipped package!
            return true;
        }
    }
    
    return false;
}

bool PerformHiddenScan() {
    // Load skip configuration from settings file
    auto skipped = LoadSkipConfig();
    
    // Debug: Write to log file
    try {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
            std::ofstream log(std::string(logPath.begin(), logPath.end()));
            if (log) {
                log << "=== Hidden Scan Debug ===" << std::endl;
                log << "Skipped packages (" << skipped.size() << "):" << std::endl;
                for (const auto &p : skipped) {
                    log << "  " << p.first << " -> " << p.second << std::endl;
                }
                log << std::endl;
                log << "About to run winget upgrade..." << std::endl;
                log.flush();
            }
        }
    } catch(...) {}
    
    // Run winget upgrade to check for updates
    // Use 110s timeout to match GUI scanner - winget can take 50-60+ seconds with msstore source
    std::string output = RunWingetUpgrade(110000);
    
    // Debug: Write winget output length first
    try {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
            std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
            if (log) {
                log << "Winget output length: " << output.size() << " bytes" << std::endl;
            }
        }
    } catch(...) {}
    
    // Debug: Write winget output
    try {
        wchar_t appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
            std::wstring logPath = std::wstring(appData) + L"\\WinUpdate\\hidden_scan_debug.txt";
            std::ofstream log(std::string(logPath.begin(), logPath.end()), std::ios::app);
            if (log) {
                log << "Winget output:" << std::endl;
                log << output << std::endl;
                log << std::endl;
            }
        }
    } catch(...) {}
    
    if (!HasNonSkippedUpdates(output, skipped)) {
        // No non-skipped updates available - don't show UI
        return false;
    }
    
    // Non-skipped updates found! Show the main window
    // Get the executable path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    // Launch without --hidden parameter to show UI
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    
    return false;
}
