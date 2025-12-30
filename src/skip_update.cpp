#include "skip_update.h"
#include <windows.h>
#include <string>
#include "logging.h"
#include "parsing.h"
#include <map>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

static std::string GetIniPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string path;
    if (len > 0 && len < MAX_PATH) path = std::string(buf) + "\\WinUpdate";
    else path = ".";
    // ensure dir
    int nw = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
    if (nw > 0) {
        std::vector<wchar_t> wb(nw);
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wb.data(), nw);
        CreateDirectoryW(wb.data(), NULL);
    }
    return path + "\\wup_settings.ini";
}

static std::map<std::string,std::string> ParseSkippedSection(const std::string &sectionText) {
    std::map<std::string,std::string> out;
    std::istringstream iss(sectionText);
    std::string ln;
    while (std::getline(iss, ln)) {
        // trim
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        trim(ln);
        if (ln.empty()) continue;
        if (ln[0] == ';' || ln[0] == '#') continue;
        // parse right-to-left: last whitespace separates identifier (left) and version (right)
        size_t p = ln.find_last_of(" \t");
        if (p == std::string::npos) continue;
        std::string id = ln.substr(0, p);
        std::string ver = ln.substr(p+1);
        trim(ver);
        if (!id.empty() && !ver.empty()) out[id] = ver;
    }
    return out;
}

std::map<std::string,std::string> LoadSkippedMap() {
    std::map<std::string,std::string> out;
    std::string ini = GetIniPath();
    std::ifstream ifs(ini, std::ios::binary);
    if (!ifs) {
        AppendLog(std::string("LoadSkippedMap: failed to open ini: ") + ini + "\n");
        return out;
    }
    std::string line;
    bool inSkipped = false;
    std::ostringstream ss;
    while (std::getline(ifs, line)) {
        std::string l = line;
        // trim
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        trim(l);
        if (l.empty()) continue;
        if (l.front() == '[') {
            if (inSkipped) break;
            inSkipped = (l == "[skipped]");
            continue;
        }
        if (inSkipped) ss << line << "\n";
    }
    if (ss.str().size() > 0) out = ParseSkippedSection(ss.str());
    // Normalize keys: strip trailing installed-version tokens from identifiers to keep left-side clean
    auto isVersionToken = [](const std::string &t){ if (t.empty()) return false; for (char c : t) if (!(isdigit((unsigned char)c) || c=='.' || c=='-' || c=='_')) return false; return true; };
    auto trim_inplace = [](std::string &x){ size_t a = x.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { x.clear(); return; } size_t b = x.find_last_not_of(" \t\r\n"); x = x.substr(a, b-a+1); };
    std::map<std::string,std::string> normalized;
    bool changed = false;
    for (auto &p : out) {
        std::string key = p.first;
        trim_inplace(key);
        // strip trailing tokens that look like versions
        while (true) {
            size_t pos = key.find_last_of(" \t");
            if (pos == std::string::npos) break;
            std::string last = key.substr(pos+1);
            if (isVersionToken(last)) { key = key.substr(0, pos); trim_inplace(key); changed = true; continue; }
            break;
        }
        if (key.empty()) key = p.first;
        normalized[key] = p.second;
    }
    if (changed) {
        AppendLog(std::string("LoadSkippedMap: normalized skipped keys, rewriting ini: ") + ini + "\n");
        SaveSkippedMap(normalized);
    }
    return normalized;
}

