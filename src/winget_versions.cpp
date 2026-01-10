#include "winget_versions.h"
#include "winget_errors.h"
#include "parsing.h"
#include <windows.h>
#include <regex>
#include <sstream>
#include <utility>
#include <set>
#include <fstream>
#include <iostream>
#include <algorithm>

// local helper: capture output of a command via cmd /C (redirects stderr)
static std::string WideToUtf8_local(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::pair<int,std::string> RunProcessCaptureExitCodeLocal(const std::wstring &cmd, int timeoutMs = 8000) {
    std::pair<int,std::string> result = {-1, std::string()};
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return result;
    // ensure read handle is not inherited
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = NULL;

    PROCESS_INFORMATION pi{};
    std::wstring cmdCopy = cmd;
    BOOL ok = CreateProcessW(NULL, &cmdCopy[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    // close write end in parent immediately
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return result;
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.first = (int)WingetErrors::TIMEOUT;
    } else {
        DWORD exitCode = 0; 
        GetExitCodeProcess(pi.hProcess, &exitCode); 
        result.first = (int)exitCode;
        
        // Log non-standard exit codes for diagnostics
        if (exitCode != WingetErrors::SUCCESS && 
            exitCode != WingetErrors::UPDATE_NOT_APPLICABLE &&
            exitCode != WingetErrors::PACKAGE_ALREADY_INSTALLED) {
            // Only log if it's a true error (not just "no updates")
            if (WingetErrors::IsFailure(exitCode)) {
                // Could add logging here if needed for version query failures
            }
        }
    }

    // read all available output from pipe
    std::string output;
    const DWORD bufSize = 4096;
    char buffer[bufSize];
    DWORD read = 0;
    while (ReadFile(hRead, buffer, bufSize, &read, NULL) && read > 0) {
        output.append(buffer, buffer + read);
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    result.second = output;
    return result;
}

// Note: these implementations intentionally avoid depending on file-static
// globals from main.cpp (like g_packages). They perform self-contained
// parsing of winget output and favor JSON extraction when available.

static inline std::string trim_copy(std::string s) {
    while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back();
    while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static inline std::string normalize_id(std::string s) {
    // remove control chars, trim, strip surrounding quotes and trailing punctuation
    std::string out;
    for (char c : s) if ((unsigned char)c >= 32) out.push_back(c);
    // trim
    while(!out.empty() && isspace((unsigned char)out.front())) out.erase(out.begin());
    while(!out.empty() && isspace((unsigned char)out.back())) out.pop_back();
    // strip surrounding quotes
    if (out.size() >= 2 && ((out.front()=='"' && out.back()=='"') || (out.front()=='\'' && out.back()=='\''))) {
        out = out.substr(1, out.size()-2);
    }
    // remove trailing punctuation that sometimes appears in noisy output
    while(!out.empty() && (out.back()==',' || out.back()=='.' || out.back()=='"')) out.pop_back();
    // final trim of any residual whitespace
    while(!out.empty() && isspace((unsigned char)out.front())) out.erase(out.begin());
    while(!out.empty() && isspace((unsigned char)out.back())) out.pop_back();
    return out;
}

std::vector<std::pair<std::string,std::string>> ParseRawWingetTextInMemory(const std::string &text) {
    std::set<std::pair<std::string,std::string>> found;
    if (text.empty()) return {};
    ParseUpgradeFast(text, found);
    if (found.empty()) ExtractUpdatesFromText(text, found);
    std::vector<std::pair<std::string,std::string>> out;
    for (auto &p : found) out.emplace_back(p.first, p.second);
    return out;
}

std::unordered_map<std::string,std::string> MapInstalledVersions() {
    std::unordered_map<std::string,std::string> out;
    try {
        // Fast approach: winget upgrade contains both installed and available versions
        auto r = RunProcessCaptureExitCodeLocal(L"cmd.exe /C winget upgrade");
        std::string txt = r.second;
        
        // Simple table parsing with right-to-left tokenization
        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) lines.push_back(trim_copy(ln));
        
        // Find header line with "Available" column
        int headerIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("Available") != std::string::npos) {
                headerIdx = i;
                break;
            }
        }
        
        if (headerIdx >= 0) {
            // Parse lines after separator (----)
            bool foundSeparator = false;
            for (int i = headerIdx + 1; i < (int)lines.size(); ++i) {
                const std::string &sline = lines[i];
                
                if (sline.find("----") != std::string::npos) {
                    foundSeparator = true;
                    continue;
                }
                
                if (!foundSeparator) continue;
                if (sline.empty()) continue;
                if (sline.find("upgrade") != std::string::npos) break;
                
                // Right-to-left tokenization: Source, Available, Version, Id, Name(rest)
                std::istringstream ls(sline);
                std::vector<std::string> toks;
                std::string tok;
                while (ls >> tok) toks.push_back(tok);
                
                if (toks.size() >= 4) {
                    int n = (int)toks.size();
                    std::string id = trim_copy(toks[n-4]);      // 4th from end = Id
                    std::string inst = trim_copy(toks[n-3]);    // 3rd from end = Version (installed)
                    id = normalize_id(id);
                    if (!id.empty() && !inst.empty()) out[id] = inst;
                }
            }
            return out;
        }
    } catch(...) {}
    return out;
}

std::unordered_map<std::string,std::string> MapAvailableVersions() {
    std::unordered_map<std::string,std::string> out;
    try {
        // Fast approach: use same winget upgrade output
        auto r = RunProcessCaptureExitCodeLocal(L"cmd.exe /C winget upgrade");
        std::string txt = r.second;
        
        // Simple table parsing with right-to-left tokenization
        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) lines.push_back(trim_copy(ln));
        
        // Find header line with "Available" column
        int headerIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("Available") != std::string::npos) {
                headerIdx = i;
                break;
            }
        }
        
        if (headerIdx >= 0) {
            // Parse lines after separator (----)
            bool foundSeparator = false;
            for (int i = headerIdx + 1; i < (int)lines.size(); ++i) {
                const std::string &sline = lines[i];
                
                if (sline.find("----") != std::string::npos) {
                    foundSeparator = true;
                    continue;
                }
                
                if (!foundSeparator) continue;
                if (sline.empty()) continue;
                if (sline.find("upgrade") != std::string::npos) break;
                
                // Right-to-left tokenization: Source, Available, Version, Id, Name(rest)
                std::istringstream ls(sline);
                std::vector<std::string> toks;
                std::string tok;
                while (ls >> tok) toks.push_back(tok);
                
                if (toks.size() >= 4) {
                    int n = (int)toks.size();
                    std::string id = trim_copy(toks[n-4]);         // 4th from end = Id
                    std::string available = trim_copy(toks[n-2]);  // 2nd from end = Available
                    id = normalize_id(id);
                    if (!id.empty() && !available.empty()) out[id] = available;
                }
            }
            return out;
        }
    } catch(...) {}
    return out;
}

// thin exported wrappers
std::unordered_map<std::string,std::string> MapInstalledVersions_ext() {
    return MapInstalledVersions();
}

std::unordered_map<std::string,std::string> MapAvailableVersions_ext() {
    return MapAvailableVersions();
}
