#include "skip_update.h"
#include <windows.h>
#include <string>
#include "logging.h"
#include <map>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

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
    return out;
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
        // prepare new line with a tab between
        std::string newLine = identifier + "\t" + version;
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