bool SaveSkippedMap(const std::map<std::string,std::string> &m) {
    std::string ini = GetIniPath();
    AppendLog(std::string("SaveSkippedMap: ini=") + ini + "\n");
    // Read entire file and replace [skipped] section
    std::ifstream ifs(ini, std::ios::binary);
    std::string pre, post; std::string line;
    bool seenSkipped = false; bool inSkipped = false;
    if (ifs) {
        while (std::getline(ifs, line)) {
            std::string l = line; auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
            trim(l);
            if (l.empty()) continue;
            if (l.front() == '[') {
                if (inSkipped) { inSkipped = false; seenSkipped = true; post.clear(); }
                if (l == "[skipped]") { inSkipped = true; continue; }
            }
            if (inSkipped) continue;
            if (!seenSkipped) pre += line + "\n"; else post += line + "\n";
        }
        ifs.close();
    }
    // Write back: pre + [skipped] + entries + post
    std::string tmp = ini + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        AppendLog(std::string("SaveSkippedMap: failed to open tmp file: ") + tmp + "\n");
        return false;
    }
    if (!pre.empty()) ofs << pre;
    ofs << "[skipped]\n";
    for (auto &p : m) ofs << p.first << "  " << p.second << "\n";
    ofs << "\n";
    if (!post.empty()) ofs << post;
    ofs.close();
    // atomic replace
    BOOL del = DeleteFileA(ini.c_str());
    if (!del) {
        DWORD err = GetLastError();
        AppendLog(std::string("SaveSkippedMap: DeleteFileA returned error ") + std::to_string(err) + "\n");
    }
    BOOL mv = MoveFileA(tmp.c_str(), ini.c_str());
    if (!mv) {
        DWORD err = GetLastError();
        AppendLog(std::string("SaveSkippedMap: MoveFileA failed err=") + std::to_string(err) + "\n");
    } else {
        AppendLog(std::string("SaveSkippedMap: wrote ini successfully: ") + ini + "\n");
    }
    return mv != 0;
}

static bool VersionGreater(const std::string &a, const std::string &b) {
    // split by '.' and compare numeric components
    auto split = [](const std::string &s){ std::vector<std::string> out; std::string cur; for (char c : s) { if (c=='.' || c=='-' || c=='_') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } } else cur.push_back(c); } if (!cur.empty()) out.push_back(cur); return out; };
    auto A = split(a); auto B = split(b);
    size_t n = std::max(A.size(), B.size());
    for (size_t i = 0; i < n; ++i) {
        long ai = 0, bi = 0;
        if (i < A.size()) try { ai = std::stol(A[i]); } catch(...) { ai = 0; }
        if (i < B.size()) try { bi = std::stol(B[i]); } catch(...) { bi = 0; }
        if (ai > bi) return true;
        if (ai < bi) return false;
    }
    return false;
}

bool AddSkippedEntry(const std::string &id, const std::string &version) {
    auto m = LoadSkippedMap();
    m[id] = version;
    return SaveSkippedMap(m);
}

bool RemoveSkippedEntry(const std::string &id) {
    auto m = LoadSkippedMap();
    auto it = m.find(id);
    if (it == m.end()) return false;
    m.erase(it);
    return SaveSkippedMap(m);
}

bool IsSkipped(const std::string &id, const std::string &availableVersion) {
    auto m = LoadSkippedMap();
    auto it = m.find(id);
    if (it == m.end()) return false;
    std::string stored = it->second;
    if (stored == availableVersion) return true;
    if (VersionGreater(availableVersion, stored)) {
        // new version supersedes skip; remove entry
        m.erase(it);
        SaveSkippedMap(m);
        return false;
    }
    // availableVersion < stored => still skip
    return true;
}

