// Centralized scanner implementation - moved here so all scanning/parsing
// logic lives in one compilation unit as requested.

// Simple scanner: run `winget upgrade` and parse the aligned table by columns.
#include "scan_runner.h"
#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <regex>
#include <filesystem>

static inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)isspace((unsigned char)s[a])) ++a;
    while (b > a && (unsigned char)isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

static std::string WideToUtf8_local(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::pair<int,std::string> RunProcessCaptureExitCodeLocal(const std::wstring &cmd, int timeoutMs = 5000) {
    std::pair<int,std::string> res = {-1, std::string()};
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return res;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite; si.hStdInput = NULL;
    PROCESS_INFORMATION pi{};
    std::wstring cmdCopy = cmd;
    if (!CreateProcessW(NULL, &cmdCopy[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(hWrite); CloseHandle(hRead); return res; }
    CloseHandle(hWrite);
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); res.first = -2; }
    else { DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode); res.first = (int)exitCode; }
    std::string out; const DWORD bufSize = 4096; char buf[bufSize]; DWORD read = 0;
    while (ReadFile(hRead, buf, bufSize, &read, NULL) && read > 0) out.append(buf, buf + read);
    CloseHandle(hRead); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    res.second = out; return res;
}

bool ScanAndPopulateMaps(std::unordered_map<std::string,std::string> &avail, std::unordered_map<std::string,std::string> &inst) {
    avail.clear(); inst.clear();
    try {
        // ensure logs dir exists
        try { std::filesystem::create_directories("logs"); } catch(...) {}
            // Try multiple invocation forms until we get non-empty output.
            std::vector<std::wstring> cmds = {
                L"cmd.exe /C winget upgrade",
                L"cmd.exe /C winget.exe upgrade",
                L"cmd.exe /C powershell -NoProfile -Command \"winget upgrade\"",
                L"cmd.exe /C powershell -NoProfile -Command \"& { winget upgrade }\"",
            };
            std::string rawout;
            std::ofstream logcmds;
            try { std::filesystem::create_directories("logs"); logcmds.open("logs\\run_commands.txt", std::ios::binary | std::ios::app); } catch(...) {}
            for (auto &c : cmds) {
                auto r = RunProcessCaptureExitCodeLocal(c, 12000);
                try { if (logcmds) logcmds << "CMD: " << WideToUtf8_local(c) << " EXIT=" << r.first << " OUTLEN=" << r.second.size() << "\n"; } catch(...) {}
                if (!r.second.empty()) {
                    rawout = r.second;
                    AppendLog(std::string("ScanAndPopulateMaps: captured winget output length=") + std::to_string((int)rawout.size()) + "\n");
                    break;
                }
            }
            try { if (logcmds) logcmds.close(); } catch(...) {}
            if (rawout.empty()) return false;
            std::vector<std::string> lines; std::string line;
            try {
                std::istringstream riss(rawout);
                while (std::getline(riss, line)) lines.push_back(line);
            } catch(...) { return false; }
        int headerIdx = -1, sepIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("----") != std::string::npos) { sepIdx = i; headerIdx = i-1; break; }
        }
        if (headerIdx >= 0 && sepIdx > 0) {
            std::string header = lines[headerIdx];
            size_t namePos = header.find("Name");
            size_t idPos = header.find("Id");
            size_t verPos = header.find("Version");
            size_t availPos = header.find("Available");
            if (namePos != std::string::npos && verPos != std::string::npos && availPos != std::string::npos) {
                // Do not write debug files to workspace; log header info instead
                AppendLog(std::string("ScanAndPopulateMaps: header='") + header + " NAME_POS=" + std::to_string((int)namePos) + " VER_POS=" + std::to_string((int)verPos) + " AVAIL_POS=" + std::to_string((int)availPos) + "\n");
                for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                    std::string s = lines[i];
                    if (s.empty()) continue;
                    if (s.find("upgrades available") != std::string::npos) break;
                    auto safeSub = [&](size_t a, size_t b)->std::string { if (a >= s.size()) return std::string(); size_t end = std::min(b, s.size()); return trim(s.substr(a, end-a)); };
                    std::string name = safeSub(namePos, verPos);
                    std::string version = safeSub(verPos, availPos);
                    std::string available = safeSub(availPos, s.size());
                    // skip writing per-line debug output to workspace
                    if (name.empty()) {
                        // fallback: tokenize the line and attempt to reconstruct fields
                        std::istringstream ls2(s);
                        std::vector<std::string> toks2; std::string tk;
                        while (ls2 >> tk) toks2.push_back(tk);
                        if (toks2.size() >= 4) {
                            int n = (int)toks2.size();
                            // join leading tokens as display name
                            std::string joinedName;
                            for (int j = 0; j < n-4; ++j) { if (!joinedName.empty()) joinedName += ' '; joinedName += toks2[j]; }
                            // if there were no leading tokens (unlikely), fallback to first token
                            if (joinedName.empty()) joinedName = toks2.front();
                            name = joinedName;
                            version = toks2[n-3];
                            available = toks2[n-2];
                        }
                    }
                    if (!name.empty()) {
                        // only accept entries where Available looks like a version (contains a digit)
                        bool avail_has_digit = false;
                        for (char c : available) if (isdigit((unsigned char)c)) { avail_has_digit = true; break; }
                        if (avail_has_digit) {
                            inst[name] = version;
                            avail[name] = available;
                        }
                    }
                    // ---
                }
                if (dbg) dbg.close();
            }
        } else {
            // fallback: try to parse lines with regex: <Name> <Id> <Version> <Available> [Source]
            try {
                std::regex re(R"(^(.+?)\s+([A-Za-z0-9_.-]+)\s+(\S+)\s+(\S+)(?:\s+\S+)?$)");
                std::smatch m;
                for (auto &s : lines) {
                    if (s.empty()) continue;
                    if (s.find("upgrades available") != std::string::npos) break;
                    if (std::regex_match(s, m, re) && m.size() >= 5) {
                        std::string name = trim(m[1].str());
                        std::string version = trim(m[3].str());
                        std::string available = trim(m[4].str());
                        if (!name.empty()) {
                            bool avail_has_digit = false;
                            for (char c : available) if (isdigit((unsigned char)c)) { avail_has_digit = true; break; }
                            if (avail_has_digit) { inst[name] = version; avail[name] = available; }
                        }
                    }
                }
            } catch(...) {}
        }
        // Diagnostic dumps into logs/
            // Do not write diagnostic files to workspace; log summary instead
            try { AppendLog(std::string("ScanAndPopulateMaps: avail count=") + std::to_string((int)avail.size()) + " inst count=" + std::to_string((int)inst.size()) + "\n"); } catch(...) {}
        return true;
    } catch(...) { return false; }
}

