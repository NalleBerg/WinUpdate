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
#include <mutex>

// Global map to store ID -> display name in memory
static std::map<std::string, std::string> g_id_to_displayname;
static std::mutex g_id_displayname_mutex;

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
    // Log loaded skipped map for diagnostics
    try {
        for (auto &kv : out) {
            AppendLog(std::string("LoadSkippedMap: entry '") + kv.first + "' -> '" + kv.second + "'\n");
        }
    } catch(...) {}
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
    // Try to atomically replace the target file. Prefer MoveFileEx with REPLACE_EXISTING
    BOOL moved = FALSE;
    DWORD lastErr = 0;
    // First attempt: MoveFileEx with replace flag
    moved = MoveFileExA(tmp.c_str(), ini.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!moved) {
        lastErr = GetLastError();
        AppendLog(std::string("SaveSkippedMap: MoveFileEx(MOVEFILE_REPLACE_EXISTING) failed err=") + std::to_string(lastErr) + "\n");
        // Fallback: try Delete + Move
        BOOL del = DeleteFileA(ini.c_str());
        if (!del) {
            DWORD derr = GetLastError();
            AppendLog(std::string("SaveSkippedMap: DeleteFileA returned error ") + std::to_string(derr) + "\n");
        }
        BOOL mv = MoveFileA(tmp.c_str(), ini.c_str());
        if (!mv) {
            DWORD merr = GetLastError();
            AppendLog(std::string("SaveSkippedMap: MoveFileA failed err=") + std::to_string(merr) + "\n");
            // Last-resort fallback: try CopyFile over the destination
            BOOL cp = CopyFileA(tmp.c_str(), ini.c_str(), FALSE);
            if (!cp) {
                DWORD cerr = GetLastError();
                AppendLog(std::string("SaveSkippedMap: CopyFileA fallback failed err=") + std::to_string(cerr) + "\n");
            } else {
                AppendLog(std::string("SaveSkippedMap: CopyFileA fallback succeeded\n"));
                moved = TRUE;
            }
        } else {
            AppendLog(std::string("SaveSkippedMap: MoveFileA succeeded after Delete: ") + ini + "\n");
            moved = TRUE;
        }
    } else {
        AppendLog(std::string("SaveSkippedMap: MoveFileEx succeeded: ") + ini + "\n");
    }
    return moved != 0;
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

bool AddSkippedEntry(const std::string &id, const std::string &version, const std::string &displayName) {
    // Store display name in memory if provided
    if (!displayName.empty() && displayName != id) {
        std::lock_guard<std::mutex> lock(g_id_displayname_mutex);
        g_id_to_displayname[id] = displayName;
        AppendLog(std::string("AddSkippedEntry: stored display name '") + displayName + "' for id '" + id + "'\n");
    }
    auto m = LoadSkippedMap();
    m[id] = version;
    return SaveSkippedMap(m);
}

std::string GetDisplayNameForId(const std::string &id) {
    std::lock_guard<std::mutex> lock(g_id_displayname_mutex);
    auto it = g_id_to_displayname.find(id);
    if (it != g_id_to_displayname.end()) {
        return it->second;
    }
    return id; // fallback to id if not found
}

bool RemoveSkippedEntry(const std::string &id) {
    auto m = LoadSkippedMap();
    auto it = m.find(id);
    if (it == m.end()) return false;
    m.erase(it);
    return SaveSkippedMap(m);
}