void PurgeObsoleteSkips(const std::map<std::string,std::string> &currentAvail) {
    auto m = LoadSkippedMap();
    bool changed = false;
    for (auto it = m.begin(); it != m.end(); ) {
        auto cit = currentAvail.find(it->first);
        if (cit != currentAvail.end()) {
            if (VersionGreater(cit->second, it->second)) { it = m.erase(it); changed = true; continue; }
        }
        ++it;
    }
    if (changed) SaveSkippedMap(m);
}
    bool AppendSkippedRaw(const std::string &identifier, const std::string &version) {
        std::string ini = GetIniPath();
        // read file into lines
        std::ifstream ifs(ini, std::ios::binary);
        std::vector<std::string> lines;
        std::string line;
        if (ifs) {
            while (std::getline(ifs, line)) lines.push_back(line);
            ifs.close();
        } else {
            // create minimal ini
            lines.push_back("[language]");
            lines.push_back("en");
            lines.push_back("");
            lines.push_back("[skipped]");
        }
        // find [skipped] section
        int idx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            std::string t = lines[i];
            // trim
            auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
            trim(t);
            if (t == "[skipped]") { idx = i; break; }
        }
        if (idx == -1) {
            // append at end
            lines.push_back("");
            lines.push_back("[skipped]");
            idx = (int)lines.size() - 1;
        }
        // find insertion point after last non-empty skipped entry
        int insertAt = idx + 1;
        while (insertAt < (int)lines.size()) {
            std::string t = lines[insertAt];
            auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
            std::string tt = t; trim(tt);
            if (tt.empty() || tt.front() == '[') break;
            ++insertAt;
        }
        // Attempt to resolve identifier (display name) to package id (no whitespace)
        std::string writeId = identifier;
        try {
            // If identifier contains whitespace, it's likely a display name — try to resolve
            bool needsResolve = (identifier.find_first_of(" \t") != std::string::npos);
            if (needsResolve) {
                AppendLog(std::string("AppendSkippedRaw: attempting id resolution for '") + identifier + "'\n");
                try {
                    // Try most recent raw winget capture first
                    std::string raw = ReadMostRecentRawWinget();
                    if (!raw.empty()) {
                        ParseWingetTextForPackages(raw);
                        AppendLog(std::string("AppendSkippedRaw: ParseWingetTextForPackages populated g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
                    }
                } catch(...) { AppendLog("AppendSkippedRaw: ReadMostRecentRawWinget/parse threw\n"); }

                // search g_packages for matching display name (exact or case-insensitive/substring)
                try {
                    std::string idFound;
                    // normalize: remove trailing version-like tokens from identifier (e.g. "Vulkan SDK 1.4.328.1" -> "Vulkan SDK")
                    auto toLower = [](const std::string &s){ std::string r = s; for (auto &c : r) c = (char)tolower((unsigned char)c); return r; };
                    auto isVersionToken = [](const std::string &t){ if (t.empty()) return false; for (char c : t) { if (!(isdigit((unsigned char)c) || c=='.' || c=='-' || c=='_')) return false; } return true; };
                    auto stripTrailingVersionTokens = [&](std::string s){ // remove trailing space-separated tokens that look like versions
                        // trim
                        auto trim_inplace = [](std::string &x){ size_t a = x.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { x.clear(); return; } size_t b = x.find_last_not_of(" \t\r\n"); x = x.substr(a, b-a+1); };
                        trim_inplace(s);
                        while (true) {
                            size_t p = s.find_last_of(" \t");
                            if (p==std::string::npos) break;
                            std::string last = s.substr(p+1);
                            if (isVersionToken(last)) {
                                s = s.substr(0, p);
                                trim_inplace(s);
                                continue;
                            }
                            break;
                        }
                        return s;
                    };
                    auto tokenize = [](const std::string &x){ std::vector<std::string> out; std::string cur; for (char c : x) { if (isalnum((unsigned char)c)) cur.push_back((char)tolower((unsigned char)c)); else { if (!cur.empty()) { out.push_back(cur); cur.clear(); } } } if (!cur.empty()) out.push_back(cur); return out; };

                    std::string ident_stripped = stripTrailingVersionTokens(identifier);
                    std::string name_l = toLower(ident_stripped);
                    auto tokens = tokenize(name_l);
                    {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        // 1) exact name match
                        for (auto &p : g_packages) {
                            if (p.second == ident_stripped) { idFound = p.first; break; }
                        }
                        // 2) case-insensitive substring/equality
                        if (idFound.empty()) {
                            for (auto &p : g_packages) {
                                std::string nm = p.second; std::string nm_l = toLower(nm);
                                if (nm_l == name_l || nm_l.find(name_l) != std::string::npos || name_l.find(nm_l) != std::string::npos) { idFound = p.first; break; }
                            }
                        }
                        // 3) token-subset match against package name
                        if (idFound.empty() && !tokens.empty()) {
                            for (auto &p : g_packages) {
                                std::string nm = p.second; std::string nm_l = toLower(nm);
                                bool all = true;
                                for (auto &t : tokens) if (nm_l.find(t) == std::string::npos) { all = false; break; }
                                if (all) { idFound = p.first; break; }
                            }
                        }
                        // 4) token-subset match against package id
                        if (idFound.empty() && !tokens.empty()) {
                            for (auto &p : g_packages) {
                                std::string idl = toLower(p.first);
                                bool all = true;
                                for (auto &t : tokens) if (idl.find(t) == std::string::npos) { all = false; break; }
                                if (all) { idFound = p.first; break; }
                            }
                        }
                    }
                    // If still not found, try fallback files in workspace by re-parsing them
                    if (idFound.empty()) {
                        const char *fallbacks[] = { "wup_winget_raw.txt", "wup_winget_raw_fallback.txt", "wup_winget_list_fallback.txt" };
                        for (auto f : fallbacks) {
                            std::ifstream ifs2(f, std::ios::binary);
                            if (!ifs2) continue;
                            std::string content((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
                            try { ParseWingetTextForPackages(content); } catch(...) { }
                            std::lock_guard<std::mutex> lk2(g_packages_mutex);
                            for (auto &p : g_packages) {
                                if (p.second == identifier) { idFound = p.first; break; }
                            }
                            if (!idFound.empty()) break;
                        }
                    }
                    // Final fallback: try extracting Id/Name pairs and fuzzy-match names
                    if (idFound.empty()) {
                        const char *fallbacks2[] = { "wup_winget_raw.txt", "wup_winget_raw_fallback.txt", "wup_winget_list_fallback.txt" };
                        for (auto f : fallbacks2) {
                            std::ifstream ifs3(f, std::ios::binary);
                            if (!ifs3) continue;
                            std::string content((std::istreambuf_iterator<char>(ifs3)), std::istreambuf_iterator<char>());
                            auto vec = ExtractIdsFromNameIdText(content);
                            std::string name_l = identifier; for (auto &c : name_l) c = (char)tolower((unsigned char)c);
                            for (auto &pr : vec) {
                                std::string nm = pr.second; std::string nm_l = nm; for (auto &c : nm_l) c = (char)tolower((unsigned char)c);
                                if (nm_l == name_l || nm_l.find(name_l) != std::string::npos || name_l.find(nm_l) != std::string::npos) {
                                    idFound = pr.first; break;
                                }
                            }
                            if (!idFound.empty()) break;
                        }
                    }
                    if (!idFound.empty()) {
                        AppendLog(std::string("AppendSkippedRaw: resolved '") + identifier + "' -> id='" + idFound + "'\n");
                        writeId = idFound;
                    } else {
                        AppendLog(std::string("AppendSkippedRaw: could not resolve id for '") + identifier + "' from current data, will trigger refresh and retry\n");
                        // Trigger an async refresh in the main window to repopulate g_packages (winget scan)
                        try {
                            HWND hMain = FindWindowW(L"WinUpdateClass", NULL);
                            if (hMain) {
                                PostMessageW(hMain, WM_APP + 1, 1, 0);
                                AppendLog("AppendSkippedRaw: posted WM_REFRESH_ASYNC to request package list refresh; not blocking UI\n");
                                // Do not wait here — migration will run after refresh completes.
                            } else {
                                AppendLog("AppendSkippedRaw: main window not found, cannot request refresh\n");
                            }
                        } catch(...) { AppendLog("AppendSkippedRaw: refresh post threw\n"); }
                    }
                } catch(...) { AppendLog("AppendSkippedRaw: id search threw\n"); }
            }
        } catch(...) { AppendLog("AppendSkippedRaw: id resolution outer try threw\n"); }

        // Before writing, strip any trailing installed-version tokens from the identifier
        auto trim_inplace = [](std::string &x){ size_t a = x.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { x.clear(); return; } size_t b = x.find_last_not_of(" \t\r\n"); x = x.substr(a, b-a+1); };
        auto isVersionToken = [](const std::string &t){ if (t.empty()) return false; for (char c : t) { if (!(isdigit((unsigned char)c) || c=='.' || c=='-' || c=='_')) return false; } return true; };
        // operate on a local copy so we don't alter writeId used elsewhere
        std::string writeIdForFile = writeId;
        trim_inplace(writeIdForFile);
        while (true) {
            size_t p = writeIdForFile.find_last_of(" \t");
            if (p == std::string::npos) break;
            std::string last = writeIdForFile.substr(p+1);
            if (isVersionToken(last)) {
                writeIdForFile = writeIdForFile.substr(0, p);
                trim_inplace(writeIdForFile);
                continue;
            }
            break;
        }
        // prepare new line with a tab between (use resolved id when available)
        std::string newLine = writeIdForFile + "\t" + version;
        lines.insert(lines.begin() + insertAt, newLine);
        // write to tmp and move
        std::string tmp = ini + ".tmp";
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            AppendLog(std::string("AppendSkippedRaw: failed to open tmp file: ") + tmp + "\n");
            return false;
        }
        for (auto &ln : lines) ofs << ln << "\n";
        ofs.close();
        BOOL del = DeleteFileA(ini.c_str());
        if (!del) {
            DWORD err = GetLastError();
            AppendLog(std::string("AppendSkippedRaw: DeleteFileA returned error ") + std::to_string(err) + "\n");
        }
        BOOL mv = MoveFileA(tmp.c_str(), ini.c_str());
            if (!mv) {
                DWORD err = GetLastError();
                AppendLog(std::string("AppendSkippedRaw: MoveFileA failed err=") + std::to_string(err) + "\n");
            } else {
                AppendLog(std::string("AppendSkippedRaw: appended skipped entry: ") + identifier + "\t" + version + " to " + ini + "\n");
                // Notify main window to refresh so the UI rescans the updated INI
                try {
                    HWND hMain = FindWindowW(L"WinUpdateClass", NULL);
                    if (hMain) {
                        PostMessageW(hMain, WM_APP + 1, 1, 0);
                        AppendLog(std::string("AppendSkippedRaw: posted WM_REFRESH_ASYNC to main window\n"));
                    }
                } catch(...) {}
            }
            return mv != 0;
    }

        bool MigrateSkippedEntries() {
            try {
                auto m = LoadSkippedMap();
                if (m.empty()) return false;
                bool changed = false;
                // helper functions (copy of logic used above)
                auto toLower = [](const std::string &s){ std::string r = s; for (auto &c : r) c = (char)tolower((unsigned char)c); return r; };
                auto isVersionToken = [](const std::string &t){ if (t.empty()) return false; for (char c : t) { if (!(isdigit((unsigned char)c) || c=='.' || c=='-' || c=='_')) return false; } return true; };
                auto stripTrailingVersionTokens = [&](std::string s){ auto trim_inplace = [](std::string &x){ size_t a = x.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { x.clear(); return; } size_t b = x.find_last_not_of(" \t\r\n"); x = x.substr(a, b-a+1); }; trim_inplace(s); while (true) { size_t p = s.find_last_of(" \t"); if (p==std::string::npos) break; std::string last = s.substr(p+1); if (isVersionToken(last)) { s = s.substr(0, p); trim_inplace(s); continue; } break; } return s; };
                auto tokenize = [](const std::string &x){ std::vector<std::string> out; std::string cur; for (char c : x) { if (isalnum((unsigned char)c)) cur.push_back((char)tolower((unsigned char)c)); else { if (!cur.empty()) { out.push_back(cur); cur.clear(); } } } if (!cur.empty()) out.push_back(cur); return out; };

                std::map<std::string,std::string> newMap = m;
                for (auto it = m.begin(); it != m.end(); ++it) {
                    const std::string &key = it->first;
                    const std::string &ver = it->second;
                    // if key contains whitespace or doesn't look like an id (no dot and contains spaces), try migrate
                    bool looksLikeId = (key.find('.') != std::string::npos);
                    if (looksLikeId) continue;
                    std::string ident_stripped = stripTrailingVersionTokens(key);
                    std::string name_l = toLower(ident_stripped);
                    auto tokens = tokenize(name_l);
                    std::string idFound;
                    {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        // exact
                        for (auto &p : g_packages) if (p.second == ident_stripped) { idFound = p.first; break; }
                        if (idFound.empty()) {
                            for (auto &p : g_packages) { std::string nm_l = toLower(p.second); if (nm_l == name_l || nm_l.find(name_l) != std::string::npos || name_l.find(nm_l) != std::string::npos) { idFound = p.first; break; } }
                        }
                        if (idFound.empty() && !tokens.empty()) {
                            for (auto &p : g_packages) { std::string nm_l = toLower(p.second); bool all = true; for (auto &t : tokens) if (nm_l.find(t) == std::string::npos) { all = false; break; } if (all) { idFound = p.first; break; } }
                        }
                        if (idFound.empty() && !tokens.empty()) {
                            for (auto &p : g_packages) { std::string idl = toLower(p.first); bool all = true; for (auto &t : tokens) if (idl.find(t) == std::string::npos) { all = false; break; } if (all) { idFound = p.first; break; } }
                        }
                    }
                    if (!idFound.empty()) {
                        // migrate
                        if (newMap.count(idFound) == 0) {
                            newMap[idFound] = ver;
                        } else {
                            // prefer existing id-entry if present and keep higher version
                            if (VersionGreater(ver, newMap[idFound])) newMap[idFound] = ver;
                        }
                        newMap.erase(key);
                        changed = true;
                        AppendLog(std::string("MigrateSkippedEntries: migrated '") + key + " -> " + idFound + "\n");
                    }
                }
            if (changed) SaveSkippedMap(newMap);
                return changed;
            } catch(...) { return false; }
        }