bool RunFullScan(std::vector<std::pair<std::string,std::string>> &outResults,
                 std::unordered_map<std::string,std::string> &avail,
                 std::unordered_map<std::string,std::string> &inst,
                 int timeoutMs) {
    outResults.clear(); avail.clear(); inst.clear();
    try {
        // reuse the same invocation attempts as ScanAndPopulateMaps
        std::vector<std::wstring> cmds = {
            L"cmd.exe /C winget upgrade",
            L"cmd.exe /C winget.exe upgrade",
            L"cmd.exe /C powershell -NoProfile -Command \"winget upgrade\"",
            L"cmd.exe /C powershell -NoProfile -Command \"& { winget upgrade }\"",
        };
        std::string rawout;
        for (auto &c : cmds) {
            auto r = RunProcessCaptureExitCodeLocal(c, timeoutMs);
            if (!r.second.empty()) { rawout = r.second; break; }
        }
        if (rawout.empty()) return false;
        // parse avail/inst using existing logic (column/header or regex/tokenize)
        // strip UTF-8 BOM if present and remove stray control chars
        if (rawout.size() >= 3 && (unsigned char)rawout[0] == 0xEF && (unsigned char)rawout[1] == 0xBB && (unsigned char)rawout[2] == 0xBF) rawout.erase(0,3);
        for (char &c : rawout) if (c == '\r' || c == '\t') c = ' ';
        std::vector<std::string> lines; std::istringstream iss(rawout); std::string ln;
        while (std::getline(iss, ln)) {
            // remove any leading/trailing control characters
            size_t a = 0, b = ln.size();
            while (a < b && (unsigned char)ln[a] < 32) ++a;
            while (b > a && (unsigned char)ln[b-1] < 32) --b;
            if (a >= b) lines.push_back(std::string()); else lines.push_back(ln.substr(a, b-a));
        }
        int headerIdx = -1, sepIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) if (lines[i].find("----") != std::string::npos) { sepIdx = i; headerIdx = i-1; break; }
        if (headerIdx >= 0 && sepIdx > 0) {
            std::string header = lines[headerIdx];
            size_t namePos = header.find("Name");
            size_t verPos = header.find("Version");
            size_t availPos = header.find("Available");
            if (namePos != std::string::npos && verPos != std::string::npos && availPos != std::string::npos) {
                for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                    std::string s = lines[i];
                    if (s.empty()) continue;
                    if (s.find("upgrades available") != std::string::npos) break;
                    auto safeSub = [&](size_t a, size_t b)->std::string { if (a >= s.size()) return std::string(); size_t end = std::min(b, s.size()); return trim(s.substr(a, end-a)); };
                    std::string name = safeSub(namePos, verPos);
                    std::string version = safeSub(verPos, availPos);
                    std::string available = safeSub(availPos, s.size());
                    if (name.empty()) {
                        std::istringstream ls2(s); std::vector<std::string> toks2; std::string tk;
                        while (ls2 >> tk) toks2.push_back(tk);
                        if (toks2.size() >= 4) {
                            int n = (int)toks2.size();
                            std::string joinedName;
                            for (int j = 0; j < n-4; ++j) { if (!joinedName.empty()) joinedName += ' '; joinedName += toks2[j]; }
                            if (joinedName.empty()) joinedName = toks2.front();
                            name = joinedName; version = toks2[n-3]; available = toks2[n-2];
                        }
                    }
                    if (!name.empty()) {
                        bool avail_has_digit = false; for (char c : available) if (isdigit((unsigned char)c)) { avail_has_digit = true; break; }
                        if (avail_has_digit) { inst[name] = version; avail[name] = available; outResults.emplace_back(name, name); }
                    }
                }
            }
        }
        // fallback: regex parse lines for name id ver avail and also populate outResults
        if (outResults.empty()) {
            try {
                std::regex re(R"(^(.+?)\s+([A-Za-z0-9_.-]+)\s+(\S+)\s+(\S+)(?:\s+\S+)?$)");
                std::smatch m;
                for (auto &s : lines) {
                    if (s.empty()) continue;
                    if (s.find("upgrades available") != std::string::npos) break;
                    if (std::regex_match(s, m, re) && m.size() >= 5) {
                        std::string name = trim(m[1].str());
                        std::string id = trim(m[2].str());
                        std::string version = trim(m[3].str());
                        std::string available = trim(m[4].str());
                        bool avail_has_digit = false; for (char c : available) if (isdigit((unsigned char)c)) { avail_has_digit = true; break; }
                        if (avail_has_digit) { inst[name] = version; avail[name] = available; outResults.emplace_back(id, name); }
                    }
                }
            } catch(...) {}
        }
        // last resort: attempt to extract name/id pairs using a soft parse (whitespace tokenization)
        if (outResults.empty()) {
            for (auto &s : lines) {
                if (s.empty()) continue;
                if (s.find("upgrades available") != std::string::npos) break;
                std::istringstream ls2(s); std::vector<std::string> toks2; std::string tk;
                while (ls2 >> tk) toks2.push_back(tk);
                if (toks2.size() >= 4) {
                    int n = (int)toks2.size(); std::string id = toks2[n-4]; std::string instv = toks2[n-3]; std::string availv = toks2[n-2];
                    std::string name;
                    for (int j = 0; j < n-4; ++j) { if (!name.empty()) name += ' '; name += toks2[j]; }
                    if (name.empty()) name = id;
                    inst[name] = instv; avail[name] = availv; outResults.emplace_back(id, name);
                }
            }
        }
        // write diagnostics
        try { std::ofstream ofsA("logs\\wup_winget_avail_map.txt", std::ios::binary | std::ios::trunc); for (auto &p : avail) ofsA << p.first << "\t" << p.second << "\n"; } catch(...){ }
        try { std::ofstream ofsI("logs\\wup_winget_inst_map.txt", std::ios::binary | std::ios::trunc); for (auto &p : inst) ofsI << p.first << "\t" << p.second << "\n"; } catch(...){ }

        // validation: both maps must be non-empty and share at least one key (best-effort)
        if (avail.empty() || inst.empty()) {
            // insufficient data
            try { std::ofstream ofsR("logs\\wup_winget_parsed.txt", std::ios::binary | std::ios::trunc); ofsR << "VALIDATION_FAILED\n"; } catch(...){}
            return false;
        }
        bool common = false;
        for (auto &p : avail) if (inst.find(p.first) != inst.end()) { common = true; break; }
        if (!common) {
            // try to match by case-insensitive name equivalence as fallback
            for (auto &pa : avail) {
                std::string a = pa.first; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        AppendLog(std::string("RunFullScan: avail count=") + std::to_string((int)avail.size()) + " inst count=" + std::to_string((int)inst.size()) + "\n");
                    if (a == b) { common = true; break; }
                }
                if (common) break;
            }
        }
        if (!common) {
            try { std::ofstream ofsR("logs\\wup_winget_parsed.txt", std::ios::binary | std::ios::trunc); ofsR << "VALIDATION_NO_COMMON_KEYS\n"; } catch(...){}
            return false;
        }

        // normalize outResults: prefer keys that exist in avail; pair (key, displayName)
        outResults.clear();
        for (auto &p : avail) {
            std::string key = p.first;
            std::string disp = p.first;
            outResults.emplace_back(key, disp);
        }
        try { std::ofstream ofsR("logs\\wup_winget_parsed.txt", std::ios::binary | std::ios::trunc); for (auto &p : outResults) ofsR << p.first << "\t" << p.second << "\n"; } catch(...){ }
        return true;
    } catch(...) { return false; }
}