bool IsSkipped(const std::string &id, const std::string &availableVersion) {
    try {
        auto m = LoadSkippedMap();
        // sanitize helper: remove all whitespace/control characters
        auto sanitize = [](const std::string &s)->std::string {
            std::string out; out.reserve(s.size());
            for (unsigned char c : s) if (!isspace(c) && c >= 32) out.push_back((char)c);
            return out;
        };
        std::string sid = sanitize(id);
        std::string savail = sanitize(availableVersion);
        try { AppendLog(std::string("IsSkipped: checking id='") + sid + "' avail='" + savail + "' map_size=" + std::to_string((int)m.size()) + "\n"); } catch(...) {}
        // find matching entry in map by sanitizing keys
        for (auto &kv : m) {
            std::string key_s = sanitize(kv.first);
            std::string stored = kv.second;
            std::string stored_s = sanitize(stored);
            if (key_s != sid) continue;
            try { AppendLog(std::string("IsSkipped: found stored='") + stored_s + "' for id='" + key_s + "'\n"); } catch(...) {}
            if (stored_s == savail) {
                try { AppendLog(std::string("IsSkipped: match -> skipping id='") + key_s + "'\n"); } catch(...) {}
                return true;
            }
            if (VersionGreater(savail, stored_s)) {
                try { AppendLog(std::string("IsSkipped: available>") + stored_s + " -> unskipping id='" + key_s + "'\n"); } catch(...) {}
                // remove original key from map and persist
                m.erase(kv.first);
                SaveSkippedMap(m);
                return false;
            }
            try { AppendLog(std::string("IsSkipped: available<stored -> still skip id='") + key_s + "'\n"); } catch(...) {}
            return true;
        }
        try { AppendLog(std::string("IsSkipped: id not found in skipped map: '") + sid + "'\n"); } catch(...) {}
        return false;
    } catch(...) { return false; }
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
                
                // If g_packages is empty, parse from in-memory winget output
                {
                    std::lock_guard<std::mutex> lk(g_packages_mutex);
                    if (g_packages.empty()) {
                        AppendLog("AppendSkippedRaw: g_packages empty, using in-memory winget output\n");
                    }
                }
                
                try {
                    // Get in-memory winget output
                    std::string raw;
                    {
                        std::lock_guard<std::mutex> lk2(g_last_winget_raw_mutex);
                        raw = g_last_winget_raw;
                    }
                    
                    if (!raw.empty()) {
                        ParseWingetTextForPackages(raw);
                        AppendLog(std::string("AppendSkippedRaw: ParseWingetTextForPackages populated g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
                    } else {
                        AppendLog("AppendSkippedRaw: ERROR - g_last_winget_raw is empty!\n");
                    }
                } catch(...) { AppendLog("AppendSkippedRaw: in-memory parse threw\n"); }

                // search g_packages for matching display name (exact or case-insensitive/substring)
                try {
                    std::string idFound;
                    // quick canonical map for known display names -> ids
                    auto GetCanonicalIdForName = [](const std::string &name)->std::string {
                        std::string nl = name; for (auto &c : nl) c = (char)tolower((unsigned char)c);
                        // entries: substring match -> id
                        const std::vector<std::pair<std::string,std::string>> canon = {
                            {"vulkan sdk", "KhronosGroup.VulkanSDK"},
                            {"khronos vulkan", "KhronosGroup.VulkanSDK"}
                        };
                        for (auto &p : canon) if (nl.find(p.first) != std::string::npos) return p.second;
                        return std::string();
                    };

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
                    // check canonical map first
                    try {
                        std::string canon = GetCanonicalIdForName(ident_stripped);
                        if (!canon.empty()) idFound = canon;
                    } catch(...) {}
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
                    if (!idFound.empty()) {
                        AppendLog(std::string("AppendSkippedRaw: resolved '") + identifier + "' -> id='" + idFound + "'\n");
                        writeId = idFound;
                    } else {
                        AppendLog(std::string("AppendSkippedRaw: FAILED to resolve id for '") + identifier + "' from in-memory data\n");
                        // Log what we have in g_packages for debugging
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        AppendLog(std::string("AppendSkippedRaw: g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
                        for (size_t i = 0; i < std::min((size_t)10, g_packages.size()); ++i) {
                            AppendLog(std::string("  [") + std::to_string((int)i) + "] id='" + g_packages[i].first + "' name='" + g_packages[i].second + "'\n");
                        }
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
        // Validate: block writes if ID contains spaces (indicates resolution failed)
        if (writeIdForFile.find(' ') != std::string::npos) {
            AppendLog(std::string("AppendSkippedRaw: BLOCKED write - ID contains spaces: '") + writeIdForFile + "'\n");
            // Build detailed error message showing what we found
            std::string msg = "Failed to skip package.\n\n";
            msg += "Searching for: " + identifier + "\n";
            msg += "Found ID: " + writeIdForFile + "\n\n";
            
            // Show what's in g_packages
            {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                msg += "Available packages in memory (" + std::to_string((int)g_packages.size()) + "):\n";
                for (size_t i = 0; i < std::min((size_t)5, g_packages.size()); ++i) {
                    msg += "  " + g_packages[i].first + " - " + g_packages[i].second + "\n";
                }
                if (g_packages.size() > 5) {
                    msg += "  ... and " + std::to_string((int)g_packages.size() - 5) + " more\n";
                }
                if (g_packages.empty()) {
                    msg += "  (empty - list not yet loaded)\n";
                }
            }
            
            msg += "\nPlease refresh the list and try again.";
            MessageBoxA(NULL, msg.c_str(), "WinUpdate - Skip Failed", MB_OK | MB_ICONWARNING);
            return false;
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
