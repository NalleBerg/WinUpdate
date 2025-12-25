// Centralized scanner implementation - moved here so all scanning/parsing
// logic lives in one compilation unit as requested.

#include "scan_runner.h"
#include "winget_versions.h"
#include "parsing.h"
#include <windows.h>
#include <regex>
#include <sstream>
#include <utility>
#include <set>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <mutex>

// scan_runner intentionally does not depend on `main.cpp` internals such as
// `normalize_id_main` or the `g_last_*` globals. It returns raw maps and
// lets the caller (main.cpp) normalize and install them into its caches.

static inline std::string trim_copy(std::string s) {
    while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back();
    while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

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
        result.first = -2;
    } else {
        DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode); result.first = (int)exitCode;
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

static inline std::string normalize_id(std::string s) {
    // remove control chars, trim, strip surrounding quotes and trailing punctuation
    std::string out;
    for (char c : s) if ((unsigned char)c >= 32) out.push_back(c);
    while(!out.empty() && isspace((unsigned char)out.front())) out.erase(out.begin());
    while(!out.empty() && isspace((unsigned char)out.back())) out.pop_back();
    if (out.size() >= 2 && ((out.front()=='"' && out.back()=='"') || (out.front()=='\'' && out.back()=='\''))) {
        out = out.substr(1, out.size()-2);
    }
    while(!out.empty() && (out.back()==',' || out.back()=='.' || out.back()=='"')) out.pop_back();
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
        auto r = RunProcessCaptureExitCodeLocal(L"cmd.exe /C winget list --accept-source-agreements --accept-package-agreements --output json", 12000);
        std::string txt = r.second;
        if (!txt.empty() && (txt.front()=='{' || txt.front()=='[' || txt.find("\"Id\"")!=std::string::npos)) {
            size_t pos = 0;
            while (true) {
                size_t idPos = txt.find("\"Id\"", pos);
                if (idPos == std::string::npos) break;
                size_t colon = txt.find(':', idPos);
                if (colon == std::string::npos) { pos = idPos + 4; continue; }
                size_t q1 = txt.find('"', colon);
                if (q1 == std::string::npos) { pos = idPos + 4; continue; }
                size_t q2 = txt.find('"', q1 + 1);
                if (q2 == std::string::npos) { pos = idPos + 4; continue; }
                std::string id = txt.substr(q1 + 1, q2 - q1 - 1);
                size_t verSearchStart = q2 + 1;
                size_t verPos = txt.find("\"Version\"", verSearchStart);
                std::string ver;
                if (verPos != std::string::npos && verPos - verSearchStart < 400) {
                    size_t vcolon = txt.find(':', verPos);
                    if (vcolon != std::string::npos) {
                        size_t vq1 = txt.find('"', vcolon);
                        if (vq1 != std::string::npos) {
                            size_t vq2 = txt.find('"', vq1 + 1);
                            if (vq2 != std::string::npos) ver = txt.substr(vq1 + 1, vq2 - vq1 - 1);
                        }
                    }
                }
                if (!id.empty() && !ver.empty()) out[id] = ver;
                pos = q2 + 1;
            }
            if (!out.empty()) return out;
        }

        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) lines.push_back(trim_copy(ln));
        int headerIdx = -1, sepIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) if (lines[i].find("----")!=std::string::npos) { sepIdx = i; break; }
        if (sepIdx > 0) headerIdx = sepIdx - 1;
        if (headerIdx >= 0) {
            for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                const std::string &sline = lines[i];
                if (sline.empty()) continue;
                if (sline.find("upgraded")!=std::string::npos) break;
                std::istringstream ls(sline);
                std::vector<std::string> toks; std::string tok;
                while (ls >> tok) toks.push_back(tok);
                if (toks.size() >= 4) {
                    int n = (int)toks.size();
                    std::string id = toks[n-4];
                    std::string inst = toks[n-3];
                    id = normalize_id(id);
                    if (!id.empty()) out[id] = inst;
                }
            }
            if (!out.empty()) return out;
            std::string header = lines[headerIdx];
            std::vector<std::string> colNames = {"Name","Id","Version"};
            std::vector<int> colStarts;
            for (auto &cn : colNames) {
                size_t p = header.find(cn);
                if (p != std::string::npos) colStarts.push_back((int)p);
            }
            if (colStarts.size() >= 2) {
                int ncols = (int)colStarts.size();
                for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                    const std::string &sline = lines[i];
                    if (sline.empty()) continue;
                    if (sline.find("upgraded")!=std::string::npos) break;
                    auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
                        int len = (int)s.size(); if (a >= len) return std::string(); int end = std::min(len, b); return s.substr(a, end-a);
                    };
                    std::vector<std::string> fields(ncols);
                    for (int c=0;c<ncols;++c) {
                        int a = colStarts[c]; int b = (c+1<ncols)?colStarts[c+1] : (int)sline.size();
                        std::string f = trim_copy(substrSafe(sline,a,b)); fields[c]=f;
                    }
                    std::string id = (ncols>1)?fields[1]:std::string();
                    std::string inst = (ncols>2)?fields[2]:std::string();
                    id = normalize_id(id);
                    if (!id.empty()) out[id] = inst;
                }
                try {
                    std::istringstream lss(txt);
                    std::string line;
                    std::regex verRe(R"((\d+(?:\.[0-9]+)+))");
                    std::regex idRe(R"(([A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+))");
                    while (std::getline(lss, line)) {
                        std::smatch m;
                        std::vector<std::string> vers;
                        std::string sline = line;
                        auto it = sline.cbegin();
                        while (std::regex_search(it, sline.cend(), m, verRe)) { vers.push_back(m[1].str()); it = m.suffix().first; }
                        if (vers.empty()) continue;
                        if (std::regex_search(sline, m, idRe)) {
                            std::string id = m[1].str();
                            id = normalize_id(id);
                            out[id] = vers.front();
                        }
                    }
                    if (!out.empty()) return out;
                } catch(...) {}
                return out;
            }
        }

        std::istringstream iss2(txt);
        while (std::getline(iss2, ln)) {
            if (ln.find("----")!=std::string::npos) continue;
            if (ln.find("Name")!=std::string::npos && ln.find("Id")!=std::string::npos) continue;
            std::string s = trim_copy(ln);
            if (s.empty()) continue;
            std::istringstream ls(s);
            std::vector<std::string> toks; std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() < 2) continue;
            auto tolower_copy = [](const std::string &x){ std::string t=x; std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return t; };
            std::string last = toks.back(); std::string last_l = tolower_copy(last);
            bool isSource = (last_l.find("winget")!=std::string::npos) || (last_l.find("msstore")!=std::string::npos) || (last_l.find("msix")!=std::string::npos) || (last_l.find("arp\\")!=std::string::npos) || (last_l.find("source")!=std::string::npos);
            std::string id; std::string inst;
            if (isSource && toks.size() >= 3) {
                inst = toks[toks.size()-2];
                id = toks[toks.size()-3];
            } else {
                inst = toks.back();
                id = (toks.size() >= 2) ? toks[toks.size()-2] : toks.front();
            }
            id = normalize_id(id);
            out[id] = inst;
        }
    } catch(...) {}
    if (out.empty()) {
        try {
            auto r2 = RunProcessCaptureExitCodeLocal(L"cmd.exe /C build\\winget_print.exe", 15000);
            std::string txt = r2.second;
            if (!txt.empty()) {
                std::istringstream iss(txt);
                std::string line;
                bool inInstalled = false;
                while (std::getline(iss, line)) {
                    if (line.find("Installed map") != std::string::npos) { inInstalled = true; continue; }
                    if (line.find("Available map") != std::string::npos) break;
                    if (!inInstalled) continue;
                    if (line.empty()) continue;
                    size_t tab = line.find('\t');
                    if (tab != std::string::npos) {
                        std::string id = trim_copy(line.substr(0, tab));
                        std::string ver = trim_copy(line.substr(tab+1));
                        if (!id.empty() && !ver.empty()) out[normalize_id(id)] = ver;
                        continue;
                    }
                    std::istringstream ls(line);
                    std::vector<std::string> toks;
                    std::string tok;
                    while (ls >> tok) toks.push_back(tok);
                    if (toks.size() >= 2) {
                        std::string ver = toks.back();
                        toks.pop_back();
                        std::string id;
                        for (size_t i = 0; i < toks.size(); ++i) { if (i) id += " "; id += toks[i]; }
                        if (!id.empty() && !ver.empty()) out[normalize_id(id)] = ver;
                    }
                }
            }
        } catch(...) {}
    }
    return out;
}

std::unordered_map<std::string,std::string> MapAvailableVersions() {
    std::unordered_map<std::string,std::string> out;
    try {
        auto r = RunProcessCaptureExitCodeLocal(L"cmd.exe /C winget upgrade --accept-source-agreements --accept-package-agreements --output json", 12000);
        std::string txt = r.second;
        if (!txt.empty() && (txt.front()=='{' || txt.front()=='[' || txt.find("\"Id\"")!=std::string::npos)) {
            size_t pos = 0;
            while (true) {
                size_t idPos = txt.find("\"Id\"", pos);
                if (idPos == std::string::npos) break;
                size_t colon = txt.find(':', idPos);
                if (colon == std::string::npos) { pos = idPos + 4; continue; }
                size_t q1 = txt.find('"', colon);
                if (q1 == std::string::npos) { pos = idPos + 4; continue; }
                size_t q2 = txt.find('"', q1 + 1);
                if (q2 == std::string::npos) { pos = idPos + 4; continue; }
                std::string id = txt.substr(q1 + 1, q2 - q1 - 1);
                size_t availPos = txt.find("\"Available", q2 + 1);
                std::string avail;
                if (availPos != std::string::npos && availPos - q2 < 600) {
                    size_t acolon = txt.find(':', availPos);
                    if (acolon != std::string::npos) {
                        size_t aq1 = txt.find('"', acolon);
                        if (aq1 != std::string::npos) {
                            size_t aq2 = txt.find('"', aq1 + 1);
                            if (aq2 != std::string::npos) avail = txt.substr(aq1 + 1, aq2 - aq1 - 1);
                        }
                    }
                }
                if (!id.empty() && !avail.empty()) out[id] = avail;
                pos = q2 + 1;
            }
            if (!out.empty()) return out;
        }

        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) lines.push_back(trim_copy(ln));
        int headerIdx=-1, sepIdx=-1;
        for (int i=0;i<(int)lines.size();++i) if (lines[i].find("----")!=std::string::npos) { sepIdx=i; break; }
        if (sepIdx>0) headerIdx=sepIdx-1;
        if (headerIdx>=0) {
            for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                const std::string &sline = lines[i];
                if (sline.empty()) continue;
                if (sline.find("upgrades available")!=std::string::npos) break;
                std::istringstream ls(sline);
                std::vector<std::string> toks; std::string tok;
                while (ls >> tok) toks.push_back(tok);
                if (toks.size() >= 4) {
                    int n = (int)toks.size();
                    std::string id = toks[n-4];
                    std::string available = toks[n-2];
                    id = normalize_id(id);
                    if (!id.empty()) out[id]=available;
                }
            }
            if (!out.empty()) return out;
            std::string header = lines[headerIdx];
            std::vector<std::string> colNames = {"Name","Id","Version","Available"};
            std::vector<int> colStarts;
            for (auto &cn:colNames){ size_t p=header.find(cn); if (p!=std::string::npos) colStarts.push_back((int)p); }
            if (colStarts.size()>=2) {
                int ncols=(int)colStarts.size();
                for (int i=sepIdx+1;i<(int)lines.size();++i) {
                    const std::string &sline = lines[i]; if (sline.empty()) continue; if (sline.find("upgrades available")!=std::string::npos) break;
                    auto substrSafe=[&](const std::string &s,int a,int b)->std::string{int len=(int)s.size(); if(a>=len) return std::string(); int end=std::min(len,b); return s.substr(a,end-a);};
                    std::vector<std::string> fields(ncols);
                    for (int c=0;c<ncols;++c){int a=colStarts[c]; int b=(c+1<ncols)?colStarts[c+1]:(int)sline.size(); fields[c]=trim_copy(substrSafe(sline,a,b));}
                    std::string id = (ncols>1)?fields[1]:std::string();
                    std::string available = (ncols>3)?fields[3]:std::string();
                    id = normalize_id(id);
                    if (!id.empty()) out[id]=available;
                }
                return out;
            }
        }

        std::istringstream iss2(txt);
        while (std::getline(iss2, ln)) {
            if (ln.find("----")!=std::string::npos) continue;
            if (ln.find("Name")!=std::string::npos && ln.find("Id")!=std::string::npos) continue;
            std::string s = trim_copy(ln);
            if (s.empty()) continue;
            std::istringstream ls(s);
            std::vector<std::string> toks; std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.empty()) continue;
            auto tolower_copy = [](const std::string &x){ std::string t=x; std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return (char)std::tolower(c); }); return t; };
            std::string last = toks.back(); std::string last_l = tolower_copy(last);
            bool isSource = (last_l.find("winget")!=std::string::npos) || (last_l.find("msstore")!=std::string::npos) || (last_l.find("msix")!=std::string::npos) || (last_l.find("arp\\")!=std::string::npos) || (last_l.find("source")!=std::string::npos);
            std::string id; std::string available;
            if (isSource && toks.size() >= 3) {
                available = toks[toks.size()-2];
                id = toks[toks.size()-3];
            } else {
                available = toks.back();
                id = (toks.size() >= 2) ? toks[toks.size()-2] : toks.front();
            }
            id = normalize_id(id);
            if (!id.empty()) out[id] = available;
        }
        try {
            std::regex anyRe(R"(([\S ]+?)\s+([^\s]+)\s+(\d+(?:\.[0-9]+)*)\s+(\d+(?:\.[0-9]+)*))");
            std::smatch m;
            std::string::const_iterator it = txt.begin();
            while (std::regex_search(it, txt.cend(), m, anyRe)) {
                std::string id = m[2].str();
                std::string available = m[4].str();
                id = normalize_id(id);
                if (!id.empty() && !available.empty()) out[id] = available;
                it = m.suffix().first;
            }
            if (!out.empty()) return out;
        } catch(...) {}
        try {
            std::istringstream lss(txt);
            std::string line;
            std::regex verRe(R"((\d+(?:\.[0-9]+)+))");
            std::regex idRe(R"(([A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+))");
            while (std::getline(lss, line)) {
                std::smatch m;
                std::vector<std::string> vers;
                std::string sline = line;
                auto it = sline.cbegin();
                while (std::regex_search(it, sline.cend(), m, verRe)) { vers.push_back(m[1].str()); it = m.suffix().first; }
                if (vers.empty()) continue;
                if (std::regex_search(sline, m, idRe)) {
                    std::string id = m[1].str();
                    std::string avail = (vers.size() >= 2) ? vers[1] : vers.back();
                    id = normalize_id(id);
                    if (!avail.empty()) out[id] = avail;
                }
            }
            if (!out.empty()) return out;
        } catch(...) {}
    } catch(...) {}
    if (out.empty()) {
        try {
            auto r2 = RunProcessCaptureExitCodeLocal(L"cmd.exe /C build\\winget_print.exe", 15000);
            std::string txt = r2.second;
            if (!txt.empty()) {
                std::istringstream iss(txt);
                std::string line;
                bool inAvail = false;
                while (std::getline(iss, line)) {
                    if (line.find("Available map") != std::string::npos) { inAvail = true; continue; }
                    if (line.find("Installed map") != std::string::npos) continue;
                    if (!inAvail) continue;
                    if (line.empty()) continue;
                    size_t tab = line.find('\t');
                    if (tab != std::string::npos) {
                        std::string id = trim_copy(line.substr(0, tab));
                        std::string ver = trim_copy(line.substr(tab+1));
                        if (!id.empty() && !ver.empty()) out[normalize_id(id)] = ver;
                        continue;
                    }
                    std::istringstream ls(line);
                    std::vector<std::string> toks;
                    std::string tok;
                    while (ls >> tok) toks.push_back(tok);
                    if (toks.size() >= 2) {
                        std::string ver = toks.back(); toks.pop_back();
                        std::string id;
                        for (size_t i = 0; i < toks.size(); ++i) { if (i) id += " "; id += toks[i]; }
                        if (!id.empty() && !ver.empty()) out[normalize_id(id)] = ver;
                    }
                }
            }
        } catch(...) {}
    }
    return out;
}

// thin exported wrappers (kept for compatibility)
std::unordered_map<std::string,std::string> MapInstalledVersions_ext() { return MapInstalledVersions(); }
std::unordered_map<std::string,std::string> MapAvailableVersions_ext() { return MapAvailableVersions(); }

bool ScanAndPopulateMaps(std::unordered_map<std::string,std::string> &avail, std::unordered_map<std::string,std::string> &inst) {
    try {
        // Prefer bulk available-version scan first (fast), then installed map.
        avail = MapAvailableVersions();
        inst = MapInstalledVersions();
        // For any id that has an available version but no installed version
        // query winget for that id specifically to obtain a reliable installed version.
        for (auto &p : avail) {
            const std::string &id = p.first;
            if (!id.empty() && inst.find(id) == inst.end()) {
                try {
                    std::wstring cmd = L"cmd.exe /C winget list --id \"";
                    // convert id to wide
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, id.c_str(), (int)id.size(), NULL, 0);
                    std::wstring wid(wlen, L' ');
                    if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, id.c_str(), (int)id.size(), &wid[0], wlen);
                    cmd += wid;
                    cmd += L"\" --accept-source-agreements --accept-package-agreements --output json";
                    auto r = RunProcessCaptureExitCodeLocal(cmd, 7000);
                    std::string txt = r.second;
                    if (!txt.empty() && (txt.front()=='{' || txt.front()=='[' || txt.find("\"Id\"")!=std::string::npos)) {
                        // try to extract Version from JSON-like output
                        size_t pos = txt.find("\"Version\"");
                        if (pos != std::string::npos) {
                            size_t colon = txt.find(':', pos);
                            if (colon != std::string::npos) {
                                size_t q1 = txt.find('"', colon);
                                if (q1 != std::string::npos) {
                                    size_t q2 = txt.find('"', q1 + 1);
                                    if (q2 != std::string::npos) {
                                        std::string ver = txt.substr(q1 + 1, q2 - q1 - 1);
                                        if (!ver.empty()) inst[id] = ver;
                                    }
                                }
                            }
                        }
                    }
                } catch(...) {}
            }
        }
        // diagnostic dumps (raw ids as returned by winget/parsers)
        try { std::ofstream ofsA("wup_winget_avail_map.txt", std::ios::binary | std::ios::trunc); if (ofsA) for (auto &p : avail) ofsA << p.first << "\t" << p.second << "\n"; } catch(...) {}
        try { std::ofstream ofsI("wup_winget_inst_map.txt", std::ios::binary | std::ios::trunc); if (ofsI) for (auto &p : inst) ofsI << p.first << "\t" << p.second << "\n"; } catch(...) {}
        return true;
    } catch(...) {
        return false;
    }
}
