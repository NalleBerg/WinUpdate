#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "resource.h"
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <regex>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <future>
#include <unordered_set>
#include <filesystem>
#include "About.h"
#include "Config.h"
#include "logging.h"
#include "hyperlink.h"
#include "skip_update.h"
#include "unskip.h"
#include "hidden_scan.h"
#include "system_tray.h"
#include "ctrlw.h"
#include "src/install_dialog.h"
#include "src/startup_manager.h"
#include "src/exclude.h"
// detect nlohmann/json.hpp if available; fall back to ad-hoc parser otherwise
#if defined(__has_include)
#  if __has_include(<nlohmann/json.hpp>)
#    include <nlohmann/json.hpp>
#    define HAVE_NLOHMANN_JSON 1
#  else
#    define HAVE_NLOHMANN_JSON 0
#  endif
#else
#  define HAVE_NLOHMANN_JSON 0
#endif

// Forward-declare cleanup so we can run it very early (remove stale wup_install_*.txt files)
static void CleanupStaleInstallFiles();

// Run cleanup as early as possible (before wWinMain) to ensure no stale temp files remain.
static struct WUPEarlyCleaner {
    WUPEarlyCleaner() {
        try { CleanupStaleInstallFiles(); } catch(...) {}
    }
} g_wup_early_cleaner;

const wchar_t CLASS_NAME[] = L"WinUpdateClass";

// control IDs
#define IDC_RADIO_SHOW 1001
#define IDC_RADIO_ALL  1002
#define IDC_BTN_REFRESH 1003
#define IDC_LISTVIEW 1004
#define IDC_CHECK_SELECTALL 2001  // Old ID, deprecated
#define IDC_BTN_SELECTALL 2099  // New Select All button
#define IDC_BTN_UPGRADE 2002
#define IDC_BTN_DONE 2003
#define IDC_CHECK_SKIPSELECTED 2004
#define IDC_BTN_ABOUT 2005
#define IDC_BTN_CONFIG 2006
#define IDC_BTN_UNSKIP 2007
// IDC_BTN_PASTE removed: app will auto-scan winget at startup/refresh
#define IDC_COMBO_LANG 3001

#define WM_REFRESH_ASYNC (WM_APP + 1)
#define WM_REFRESH_DONE  (WM_APP + 2)
#define WM_INSTALL_DONE  (WM_APP + 5)
#define WM_SHOW_FROM_SECOND_INSTANCE (WM_APP + 10)

// Forward declarations for functions defined later
static std::pair<int,std::string> RunProcessCaptureExitCode(const std::wstring &cmd, int timeoutMs);
static void ParseWingetTextForPackages(const std::string &text);
static std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text);
static void ParseUpgradeFast(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet);
static void ExtractUpdatesFromText(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet);
static void ParseWingetUpgradeTableForUpdates(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet);
static std::unordered_map<std::string,std::string> MapAvailableVersions();
static std::unordered_map<std::string,std::string> MapInstalledVersions();
static std::vector<std::pair<std::string,std::string>> ParseRawWingetTextInMemory(const std::string &text);

static std::unordered_map<std::string,std::string> MapInstalledVersions();


// Globals (non-static for cross-file access - defined in src/globals.cpp)
extern std::vector<std::pair<std::string,std::string>> g_packages;
extern std::mutex g_packages_mutex;
extern std::string g_last_winget_raw;
extern std::mutex g_last_winget_raw_mutex;
extern std::atomic<bool> g_refresh_in_progress;
static std::set<std::string> g_not_applicable_ids;
// per-locale skipped versions: id -> version
static std::unordered_map<std::string,std::string> g_skipped_versions;
// excluded apps: id -> reason ("auto" or "manual")
std::unordered_map<std::string,std::string> g_excluded_apps;
HANDLE g_excluded_mutex = CreateMutex(NULL, FALSE, NULL);
static std::atomic<int> g_total_winget_packages{11107}; // Updated during each scan
static HFONT g_hListFont = NULL;
static std::vector<std::wstring> g_colHeaders;
static std::unordered_map<std::string,std::string> g_last_avail_versions;
static std::unordered_map<std::string,std::string> g_last_inst_versions;
static std::mutex g_versions_mutex;
// Startup snapshot maps (preserve the first successful scan results)
static std::unordered_map<std::string,std::string> g_startup_avail_versions;
static std::unordered_map<std::string,std::string> g_startup_inst_versions;
static std::mutex g_startup_versions_mutex;
static std::wstring g_last_install_outfile;
static HWND g_hTitle = NULL;
static HWND g_hLastUpdated = NULL;
static HFONT g_hTitleFont = NULL;
static HFONT g_hLastUpdatedFont = NULL;
static HWND g_hLoadingPopup = NULL;
static HWND g_hLoadingIcon = NULL;
static HWND g_hLoadingText = NULL;
static HWND g_hLoadingDesc = NULL;
static HWND g_hLoadingDots = NULL;
static HFONT g_hDotsFont = NULL;
HWND g_hMainWindow = NULL;  // Non-static for system tray access
static HWND g_hInstallAnim = NULL;
static HWND g_hInstallPanel = NULL;
static int g_install_anim_state = 0;
static std::atomic<bool> g_install_block_destroy{false};
static int g_loading_anim_state = 0;
static const UINT LOADING_TIMER_ID = 0xC0DE;
static bool g_popupClassRegistered = false;

// Simple i18n: UTF-8 key=value loader. Keys are ASCII.
static std::unordered_map<std::string,std::string> g_i18n_default;
static std::unordered_map<std::string,std::string> g_i18n;
static std::string g_locale = "en_GB";

// forward declare helper functions used by i18n loader (defined later)
static std::string ReadFileUtf8(const std::wstring &path);
static std::wstring Utf8ToWide(const std::string &s);
static std::string WideToUtf8(const std::wstring &w);

// Return cached available versions if present, otherwise probe and cache result.
static std::unordered_map<std::string,std::string> GetAvailableVersionsCached() {
    {
        std::lock_guard<std::mutex> lk(g_versions_mutex);
        if (!g_last_avail_versions.empty()) return g_last_avail_versions;
    }
    auto m = MapAvailableVersions();
    {
        std::lock_guard<std::mutex> lk(g_versions_mutex);
        g_last_avail_versions = m;
    }
    // (startup capture intentionally handled by the async scan code path)
    return m;
}

static std::unordered_map<std::string,std::string> GetInstalledVersionsCached() {
    {
        std::lock_guard<std::mutex> lk(g_versions_mutex);
        if (!g_last_inst_versions.empty()) return g_last_inst_versions;
    }
    auto m = MapInstalledVersions();
    {
        std::lock_guard<std::mutex> lk(g_versions_mutex);
        g_last_inst_versions = m;
    }
    // (startup capture intentionally handled by the async scan code path)
    return m;
}

// Parse raw winget output in memory by trying multiple parsers (fast -> tolerant -> table)
static std::vector<std::pair<std::string,std::string>> ParseRawWingetTextInMemory(const std::string &text) {
    std::set<std::pair<std::string,std::string>> found;
    if (text.empty()) return {};
    ParseUpgradeFast(text, found);
    if (found.empty()) ExtractUpdatesFromText(text, found);
    if (found.empty()) {
        std::set<std::pair<std::string,std::string>> tmp;
        ParseWingetUpgradeTableForUpdates(text, tmp);
        for (auto &p : tmp) found.insert(p);
    }
    std::vector<std::pair<std::string,std::string>> out;
    for (auto &p : found) out.emplace_back(p.first, p.second);
    return out;
}

// Load/Save per-locale skip config in locale/<locale>.ini with lines: skip=Id|Version
static void LoadSkipConfig(const std::string &locale) {
    g_skipped_versions.clear();
    try {
        std::string fn = std::string("locale/") + locale + ".ini";
        std::ifstream ifs(fn, std::ios::binary);
        if (!ifs) return;
        std::string ln;
        while (std::getline(ifs, ln)) {
            // trim
            auto ltrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); };
            auto rtrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
            ltrim(ln); rtrim(ln);
            if (ln.empty() || ln[0]=='#' || ln[0]==';') continue;
            size_t eq = ln.find('=');
            if (eq == std::string::npos) continue;
            std::string key = ln.substr(0, eq);
            std::string val = ln.substr(eq+1);
            ltrim(key); rtrim(key); ltrim(val); rtrim(val);
            if (key == "skip") {
                size_t p = val.find('|');
                if (p != std::string::npos) {
                    std::string id = val.substr(0,p);
                    std::string ver = val.substr(p+1);
                    if (!id.empty() && !ver.empty()) g_skipped_versions[id] = ver;
                }
            }
        }
    } catch(...) {}
}

static void SaveSkipConfig(const std::string &locale) {
    try {
        std::string fn = std::string("locale\\") + locale + ".ini";
        std::ofstream ofs(fn + ".tmp", std::ios::binary | std::ios::trunc);
        if (!ofs) return;
        for (auto &p : g_skipped_versions) {
            ofs << "skip=" << p.first << "|" << p.second << "\n";
        }
        ofs.close();
        // replace file
        std::remove(fn.c_str());
        std::rename((fn + ".tmp").c_str(), fn.c_str());
    } catch(...) {}
}

static std::unordered_map<std::string,std::string> MapInstalledVersions() {
    std::unordered_map<std::string,std::string> out;
    try {
        std::vector<int> attempts = {2400, 2400};
        std::string txt;
        for (int t : attempts) {
            auto r = RunProcessCaptureExitCode(L"winget list --accept-source-agreements --accept-package-agreements --output json", t);
            txt = r.second;
            if (!txt.empty()) break;
        }
        if (!txt.empty()) {
#if HAVE_NLOHMANN_JSON
            try {
                auto j = nlohmann::json::parse(txt);
                std::function<void(const nlohmann::json&)> visit;
                visit = [&](const nlohmann::json &node) {
                    if (node.is_object()) {
                        std::string id; std::string ver;
                        if (node.contains("Id") && node["Id"].is_string()) id = node["Id"].get<std::string>();
                        if (node.contains("InstalledVersion") && node["InstalledVersion"].is_string()) ver = node["InstalledVersion"].get<std::string>();
                        if (node.contains("Version") && node["Version"].is_string()) ver = node["Version"].get<std::string>();
                        if (!id.empty() && !ver.empty()) out[id] = ver;
                        for (auto it = node.begin(); it != node.end(); ++it) visit(it.value());
                    } else if (node.is_array()) {
                        for (auto &el : node) visit(el);
                    }
                };
                visit(j);
                if (!out.empty()) return out;
            } catch(...) { }
#endif
        }
        // Try to parse aligned table output (header + separator) like:
        // Name                                   Id                       Version
        // ------------------------------------   ---------------------    --------
        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) {
            while (!ln.empty() && (ln.back()=='\r' || ln.back()=='\n')) ln.pop_back();
            lines.push_back(ln);
        }
        int headerIdx = -1, sepIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("----") != std::string::npos) { sepIdx = i; break; }
        }
        if (sepIdx > 0) headerIdx = sepIdx - 1;
        auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
        if (headerIdx >= 0) {
            std::string header = lines[headerIdx];
            // find column starts for Name, Id, Version
            std::vector<std::string> colNames = {"Name", "Id", "Version"};
            std::vector<int> colStarts;
            for (auto &cn : colNames) {
                size_t p = header.find(cn);
                if (p != std::string::npos) colStarts.push_back((int)p);
            }
            if (colStarts.size() >= 2) {
                int ncols = (int)colStarts.size();
                for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                    const std::string &sline = lines[i];
                    if (trim(sline).empty()) continue;
                    if (sline.find("upgraded") != std::string::npos) break;
                    auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
                        int len = (int)s.size();
                        if (a >= len) return std::string();
                        int end = std::min(len, b);
                        return s.substr(a, end - a);
                    };
                    std::vector<std::string> fields(ncols);
                    for (int c = 0; c < ncols; ++c) {
                        int a = colStarts[c];
                        int b = (c+1 < ncols) ? colStarts[c+1] : (int)sline.size();
                        fields[c] = trim(substrSafe(sline, a, b));
                    }
                    std::string id = (ncols > 1) ? fields[1] : std::string();
                    std::string inst = (ncols > 2) ? fields[2] : std::string();
                    if (!id.empty()) out[id] = inst;
                }
                return out;
            }
        }
        // Fallback: token-based heuristic
        std::istringstream iss2(txt);
        while (std::getline(iss2, ln)) {
            if (ln.find("----") != std::string::npos) continue;
            if (ln.find("Name") != std::string::npos && ln.find("Id") != std::string::npos) continue;
            // trim
            auto trim2 = [](std::string &s){ while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back(); while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
            trim2(ln);
            if (ln.empty()) continue;
            std::istringstream ls(ln);
            std::vector<std::string> toks;
            std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() < 2) continue;
            std::unordered_set<std::string> knownIds;
            {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                for (auto &p : g_packages) knownIds.insert(p.first);
            }
            std::string id;
            for (auto &t : toks) {
                if (knownIds.find(t) != knownIds.end()) { id = t; break; }
            }
            if (id.empty()) {
                // assume last token is version, second-last is id
                if (toks.size() >= 2) {
                    id = toks[toks.size()-2];
                } else id = toks.front();
            }
            std::string inst = toks.back();
            out[id] = inst;
        }
    } catch(...) {}
    return out;
}

// Map id -> available version by parsing `winget upgrade` table quickly
static std::unordered_map<std::string,std::string> MapAvailableVersions() {
    std::unordered_map<std::string,std::string> out;
    try {
        std::vector<int> attempts = {2400, 2400};
        std::string txt;
        for (int t : attempts) {
            auto r = RunProcessCaptureExitCode(L"winget upgrade --accept-source-agreements --accept-package-agreements --output json", t);
            txt = r.second;
            if (!txt.empty()) break;
        }
        if (!txt.empty()) {
#if HAVE_NLOHMANN_JSON
            try {
                auto j = nlohmann::json::parse(txt);
                std::function<void(const nlohmann::json&)> visit;
                visit = [&](const nlohmann::json &node) {
                    if (node.is_object()) {
                        std::string id; std::string ver;
                        if (node.contains("Id") && node["Id"].is_string()) id = node["Id"].get<std::string>();
                        if (node.contains("AvailableVersion") && node["AvailableVersion"].is_string()) ver = node["AvailableVersion"].get<std::string>();
                        if (node.contains("Available") && node["Available"].is_string()) ver = node["Available"].get<std::string>();
                        if (node.contains("Version") && node["Version"].is_string()) ver = node["Version"].get<std::string>();
                        if (!id.empty() && !ver.empty()) out[id] = ver;
                        for (auto it = node.begin(); it != node.end(); ++it) visit(it.value());
                    } else if (node.is_array()) {
                        for (auto &el : node) visit(el);
                    }
                };
                visit(j);
                if (!out.empty()) return out;
            } catch(...) { }
#endif
        }
        // Prefer parsing the aligned table output (header + separator)
        std::istringstream iss(txt);
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(iss, ln)) {
            while (!ln.empty() && (ln.back()=='\r' || ln.back()=='\n')) ln.pop_back();
            lines.push_back(ln);
        }
        int headerIdx = -1, sepIdx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("----") != std::string::npos) { sepIdx = i; break; }
        }
        if (sepIdx > 0) headerIdx = sepIdx - 1;
        auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
        if (headerIdx >= 0) {
            std::string header = lines[headerIdx];
            std::vector<std::string> colNames = {"Name","Id","Version","Available"};
            std::vector<int> colStarts;
            for (auto &cn : colNames) {
                size_t p = header.find(cn);
                if (p != std::string::npos) colStarts.push_back((int)p);
            }
            if (colStarts.size() >= 2) {
                int ncols = (int)colStarts.size();
                for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
                    const std::string &sline = lines[i];
                    if (trim(sline).empty()) continue;
                    if (sline.find("upgrades available") != std::string::npos) break;
                    auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
                        int len = (int)s.size();
                        if (a >= len) return std::string();
                        int end = std::min(len, b);
                        return s.substr(a, end - a);
                    };
                    std::vector<std::string> fields(ncols);
                    for (int c = 0; c < ncols; ++c) {
                        int a = colStarts[c];
                        int b = (c+1 < ncols) ? colStarts[c+1] : (int)sline.size();
                        fields[c] = trim(substrSafe(sline, a, b));
                    }
                    std::string id = (ncols > 1) ? fields[1] : std::string();
                    std::string available = (ncols > 3) ? fields[3] : std::string();
                    if (!id.empty()) out[id] = available;
                }
                return out;
            }
        }
        // Fallback token-based parsing
        std::istringstream iss2(txt);
        while (std::getline(iss2, ln)) {
            if (ln.find("----") != std::string::npos) continue;
            if (ln.find("Name") != std::string::npos && ln.find("Id") != std::string::npos) continue;
            auto trim2 = [](std::string &s){ while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back(); while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
            trim2(ln);
            if (ln.empty()) continue;
            std::istringstream ls(ln);
            std::vector<std::string> toks;
            std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.empty()) continue;
            std::unordered_set<std::string> knownIds;
            {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                for (auto &p : g_packages) knownIds.insert(p.first);
            }
            std::string id;
            for (auto &t : toks) {
                if (knownIds.find(t) != knownIds.end()) { id = t; break; }
            }
            if (id.empty()) {
                if (toks.size() >= 2) id = toks[toks.size()-2]; else id = toks.front();
            }
            std::string available = toks.back();
            if (!id.empty()) out[id] = available;
        }
    } catch(...) {}
    return out;
}

static void InitDefaultTranslations() {
    if (!g_i18n_default.empty()) return;
    g_i18n_default["app_window_title"] = "WinUpdate - winget GUI updater";
    g_i18n_default["app_title"] = "WinUpdate";
    g_i18n_default["list_last_updated_prefix"] = "List last updated:";
    g_i18n_default["select_all"] = "Select all";
    g_i18n_default["upgrade_now"] = "Install updates";
    g_i18n_default["refresh"] = "Refresh";
    g_i18n_default["lang_changed"] = "Language changed to English (UK)";
    g_i18n_default["package_col"] = "Package";
    g_i18n_default["id_col"] = "Id";
    g_i18n_default["loading_title"] = "Loading, please";
    g_i18n_default["loading_desc"] = "Querying winget — application will start when the scan completes";
    g_i18n_default["installing_label"] = "Installing update";
    g_i18n_default["your_system_updated"] = "Your system is updated!";
    g_i18n_default["your_system_updated"] = "Your system is up to date";
    g_i18n_default["msg_error_elevate"] = "Failed to launch elevated process.";
}

static void LoadLocaleFromFile(const std::string &locale) {
    g_i18n = g_i18n_default; // start with defaults
    std::string path = std::string("locale\\") + locale + ".txt";
    std::string txt = ReadFileUtf8(std::wstring(path.begin(), path.end()));
    if (txt.empty()) return;
    std::istringstream iss(txt);
    std::string ln;
    while (std::getline(iss, ln)) {
        // Trim
        auto ltrim = [](std::string &s){ while(!s.empty() && (s.front()==' '||s.front()=='\t' || s.front()=='\r')) s.erase(s.begin()); };
        auto rtrim = [](std::string &s){ while(!s.empty() && (s.back()==' '||s.back()=='\t' || s.back()=='\r' || s.back()=='\n')) s.pop_back(); };
        ltrim(ln); rtrim(ln);
        if (ln.empty()) continue;
        if (ln[0] == '#' || ln[0] == ';') continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ln.substr(0, eq);
        std::string val = ln.substr(eq+1);
        ltrim(key); rtrim(key); ltrim(val); rtrim(val);
        if (!key.empty()) g_i18n[key] = val;
    }
}

std::wstring t(const char *key) {
    InitDefaultTranslations();
    std::string k(key);
    auto it = g_i18n.find(k);
    if (it == g_i18n.end()) it = g_i18n_default.find(k);
    if (it == g_i18n_default.end()) return Utf8ToWide(k);
    return Utf8ToWide(it->second);
}

// Settings persistence: save to %APPDATA%\WinUpdate\wup_settings.ini
static bool SaveLocaleSetting(const std::string &locale) {
    try {
        char buf[MAX_PATH];
        DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return false;
        std::string path = std::string(buf) + "\\WinUpdate";
        // ensure directory exists
        CreateDirectoryA(path.c_str(), NULL);
        std::string ini = path + "\\wup_settings.ini";
        
        // Read existing content to preserve other sections
        std::vector<std::string> lines;
        std::ifstream ifs(ini, std::ios::binary);
        if (ifs) {
            std::string ln;
            while (std::getline(ifs, ln)) lines.push_back(ln);
            ifs.close();
        }
        
        // Update [language] section
        bool foundLangSection = false;
        bool updatedLang = false;
        std::vector<std::string> newLines;
        for (size_t i = 0; i < lines.size(); i++) {
            std::string ln = lines[i];
            auto trim = [](std::string s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) return std::string(); size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); };
            std::string trimmed = trim(ln);
            if (trimmed == "[language]") {
                foundLangSection = true;
                newLines.push_back(ln);
                // Replace next non-empty line in this section with our locale
                while (i + 1 < lines.size()) {
                    std::string next = lines[i+1];
                    std::string nextTrim = trim(next);
                    if (nextTrim.empty() || nextTrim[0] == '#' || nextTrim[0] == ';') {
                        newLines.push_back(next);
                        i++;
                    } else if (nextTrim[0] == '[') {
                        // Next section, insert our locale before it
                        newLines.push_back(locale);
                        updatedLang = true;
                        break;
                    } else {
                        // This is the language line, replace it
                        newLines.push_back(locale);
                        updatedLang = true;
                        i++;
                        break;
                    }
                }
                if (!updatedLang) {
                    newLines.push_back(locale);
                    updatedLang = true;
                }
            } else {
                newLines.push_back(ln);
            }
        }
        
        // If [language] section not found, add it at the beginning
        if (!foundLangSection) {
            newLines.insert(newLines.begin(), locale);
            newLines.insert(newLines.begin(), "[language]");
        }
        
        // Write back
        std::ofstream ofs(ini, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        for (const auto &ln : newLines) ofs << ln << "\n";
        return true;
    } catch(...) { return false; }
}

static std::string LoadLocaleSetting() {
    try {
        char buf[MAX_PATH];
        DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return std::string();
        std::string ini = std::string(buf) + "\\WinUpdate\\wup_settings.ini";
        
        std::ifstream ifs(ini, std::ios::binary);
        if (!ifs) return std::string();
        
        auto trim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
        std::string ln;
        bool inLang = false;
        while (std::getline(ifs, ln)) {
            trim(ln);
            if (ln.empty()) continue;
            if (ln[0] == '#' || ln[0] == ';') continue;
            if (ln[0] == '[') {
                inLang = (ln == "[language]");
                continue;
            }
            if (inLang) return ln; // Return full locale code (en_GB, nb_NO, sv_SE)
        }
    } catch(...) {}
    return std::string();
}

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK AnimSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static void TryForceForegroundWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    // Try common approaches to bring window to foreground
    ::ShowWindow(hwnd, SW_RESTORE);
    ::BringWindowToTop(hwnd);
    ::SetForegroundWindow(hwnd);
    ::SetActiveWindow(hwnd);
    // Attach thread input to foreground thread as a fallback
    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    DWORD thisThread = GetCurrentThreadId();
    if (fgThread != 0 && fgThread != thisThread) {
        AttachThreadInput(thisThread, fgThread, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        AttachThreadInput(thisThread, fgThread, FALSE);
    }
}
// subclass procedure for custom-drawn dots control
static LRESULT CALLBACK DotsSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // fill background with dialog face
        HBRUSH hbr = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rc, hbr);
        // determine number of dots from global animation state
        int state = g_loading_anim_state % 3;
        int count = (state == 0) ? 1 : (state == 1) ? 3 : 5;
        // compute diameter and spacing (smaller, more compact)
        int ch = rc.bottom - rc.top;
        int dia = std::min(12, std::max(6, ch - 12));
        int gap = dia / 2; // tighter spacing
        int totalW = count * dia + (count - 1) * gap;
        int startX = rc.left + ((rc.right - rc.left) - totalW) / 2;
        int y = rc.top + (rc.bottom - rc.top - dia) / 2;
        // paint filled navy circles (select pen+brush)
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0,0,128));
        HBRUSH hBrush = CreateSolidBrush(RGB(0,0,128));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
        for (int i = 0; i < count; ++i) {
            int x = startX + i * (dia + gap);
            Ellipse(hdc, x, y, x + dia, y + dia);
        }
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(hPen);
        DeleteObject(hBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        RemoveWindowSubclass(hwnd, DotsSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
static void EnsurePopupClassRegistered(HINSTANCE hInst) {
    if (g_popupClassRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PopupWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"WUPopupClass";
    RegisterClassExW(&wc);
    g_popupClassRegistered = true;
}

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // start animation timer on popup (175ms per user preference)
        g_loading_anim_state = 0;
        SetTimer(hwnd, LOADING_TIMER_ID, 175, NULL);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == LOADING_TIMER_ID) {
            g_loading_anim_state = (g_loading_anim_state + 1) % 3; // cycle 0..2
            InvalidateRect(hwnd, NULL, TRUE); // full repaint (double-buffered)
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // double-buffering
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);

        // fill background with dialog face color
        HBRUSH hbrFace = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdcMem, &rc, hbrFace);

        // draw a simple frame to mimic dialog border
        HBRUSH hbrFrame = GetSysColorBrush(COLOR_WINDOWFRAME);
        RECT fr = rc;
        FrameRect(hdcMem, &fr, hbrFrame);

        // layout: left icon cell, right title/desc cell (row1); row2 merged for dots
        int padding = 12;
        int iconW = 48; int iconH = 48;
        int ix = padding;
        int iy = padding;
        // draw information icon using standard system icon
        HICON hInfo = LoadIcon(NULL, IDI_INFORMATION);
        if (hInfo) DrawIconEx(hdcMem, ix, iy, hInfo, iconW, iconH, 0, NULL, DI_NORMAL);

        int txtX = ix + iconW + padding;
        int txtW = w - txtX - padding;
        int row1Height = iconH + padding; // provide vertical space for icon + text
        // Title centered across full dialog width
        RECT titleRect = { rc.left + padding, iy, rc.right - padding, iy + row1Height / 2 };
        // Description centered across full dialog width (not just text area)
        RECT descRect = { rc.left + padding, iy + row1Height / 2, rc.right - padding, iy + row1Height };

        // draw title using existing title font if available (preserves ClearType)
        if (g_hTitleFont) {
            HGDIOBJ oldFont = SelectObject(hdcMem, g_hTitleFont);
            SetTextColor(hdcMem, RGB(0,0,0));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_title").c_str(), -1, &titleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(hdcMem, oldFont);
        } else {
            SetTextColor(hdcMem, RGB(0,0,0));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_title").c_str(), -1, &titleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        // draw description smaller
        if (g_hLastUpdatedFont) {
            HGDIOBJ oldFont = SelectObject(hdcMem, g_hLastUpdatedFont);
            SetTextColor(hdcMem, RGB(64,64,64));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_desc").c_str(), -1, &descRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(hdcMem, oldFont);
        } else {
            SetTextColor(hdcMem, RGB(64,64,64));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_desc").c_str(), -1, &descRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        // Row2: center dots across full width (merged cell)
        int row2Top = iy + row1Height + padding/2;
        int row2H = h - row2Top - padding;
        RECT dotsArea = { rc.left + padding, row2Top, rc.right - padding, row2Top + row2H };

        int state = g_loading_anim_state % 3;
        int count = (state == 0) ? 1 : (state == 1) ? 3 : 5;
        int dia = std::min(14, std::max(6, row2H - 8));
        int gap = dia / 2;
        int totalW = count * dia + (count - 1) * gap;
        int centerX = (dotsArea.left + dotsArea.right) / 2;
        int startX = centerX - totalW / 2;
        int y = dotsArea.top + (row2H - dia) / 2;

        // draw navy filled circles
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0,0,128));
        HBRUSH hBrush = CreateSolidBrush(RGB(0,0,128));
        HGDIOBJ oldPen = SelectObject(hdcMem, hPen);
        HGDIOBJ oldBrush = SelectObject(hdcMem, hBrush);
        for (int i = 0; i < count; ++i) {
            int x = startX + i * (dia + gap);
            Ellipse(hdcMem, x, y, x + dia, y + dia);
        }
        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(hPen);
        DeleteObject(hBrush);

        // blit buffer to screen
        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        // cleanup
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, LOADING_TIMER_ID);
        return 0;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
static LRESULT CALLBACK AnimSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_TIMER: {
        g_install_anim_state++;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int dots = 5;
        int dotW = std::min(12, std::max(6, h - 6));
        int gap = 6; // tight gaps between dots to form a snake-like look
        int step = 6; // pixels to advance per timer tick
        int cycle = w + dotW + gap;
        int offset = (g_install_anim_state * step) % cycle;
        HBRUSH fill = CreateSolidBrush(RGB(0, 102, 204));
        HGDIOBJ old = SelectObject(hdc, fill);
        // Do NOT clear the background - draw only the rounded dots so underlying progress bar remains visible
        for (int i = 0; i < dots; ++i) {
            int rawx = offset + i * (dotW + gap) - (dotW + gap);
            int x = ((rawx % cycle) + cycle) % cycle; // normalize into [0,cycle)
            if (x > w) x -= cycle; // map into [-dotW, w]
            if (x < -dotW || x > w) continue;
            RECT r = { rc.left + x, rc.top + 2, rc.left + x + dotW, rc.top + 2 + dotW };
            RoundRect(hdc, r.left, r.top, r.right, r.bottom, 4, 4);
        }
        SelectObject(hdc, old);
        DeleteObject(fill);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, AnimSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}


// Run a command, capture stdout/stderr to a temp file, return exit code and UTF-8 output.
static std::pair<int,std::string> RunProcessCaptureExitCode(const std::wstring &cmd, int timeoutMs) {
    // Launch the given command line and capture stdout+stderr via pipes (no temp files).
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
    // copy command into writable buffer for CreateProcess
    std::wstring cmdCopy = cmd;
    BOOL ok = CreateProcessW(NULL, &cmdCopy[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    // close write end in parent regardless
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return result;
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.first = -2; // timeout sentinel
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

    // append to run log for debugging
    {
        std::ostringstream oss;
        oss << "--- CMD: " << WideToUtf8(cmd) << " ---\n";
        oss << "Exit: " << result.first << "\n";
        if (result.first == -2) oss << "(TIMEOUT)\n";
        oss << "Output:\n" << output << "\n\n";
        AppendLog(oss.str());
    }

    result.second = output;
    return result;
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string ReadFileUtf8(const std::wstring &path) {
    std::string narrow = WideToUtf8(path);
    std::ifstream ifs(narrow, std::ios::binary);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::wstring GetTimestampNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, _countof(buf), L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

static void UpdateLastUpdatedLabel(HWND hwnd) {
    if (!g_hLastUpdated) return;
    std::wstring ts = GetTimestampNow();
    std::wstring prefix = t("list_last_updated_prefix");
    std::wstring txt = prefix + L" " + ts;
    SetWindowTextW(g_hLastUpdated, txt.c_str());
}

static void ShowLoading(HWND parent) {
    if (!parent) return;
    // Don't show loading popup if window is hidden (e.g., in system tray mode)
    if (!IsWindowVisible(parent)) return;
    if (g_hLoadingPopup && IsWindow(g_hLoadingPopup)) return;
    RECT rc;
    GetWindowRect(parent, &rc);
    int pw = rc.right - rc.left;
    int ph = rc.bottom - rc.top;
    int w = 340; int h = 120;
    int x = rc.left + (pw - w) / 2;
    int y = rc.top + (ph - h) / 2;
    // Create a popup window that looks like an informational dialog
    // Create an owned border-style popup (no title or close button)
    HINSTANCE hInst = GetModuleHandleW(NULL);
    // ensure our popup window class is registered and create a top-level border popup
    EnsurePopupClassRegistered(hInst);
    // create a top-level border popup (no caption/title bar) positioned centered over parent
    g_hLoadingPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"WUPopupClass", NULL,
        WS_POPUP | WS_BORDER | WS_VISIBLE, x, y, w, h, NULL, NULL, hInst, NULL);
    if (g_hLoadingPopup) {
        // Owner-drawn popup: the window procedure paints icon, title/desc and centered dots.
        ShowWindow(g_hLoadingPopup, SW_SHOW);
        UpdateWindow(g_hLoadingPopup);
        // WM_CREATE handler of the popup will start the 175ms timer.
    }
}

static void HideLoading() {
    if (g_hLoadingPopup && IsWindow(g_hLoadingPopup)) {
        DestroyWindow(g_hLoadingPopup);
        g_hLoadingPopup = NULL;
        g_hLoadingIcon = NULL;
        g_hLoadingText = NULL;
        g_hLoadingDesc = NULL;
        g_hLoadingDots = NULL;
    }
}

// Query the system for the installed version of a package by id using `winget list`.
// (No per-id installed-version helper in this restore)

static std::string RunWingetElevatedCaptureJson(HWND hwnd) {
    wchar_t tmpPathBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPathBuf);
    unsigned long long uniq = GetTickCount64();
    std::wstring batch = std::wstring(tmpPathBuf) + L"winget_run_" + std::to_wstring(uniq) + L".bat";
    std::wstring outfn = std::wstring(tmpPathBuf) + L"winget_out_" + std::to_wstring(uniq) + L".txt";
    // write batch that redirects output to outfn
    std::string nbatch = WideToUtf8(batch);
    std::string nout = WideToUtf8(outfn);
    std::ofstream ofs(nbatch, std::ios::binary);
    ofs << "@echo off\r\n";
    ofs << "winget upgrade --accept-source-agreements --accept-package-agreements > \"" << nout << "\" 2>&1\r\n";
    ofs.close();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    std::wstring params = L"/C \"" + batch + L"\"";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE; // hide elevated console window
    if (!ShellExecuteExW(&sei)) {
        return std::string();
    }
    DWORD wait = WaitForSingleObject(sei.hProcess, 15000); // 15s timeout for elevated capture
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(sei.hProcess, 1);
    }
    CloseHandle(sei.hProcess);
    std::string out = ReadFileUtf8(outfn);
    // cleanup temp files
    DeleteFileW(batch.c_str());
    DeleteFileW(outfn.c_str());
    return out;
}

// very small JSON-ish extractor for "Id" and "Name" values from winget --output json
static void ParseWingetJsonForPackages(const std::string &jsonText) {
    // Winget JSON is unreliable; treat incoming JSON-like text as raw/table output
    ParseWingetTextForPackages(jsonText);
}

// Parse human-readable `winget upgrade` text output.
// compare semantic-ish version strings: returns -1 if a<b, 0 if equal, 1 if a>b
static int CompareVersions(const std::string &a, const std::string &b) {
    if (a == b) return 0;
    std::istringstream sa(a), sb(b);
    std::string ta, tb;
    while (true) {
        if (!std::getline(sa, ta, '.')) ta.clear();
        if (!std::getline(sb, tb, '.')) tb.clear();
        if (ta.empty() && tb.empty()) break;
        // try numeric compare
        long va = 0, vb = 0;
        try { va = std::stol(ta.empty()?"0":ta); } catch(...) { va = 0; }
        try { vb = std::stol(tb.empty()?"0":tb); } catch(...) { vb = 0; }
        if (va < vb) return -1;
        if (va > vb) return 1;
        // continue
        if (!sa.good() && !sb.good()) break;
    }
    return 0;
}

// Parse text output and pick only entries where an available version is greater
static void ParseWingetTextForUpdates(const std::string &text) {
    g_packages.clear();
    std::istringstream iss(text);
    std::string line;
    // match lines that end with: <installed-version> <available-version>
    std::regex lineRe("^\\s*(.+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)\\s*$");
    std::smatch m;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        if (line.empty()) continue;
        if (std::regex_match(line, m, lineRe)) {
            // m[1]=name, m[2]=id, m[3]=installed, m[4]=available
            std::string name = m[1].str();
            std::string id = m[2].str();
            std::string installed = m[3].str();
            std::string available = m[4].str();
            if (CompareVersions(installed, available) < 0) {
                g_packages.emplace_back(id, name);
            }
        }
    }
}

// Very fast upgrade output parser: split each non-header line into tokens,
// take the last tokens as id/installed/available and compare versions.
static void ParseUpgradeFast(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    std::istringstream iss(text);
    std::string line;
    bool seenHeader = false;
    std::regex verRe(R"(^[0-9]+(\.[0-9]+)*$)");
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        std::string t = trim(line);
        if (t.empty()) continue;
        if (!seenHeader) {
            if (t.find("Name") != std::string::npos && t.find("Id") != std::string::npos) { seenHeader = true; continue; }
            continue;
        }
        if (t.find("----") != std::string::npos) continue;
        if (t.find("upgrades available") != std::string::npos) break;

        std::istringstream ls(t);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        if (toks.size() < 3) continue;

        int n = (int)toks.size();
        int verIdx2 = -1, verIdx1 = -1;
        for (int i = n - 1; i >= 1; --i) {
            if (std::regex_match(toks[i], verRe) && std::regex_match(toks[i-1], verRe)) { verIdx2 = i; verIdx1 = i-1; break; }
        }
        if (verIdx1 < 0) continue;
        int idIdx = verIdx1 - 1;
        if (idIdx < 0) continue;

        std::string available = toks[verIdx2];
        std::string installed = toks[verIdx1];

        auto looks_like_id = [&](const std::string &s)->bool {
            if (s.find('.') != std::string::npos) return true;
            if (s.size() >= 4) return true;
            for (char c : s) if (isupper((unsigned char)c)) return true;
            return false;
        };

        std::string id = toks[idIdx];
        if (!looks_like_id(id)) {
            int better = -1;
            for (int k = idIdx - 1; k >= 0; --k) { if (looks_like_id(toks[k])) { better = k; break; } }
            if (better >= 0) idIdx = better;
        }

        std::string name;
        for (int i = 0; i < idIdx; ++i) { if (i) name += " "; name += toks[i]; }
        if (name.empty()) name = toks[idIdx];

        id = toks[idIdx];
        if (CompareVersions(installed, available) < 0) outSet.emplace(id, name);
    }
}

// More tolerant extractor: find any occurrences of lines or fragments that contain
// <name> <id> <installed-version> <available-version> and add when available>installed
static void ExtractUpdatesFromText(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    // regex: capture name (greedy), id token (no spaces), installed(ver), available(ver)
    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)");
    std::smatch m;
    std::string::const_iterator it = text.begin();
    while (std::regex_search(it, text.cend(), m, anyRe)) {
        std::string name = m[1].str();
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        // trim name
        auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
        name = trim(name);
        if (!id.empty() && CompareVersions(installed, available) < 0) outSet.emplace(id, name);
        it = m.suffix().first;
    }
}

// Build a map of Id->Name from a full winget listing (Name/Id table)
// then scan the upgrade output for <name> <id> <installed> <available>
// and add entries where available > installed and id exists in the map.
static void FindUpdatesUsingKnownList(const std::string &listText, const std::string &upgradeText, std::set<std::pair<std::string,std::string>> &outSet) {
    // populate g_packages from the listText
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        ParseWingetTextForPackages(listText);
        // copy into local map
    }
    std::unordered_map<std::string,std::string> pkgmap;
    for (auto &p : g_packages) pkgmap[p.first] = p.second;
    // if we didn't get a useful map from the provided list, try extracting Id/Name pairs from the upgrade text
    if (pkgmap.empty() && !upgradeText.empty()) {
        auto extra = ExtractIdsFromNameIdText(upgradeText);
        for (auto &p : extra) pkgmap[p.first] = p.second;
    }
    // clear global helper list to avoid side-effects
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        g_packages.clear();
    }
    if (pkgmap.empty()) return;

    // regex to find fragments like: <name> <id> <installed> <available>
    std::regex anyRe(R"(([\S ]+?)\s+([^\s]+)\s+(\d+(?:\.[0-9]+)*)\s+(\d+(?:\.[0-9]+)*))");
    std::smatch m;
    std::string::const_iterator it = upgradeText.begin();
    while (std::regex_search(it, upgradeText.cend(), m, anyRe)) {
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        if (!id.empty() && pkgmap.count(id) && CompareVersions(installed, available) < 0) {
            outSet.emplace(id, pkgmap[id]);
        }
        it = m.suffix().first;
    }
}

static std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text) {
    std::vector<std::pair<std::string,std::string>> ids;
    std::istringstream iss(text);
    std::string ln;
    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    while (std::getline(iss, ln)) {
        std::string t = trim(ln);
        if (t.empty()) continue;
        // skip header/separator lines that contain dashes or 'Name' header
        if (t.find("----") != std::string::npos) continue;
        if (t.find("Name") != std::string::npos && t.find("Id") != std::string::npos) continue;
        std::istringstream ls(t);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        if (toks.size() >= 2) {
            std::string id = toks.back();
            std::string name;
            for (size_t i = 0; i + 1 < toks.size(); ++i) {
                if (i) name += " ";
                name += toks[i];
            }
            ids.emplace_back(id, name);
        }
    }
    return ids;
}

static void CheckIdsForUpdates(const std::vector<std::pair<std::string,std::string>> &candidates, std::set<std::pair<std::string,std::string>> &outFound, HWND hwnd) {
    // Probe all candidate ids in parallel with limited concurrency to keep it fast.
    unsigned int hw = std::thread::hardware_concurrency();
    size_t concurrency = hw > 0 ? std::min<unsigned int>(hw, 8) : 4;
    std::vector<std::future<std::pair<std::string,std::string>>> futures;
    futures.reserve(candidates.size());

    auto probeOne = [](const std::pair<std::string,std::string> &p)->std::pair<std::string,std::string> {
        std::wstring idw(p.first.begin(), p.first.end());
        std::wstring cmd = L"cmd /C winget upgrade --id \"" + idw + L"\" --accept-source-agreements --accept-package-agreements";
        auto res = RunProcessCaptureExitCode(cmd, 4000);
        std::string out = res.second;
        if (out.empty()) {
            res = RunProcessCaptureExitCode(cmd, 8000);
            out = res.second;
        }
        if (!out.empty()) {
            std::set<std::pair<std::string,std::string>> found;
            ExtractUpdatesFromText(out, found);
            for (auto &f : found) {
                if (f.first == p.first) return f; // return matching id/name
            }
        }
        return std::pair<std::string,std::string>();
    };

    size_t idx = 0;
    while (idx < candidates.size()) {
        // launch up to concurrency tasks
        size_t launched = 0;
        std::vector<std::future<std::pair<std::string,std::string>>> batch;
        for (; idx < candidates.size() && launched < concurrency; ++idx, ++launched) {
            batch.push_back(std::async(std::launch::async, probeOne, candidates[idx]));
        }
        // collect results
        for (auto &f : batch) {
            try {
                auto r = f.get();
                if (!r.first.empty()) outFound.emplace(r.first, r.second);
            } catch(...) {}
        }
        // small pause to avoid hammering winget
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Read the most recent raw winget output file matching prefix wup_winget_raw_*.txt
static std::string ReadMostRecentRawWinget() {
    namespace fs = std::filesystem;
    std::string best;
    std::filesystem::file_time_type bestTime = std::filesystem::file_time_type::min();
    try {
        for (auto &p : fs::directory_iterator(fs::current_path())) {
            std::string name = p.path().filename().string();
            if (name.rfind("wup_winget_raw_", 0) == 0 && p.path().extension() == ".txt") {
                auto ftime = fs::last_write_time(p.path());
                if (ftime > bestTime) {
                    bestTime = ftime;
                    best = p.path().string();
                }
            }
        }
        if (!best.empty()) {
            // read file
            std::ifstream ifs(best, std::ios::binary);
            if (ifs) {
                std::ostringstream ss; ss << ifs.rdbuf();
                return ss.str();
            }
        }
    } catch(...) {}
    return std::string();
}

static void ParseWingetTextForPackages(const std::string &text) {
    g_packages.clear();
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        // trim CR
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        // keep even empty lines (we'll skip later)
        lines.push_back(line);
    }
    if (lines.empty()) return;

    // find header line (contains Name and Id) and separator (----)
    int headerIdx = -1;
    int sepIdx = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (lines[i].find("----") != std::string::npos) {
            sepIdx = i;
            break;
        }
    }
    if (sepIdx <= 0) return;
    headerIdx = sepIdx - 1;
    std::string header = lines[headerIdx];

    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };

    // determine column start positions from header
    std::vector<int> colStarts;
    std::vector<std::string> colNames = {"Name","Id","Version","Available","Source"};
    for (auto &cn : colNames) {
        size_t p = header.find(cn);
        if (p != std::string::npos) colStarts.push_back((int)p);
    }
    if (colStarts.size() < 2) {
        // fallback: whitespace token parsing
        for (int i = sepIdx + 1, lastAdded = -1; i < (int)lines.size(); ++i) {
            const std::string &ln = lines[i];
            if (ln.find("upgrades available") != std::string::npos) break;
            if (trim(ln).empty()) continue;
            std::istringstream ls(ln);
            std::vector<std::string> toks;
            std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() < 4) continue;
                // Look for pattern: <name...> <id> <installed-version> <available-version>
                std::regex verRe2(R"(^[0-9]+(\.[0-9]+)*$)");
                size_t n = toks.size();
                // require at least two trailing version-like tokens
                if (n >= 3 && std::regex_match(toks[n-1], verRe2) && std::regex_match(toks[n-2], verRe2)) {
                    std::string available = toks[n-1];
                    std::string installed = toks[n-2];
                    std::string id = toks[n-3];
                    std::string name;
                    for (size_t j = 0; j + 3 < toks.size(); ++j) {
                        if (j) name += " ";
                        name += toks[j];
                    }
                    if (name.empty()) name = id;
                    if (CompareVersions(installed, available) < 0) {
                        try { if (!IsSkipped(id, available)) g_packages.emplace_back(id, name); } catch(...) { g_packages.emplace_back(id, name); }
                    }
                } else {
                    // fallback: if we can't detect versions, skip to avoid false positives
                    continue;
                }
            lastAdded = (int)g_packages.size()-1;
        }
        return;
    }

    // add end position as line length sentinel per line when slicing
    int lastAdded = -1;
    for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (ln.find("upgrades available") != std::string::npos) break;
        if (trim(ln).empty()) continue;
        // if the line is shorter than second column start, treat as continuation
        if ((int)ln.size() <= colStarts[1]) {
            if (lastAdded >= 0) {
                std::string cont = trim(ln);
                if (!cont.empty()) {
                    g_packages[lastAdded].second += " ";
                    g_packages[lastAdded].second += cont;
                }
            }
            continue;
        }
        auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
            int len = (int)s.size();
            if (a >= len) return std::string();
            int end = std::min(len, b);
            return s.substr(a, end - a);
        };

        // compute per-column substrings using next column start or line end
        int ncols = (int)colStarts.size();
        std::vector<std::string> fields(ncols);
        for (int c = 0; c < ncols; ++c) {
            int a = colStarts[c];
            int b = (c+1 < ncols) ? colStarts[c+1] : (int)ln.size();
            fields[c] = trim(substrSafe(ln, a, b));
        }
        std::string name = fields[0];
        std::string id = (ncols > 1) ? fields[1] : std::string();
        if (id.empty()) {
            // treat as continuation if id missing
            if (lastAdded >= 0) {
                std::string cont = trim(ln);
                if (!cont.empty()) {
                    g_packages[lastAdded].second += " ";
                    g_packages[lastAdded].second += cont;
                }
            }
            continue;
        }
        if (name.empty()) name = id;
        try {
            std::string availCol = (ncols > 3) ? fields[3] : std::string();
            if (!IsSkipped(id, availCol)) g_packages.emplace_back(id, name);
            else { /* skipped */ }
        } catch(...) { g_packages.emplace_back(id, name); }
        lastAdded = (int)g_packages.size()-1;
    }

    // Additional heuristic pass: look for tokens like <Id-with-dot> <version>
    // to catch lines where wrapping confused column slicing.
    std::set<std::string> seenIds;
    for (auto &p : g_packages) seenIds.insert(p.first);
    std::regex verRe(R"(^[0-9]+(\.[0-9]+)*$)");
    for (const auto &ln : lines) {
        std::istringstream ls(ln);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        // look for pattern: ... <id> <installed> <available>
        for (size_t j = 0; j + 2 < toks.size(); ++j) {
            if (std::regex_match(toks[j+1], verRe) && std::regex_match(toks[j+2], verRe)) {
                std::string id = toks[j];
                if (seenIds.count(id)) break;
                std::string installed = toks[j+1];
                std::string available = toks[j+2];
                if (CompareVersions(installed, available) < 0) {
                    std::string name;
                    for (size_t k = 0; k < j; ++k) {
                        if (k) name += " ";
                        name += toks[k];
                    }
                    if (name.empty()) name = id;
                    try {
                        if (!IsSkipped(id, available)) { g_packages.emplace_back(id, name); seenIds.insert(id); }
                        else { /* skipped */ }
                    } catch(...) { g_packages.emplace_back(id, name); seenIds.insert(id); }
                }
                break;
            }
        }
    }
    }

static void PopulateListView(HWND hList) {
    // Ensure any parsed-but-skipped packages are removed before inserting into the ListView
    try {
        AppendLog(std::string("RemoveSkippedFromPackages: start, count=") + std::to_string(g_packages.size()) + "\n");
    } catch(...) {}
    try {
        std::vector<std::pair<std::string,std::string>> kept;
        kept.reserve(g_packages.size());
        for (auto &p : g_packages) {
            bool skip = false;
            try {
                std::string avail;
                {
                    std::lock_guard<std::mutex> lk(g_versions_mutex);
                    auto it = g_last_avail_versions.find(p.first);
                    if (it != g_last_avail_versions.end()) avail = it->second;
                }
                if (IsSkipped(p.first, avail)) {
                    skip = true;
                    try { AppendLog(std::string("RemoveSkippedFromPackages: skipping ") + p.first + " avail='" + avail + "' name='" + p.second + "'\n"); } catch(...) {}
                }
            } catch(...) {}
            if (!skip) kept.push_back(p);
        }
        g_packages.swap(kept);
        try { AppendLog(std::string("RemoveSkippedFromPackages: end, kept=") + std::to_string(g_packages.size()) + "\n"); } catch(...) {}
    } catch(...) {}
    // Preserve current check state per-package (by id) so user selections survive refreshes
    std::unordered_map<std::string, bool> preservedChecks;
    int oldCount = ListView_GetItemCount(hList);
    for (int i = 0; i < oldCount; ++i) {
        BOOL checked = ListView_GetCheckState(hList, i);
        LVITEMW lvi{}; lvi.mask = LVIF_PARAM; lvi.iItem = i; lvi.iSubItem = 0;
        if (SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi)) {
            int idx = (int)lvi.lParam;
            if (idx >= 0 && idx < (int)g_packages.size()) {
                preservedChecks[g_packages[idx].first] = (checked != 0);
            }
        }
    }
    ListView_DeleteAllItems(hList);
    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    // Keep persistent buffers for item texts so ListView receives stable pointers
    static std::vector<std::wstring> itemNameBuf;
    static std::vector<std::wstring> itemCurBuf;
    static std::vector<std::wstring> itemAvailBuf;
    static std::vector<std::wstring> itemSkipBuf;
    static std::vector<std::wstring> itemExcludeBuf;
    itemNameBuf.clear(); itemCurBuf.clear(); itemAvailBuf.clear(); itemSkipBuf.clear(); itemExcludeBuf.clear();
    itemNameBuf.resize(g_packages.size()); itemCurBuf.resize(g_packages.size()); itemAvailBuf.resize(g_packages.size()); itemSkipBuf.resize(g_packages.size()); itemExcludeBuf.resize(g_packages.size());
    // prepare maps for versions (prefer cached probes to avoid blocking UI twice)
    auto avail = GetAvailableVersionsCached();
    auto inst = GetInstalledVersionsCached();
    for (int i = 0; i < (int)g_packages.size(); ++i) {
        std::string name = g_packages[i].second;
        std::string id = g_packages[i].first;
        std::wstring wname = Utf8ToWide(name);
        std::wstring wid = Utf8ToWide(id);
        // store into persistent buffers to ensure pointers remain valid
        itemNameBuf[i] = wname;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)itemNameBuf[i].c_str();
        lvi.lParam = i;
        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        // Current version (subitem 1)
        LVITEMW lviCur{}; lviCur.mask = LVIF_TEXT; lviCur.iItem = i; lviCur.iSubItem = 1;
        std::wstring wcur = L"";
        // resolve installed/available version robustly with normalization
        auto normalize = [&](const std::string &s)->std::string{
            std::string out;
            for (char c : s) {
                if (c == '.' || c == '-' || c == '_' || c == ' ' || c == '\\' || c == '/') continue;
                out.push_back((char)tolower((unsigned char)c));
            }
            return out;
        };
        auto resolveVersion = [&](const std::unordered_map<std::string,std::string> &m)->std::string {
            auto it = m.find(id);
            if (it != m.end()) return it->second;
            std::string nid = normalize(id);
            // try exact, substring and normalized matches
            for (auto &p : m) {
                if (p.first == id) return p.second;
                if (p.first.find(id) != std::string::npos) return p.second;
            }
            for (auto &p : m) {
                std::string pk = normalize(p.first);
                if (!pk.empty() && (pk == nid || pk.find(nid) != std::string::npos || nid.find(pk) != std::string::npos)) return p.second;
            }
            // try matching by package name tokens
            std::string nname = normalize(name);
            if (!nname.empty()) {
                for (auto &p : m) {
                    std::string pk = normalize(p.first);
                    if (!pk.empty() && pk.find(nname) != std::string::npos) return p.second;
                }
            }
            return std::string();
        };
        std::string curv = resolveVersion(inst);
        if (!curv.empty()) wcur = Utf8ToWide(curv);
        itemCurBuf[i] = wcur;
        lviCur.pszText = (LPWSTR)itemCurBuf[i].c_str();
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lviCur);
        // Available version (subitem 2)
        LVITEMW lviAvail{}; lviAvail.mask = LVIF_TEXT; lviAvail.iItem = i; lviAvail.iSubItem = 2;
        std::wstring wavail = L"";
        std::string availv = resolveVersion(avail);
        if (!availv.empty()) wavail = Utf8ToWide(availv);
        itemAvailBuf[i] = wavail;
        lviAvail.pszText = (LPWSTR)itemAvailBuf[i].c_str();
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lviAvail);
        // Skip column (subitem 3): always show localized Skip label (clickable hyperlink)
        LVITEMW lviSkip{}; lviSkip.mask = LVIF_TEXT; lviSkip.iItem = i; lviSkip.iSubItem = 3;
        std::wstring skipText = L"";
        // Always display the Skip label so the hyperlink can be shown; actual skip state is stored separately
        skipText = t("skip_col");
        itemSkipBuf[i] = skipText;
        lviSkip.pszText = (LPWSTR)itemSkipBuf[i].c_str();
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lviSkip);
        // Exclude column (subitem 4): always show localized Exclude label (clickable hyperlink)
        LVITEMW lviExclude{}; lviExclude.mask = LVIF_TEXT; lviExclude.iItem = i; lviExclude.iSubItem = 4;
        std::wstring excludeText = t("exclude_col");
        itemExcludeBuf[i] = excludeText;
        lviExclude.pszText = (LPWSTR)itemExcludeBuf[i].c_str();
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lviExclude);
    }
}

// Update the header control items' text using stable buffers so the header shows full words.
static void UpdateListViewHeaders(HWND hList) {
    if (!hList || !IsWindow(hList)) return;
    HWND hHeader = (HWND)SendMessageW(hList, LVM_GETHEADER, 0, 0);
    if (!hHeader || !IsWindow(hHeader)) return;
    static std::vector<std::wstring> headerBuffers;
    headerBuffers.clear();
    // Columns: Package, Current, Available, Skip, Exclude
    headerBuffers.push_back(t("package_col"));
    headerBuffers.push_back(t("current_col"));
    headerBuffers.push_back(t("available_col"));
    headerBuffers.push_back(t("skip_col"));
    headerBuffers.push_back(t("exclude_col"));
    for (int i = 0; i < (int)headerBuffers.size(); ++i) {
        HDITEMW hi{};
        hi.mask = HDI_TEXT;
        hi.pszText = (LPWSTR)headerBuffers[i].c_str();
        SendMessageW(hHeader, HDM_SETITEMW, (WPARAM)i, (LPARAM)&hi);
    }
}

static void AdjustListColumns(HWND hList) {
    RECT rc; GetClientRect(hList, &rc);
    int totalW = rc.right - rc.left;
    if (totalW <= 0) return;
    int wCur = (int)(totalW * 0.16);
    int wAvail = (int)(totalW * 0.16);
    int wSkip = (int)(totalW * 0.11);
    int wExclude = (int)(totalW * 0.11);
    int wName = totalW - (wCur + wAvail + wSkip + wExclude) - 4;
    ListView_SetColumnWidth(hList, 0, wName);
    ListView_SetColumnWidth(hList, 1, wCur);
    ListView_SetColumnWidth(hList, 2, wAvail);
    ListView_SetColumnWidth(hList, 3, wSkip);
    ListView_SetColumnWidth(hList, 4, wExclude);
    // Adjust font if needed (shrink if columns exceed width)
    // For simplicity, leave font as-is; could implement dynamic font sizing here.
}

// Show or hide the Unskip button based on whether any skipped entries exist
static void UpdateUnskipButton(HWND hwnd) {
    HWND hUn = GetDlgItem(hwnd, IDC_BTN_UNSKIP);
    if (!hUn || !IsWindow(hUn)) return;
    try {
        if (g_skipped_versions.empty()) {
            ShowWindow(hUn, SW_HIDE);
            EnableWindow(hUn, FALSE);
        } else {
            ShowWindow(hUn, SW_SHOW);
            EnableWindow(hUn, TRUE);
        }
    } catch(...) {}
}

// Helper used by custom draw / notifications: check if item index corresponds to NotApplicable id
static bool IsItemNotApplicable(int index) {
    if (index < 0 || index >= (int)g_packages.size()) return false;
    std::string id = g_packages[index].first;
    std::lock_guard<std::mutex> lk(g_packages_mutex);
    return g_not_applicable_ids.find(id) != g_not_applicable_ids.end();
}

// Parse the standard `winget upgrade` table which has columns: Name | Id | Version | Available
// Add entries where Available > Version.
static void ParseWingetUpgradeTableForUpdates(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) return;

    int headerIdx = -1, sepIdx = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (lines[i].find("----") != std::string::npos) { sepIdx = i; break; }
    }
    if (sepIdx <= 0) return;
    headerIdx = sepIdx - 1;
    std::string header = lines[headerIdx];

    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    std::vector<std::string> colNames = {"Name","Id","Version","Available"};
    std::vector<int> colStarts;
    for (auto &cn : colNames) {
        size_t p = header.find(cn);
        if (p != std::string::npos) colStarts.push_back((int)p);
    }
    if (colStarts.size() < 3) return;

    for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (trim(ln).empty()) continue;
        // stop on summary line
        if (ln.find("upgrades available") != std::string::npos) break;
        int ncols = (int)colStarts.size();
        auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
            int len = (int)s.size();
            if (a >= len) return std::string();
            int end = std::min(len, b);
            return s.substr(a, end - a);
        };
        std::vector<std::string> fields(ncols);
        for (int c = 0; c < ncols; ++c) {
            int a = colStarts[c];
            int b = (c+1 < ncols) ? colStarts[c+1] : (int)ln.size();
            fields[c] = trim(substrSafe(ln, a, b));
        }
        std::string name = fields[0];
        std::string id = (ncols > 1) ? fields[1] : std::string();
        std::string installed = (ncols > 2) ? fields[2] : std::string();
        std::string available = (ncols > 3) ? fields[3] : std::string();
        if (id.empty()) continue;
        if (CompareVersions(installed, available) < 0) outSet.emplace(id, name.empty()?id:name);
    }
}

// Dump parsed packages and current ListView items to a temp file for debugging
static std::wstring DumpPackagesAndListViewToTemp(HWND hList) {
    wchar_t curDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, curDir);
    unsigned long long uniq = GetTickCount64();
    wchar_t outfn[MAX_PATH];
    swprintf(outfn, _countof(outfn), L"%s\\wup_dump_%llu.txt", curDir, uniq);
    std::string nfn = WideToUtf8(outfn);
    std::ofstream ofs(nfn, std::ios::binary);
    if (!ofs) return std::wstring();
    ofs << "Parsed packages:\r\n";
    for (auto &p : g_packages) {
        ofs << p.first << "\t" << p.second << "\r\n";
    }
    ofs << "\r\nListView items:\r\n";
    int cnt = ListView_GetItemCount(hList);
    for (int i = 0; i < cnt; ++i) {
        wchar_t buf[1024] = {0};
        LVITEMW lvi{};
        lvi.iItem = i; lvi.iSubItem = 0; lvi.mask = LVIF_TEXT; lvi.pszText = buf; lvi.cchTextMax = _countof(buf);
        SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi);
        std::wstring wtext(buf);
        std::string stext = WideToUtf8(wtext);
        ofs << stext << "\r\n";
    }
    ofs.close();
    return std::wstring(outfn);
}

// Write arbitrary UTF-8 text to a temp file and return its path
static std::wstring WriteDebugTextToTemp(const std::string &txt) {
    wchar_t curDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, curDir);
    unsigned long long uniq = GetTickCount64();
    wchar_t outfn[MAX_PATH];
    swprintf(outfn, _countof(outfn), L"%s\\wupdbg_%llu.txt", curDir, uniq);
    std::string nfn = WideToUtf8(outfn);
    std::ofstream ofs(nfn, std::ios::binary);
    if (!ofs) return std::wstring();
    ofs << txt;
    ofs.close();
    return std::wstring(outfn);
}

// Remove any stale temp files created by previous runs (wup_install_*.txt)
static void CleanupStaleInstallFiles() {
    try {
        wchar_t tmpPathBuf[MAX_PATH]; GetTempPathW(MAX_PATH, tmpPathBuf);
        std::wstring tmpPath(tmpPathBuf);
        namespace fs = std::filesystem;
        for (auto &e : fs::directory_iterator(tmpPath)) {
            try {
                if (!e.is_regular_file()) continue;
                std::wstring fn = e.path().filename().wstring();
                if (fn.rfind(L"wup_install_", 0) == 0) {
                    fs::remove(e.path());
                }
            } catch(...) { /* ignore per-file errors */ }
        }
    } catch(...) {}
}

static void CheckAllItems(HWND hList, bool check) {
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) {
        bool skip = false;
        if (i >= 0 && i < (int)g_packages.size()) {
            std::string id = g_packages[i].first;
            std::lock_guard<std::mutex> lk(g_packages_mutex);
            skip = (g_skipped_versions.find(id) != g_skipped_versions.end());
        }
        if (!skip) ListView_SetCheckState(hList, i, check);
    }
}

// Parse raw winget upgrade output, update startup/live version maps and write logfile.
static void CaptureStartupVersions(const std::string &rawOut,
                                   const std::vector<std::pair<std::string,std::string>> &results,
                                   const std::unordered_map<std::string,std::string> &avail,
                                   const std::unordered_map<std::string,std::string> &inst,
                                   bool forceOverwrite = false) {
    try {
        std::vector<std::tuple<std::string,std::string,std::string,std::string>> parsedRows;
        std::string localRaw = rawOut;
        // prefer existing raw file if present
        try {
            std::string fileTxt = ReadFileUtf8(std::wstring(L"logs\\wup_winget_raw.txt"));
            if (!fileTxt.empty()) localRaw = fileTxt;
        } catch(...) {}
        if (localRaw.empty()) {
            try {
                auto fresh = RunProcessCaptureExitCode(L"winget upgrade", 15000);
                if (!fresh.second.empty()) localRaw = fresh.second;
            } catch(...) {}
        }
        // Right-to-left token parsing (robust against variable name widths)
        if (!localRaw.empty()) {
            std::istringstream ois(localRaw);
            std::string line;
            std::vector<std::string> allLines;
            while (std::getline(ois, line)) allLines.push_back(line);
            size_t startIdx = (allLines.size() > 2) ? 2 : 0;
            for (size_t i = startIdx; i < allLines.size(); ++i) {
                std::string row = allLines[i];
                auto trimRow = [](std::string &s){ while(!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back(); while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
                trimRow(row);
                if (row.empty()) continue;
                if (row.find("----") != std::string::npos) continue;
                if (row.find("upgrades available") != std::string::npos) break;
                std::istringstream ls(row);
                std::vector<std::string> toks;
                std::string tok;
                while (ls >> tok) toks.push_back(tok);
                if (toks.size() < 4) continue;
                std::string source = toks.back(); toks.pop_back();
                std::string available = toks.back(); toks.pop_back();
                std::string installed = toks.back(); toks.pop_back();
                std::string id = toks.back(); toks.pop_back();
                std::string name;
                for (size_t k = 0; k < toks.size(); ++k) { if (k) name += " "; name += toks[k]; }
                if (name.empty()) name = id;
                parsedRows.emplace_back(name, id, installed, available);
            }
        }

        // If parsed rows found, update startup maps and live caches
        if (!parsedRows.empty()) {
            try {
                std::lock_guard<std::mutex> lk(g_startup_versions_mutex);
                if (forceOverwrite || g_startup_avail_versions.empty() && g_startup_inst_versions.empty()) {
                    for (auto &t : parsedRows) {
                        const std::string &id = std::get<1>(t);
                        const std::string &installed = std::get<2>(t);
                        const std::string &available = std::get<3>(t);
                        g_startup_inst_versions[id] = installed;
                        g_startup_avail_versions[id] = available;
                    }
                }
            } catch(...) {}
            try {
                std::lock_guard<std::mutex> vlk(g_versions_mutex);
                for (auto &t : parsedRows) {
                    const std::string &id = std::get<1>(t);
                    const std::string &installed = std::get<2>(t);
                    const std::string &available = std::get<3>(t);
                    if (!installed.empty()) g_last_inst_versions[id] = installed;
                    if (!available.empty()) g_last_avail_versions[id] = available;
                }
            } catch(...) {}
        } else {
            // fallback: use avail/inst maps and discovered results
            try {
                std::lock_guard<std::mutex> lk(g_startup_versions_mutex);
                std::unordered_set<std::string> candidateIds;
                for (auto &p : results) candidateIds.insert(p.first);
                if (candidateIds.empty()) {
                    for (auto &a : avail) {
                        const std::string &id = a.first;
                        auto itInst = inst.find(id);
                        if (itInst == inst.end()) continue;
                        try {
                            if (CompareVersions(itInst->second, a.second) < 0) candidateIds.insert(id);
                        } catch(...) {
                            if (itInst->second != a.second) candidateIds.insert(id);
                        }
                    }
                }
                for (auto &id : candidateIds) {
                    auto ait = avail.find(id);
                    if (ait != avail.end()) g_startup_avail_versions[id] = ait->second;
                    auto iit = inst.find(id);
                    if (iit != inst.end()) g_startup_inst_versions[id] = iit->second;
                    // also update live caches
                    try {
                        std::lock_guard<std::mutex> vlk(g_versions_mutex);
                        if (iit != inst.end() && !iit->second.empty()) g_last_inst_versions[id] = iit->second;
                        if (ait != avail.end() && !ait->second.empty()) g_last_avail_versions[id] = ait->second;
                    } catch(...) {}
                }
            } catch(...) {}
        }

        // No logfile output requested: keep parsed startup data in memory only.
    } catch(...) {}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Handle Ctrl+W for closing window
    if (HandleCtrlW(hwnd, uMsg, wParam, lParam)) {
        return 0;
    }
    
    // Debug: Log if we receive message 1025 (WM_TRAYICON)
    if (uMsg == 1025) {
        std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
        log << "WndProc received uMsg=1025 (WM_TRAYICON), wParam=" << wParam << ", lParam=0x" << std::hex << lParam << std::dec << "\n";
        log.close();
    }
    
    static HWND hRadioShow, hRadioAll, hBtnRefresh, hList, hCheckAll, hBtnUpgrade;
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icce);

        // Title (H2-like centered)
        HDC hdcTitle = GetDC(hwnd);
        int lfTitleHeight = -MulDiv(14, GetDeviceCaps(hdcTitle, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdcTitle);
        g_hTitleFont = CreateFontW(lfTitleHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        // Title moved right to make space for left-aligned language combo
        g_hTitle = CreateWindowExW(0, L"Static", L"WinUpdate", WS_CHILD | WS_VISIBLE | SS_CENTER, 80, 10, 530, 28, hwnd, NULL, NULL, NULL);
        if (g_hTitle && g_hTitleFont) SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);

        // language selection combobox (top-left)
        HWND hComboLang = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 10, 10, 150, 200, hwnd, (HMENU)IDC_COMBO_LANG, NULL, NULL);
        if (hComboLang) {
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)L"English (UK)");
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)L"Norsk (NB)");
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)L"Svenska (SE)");
            // select based on g_locale (prefix)
            int sel = 0;
            if (g_locale.rfind("nb_NO",0) == 0) sel = 1;
            else if (g_locale.rfind("sv_SE",0) == 0) sel = 2;
            SendMessageW(hComboLang, CB_SETCURSEL, sel, 0);
        }

        // Last-updated label (small bold ~9pt) placed under the title and centered
        HDC hdc = GetDC(hwnd);
        int lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdc);
        g_hLastUpdatedFont = CreateFontW(lfHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hLastUpdated = CreateWindowExW(0, L"Static", L"List last updated: N/A", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 40, 600, 16, hwnd, NULL, NULL, NULL);
        if (g_hLastUpdated && g_hLastUpdatedFont) SendMessageW(g_hLastUpdated, WM_SETFONT, (WPARAM)g_hLastUpdatedFont, TRUE);

        hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 10, 60, 600, 284, hwnd, (HMENU)IDC_LISTVIEW, NULL, NULL);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        // attach hyperlink behavior to list so hover/click are handled inside src/hyperlink.*
        Hyperlink_Attach(hList);
        // prepare persistent column header strings so pointers remain valid
        g_colHeaders.clear();
        g_colHeaders.push_back(t("package_col"));
        g_colHeaders.push_back(t("current_col"));
        g_colHeaders.push_back(t("available_col"));
        g_colHeaders.push_back(t("skip_col"));
        g_colHeaders.push_back(t("exclude_col"));
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.cx = 280;
        col.fmt = LVCFMT_LEFT;
        col.pszText = (LPWSTR)g_colHeaders[0].c_str();
        ListView_InsertColumn(hList, 0, &col);
        LVCOLUMNW colCur{}; colCur.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; colCur.cx = 100; colCur.fmt = LVCFMT_LEFT; colCur.pszText = (LPWSTR)g_colHeaders[1].c_str(); ListView_InsertColumn(hList, 1, &colCur);
        LVCOLUMNW colAvail{}; colAvail.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; colAvail.cx = 100; colAvail.fmt = LVCFMT_LEFT; colAvail.pszText = (LPWSTR)g_colHeaders[2].c_str(); ListView_InsertColumn(hList, 2, &colAvail);
        LVCOLUMNW colSkip{}; colSkip.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; colSkip.cx = 80; colSkip.fmt = LVCFMT_CENTER; colSkip.pszText = (LPWSTR)g_colHeaders[3].c_str(); ListView_InsertColumn(hList, 3, &colSkip);
        LVCOLUMNW colExclude{}; colExclude.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; colExclude.cx = 80; colExclude.fmt = LVCFMT_CENTER; colExclude.pszText = (LPWSTR)g_colHeaders[4].c_str(); ListView_InsertColumn(hList, 4, &colExclude);

        hCheckAll = CreateWindowExW(0, L"Button", t("select_all").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 350, 120, 28, hwnd, (HMENU)IDC_BTN_SELECTALL, NULL, NULL);
        // place Upgrade button 5px to the right of Select all
        hBtnUpgrade = CreateWindowExW(0, L"Button", t("upgrade_now").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 135, 350, 220, 28, hwnd, (HMENU)IDC_BTN_UPGRADE, NULL, NULL);
        // Unskip selected (hidden by default). Place between Upgrade and Refresh.
        HWND hBtnUnskip = CreateWindowExW(0, L"Button", t("unskip_btn").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 365, 350, 100, 28, hwnd, (HMENU)IDC_BTN_UNSKIP, NULL, NULL);
        if (hBtnUnskip) {
            UpdateUnskipButton(hwnd);
        }
        // Paste button removed — app scans `winget` at startup and on Refresh
        // position Refresh where the Upgrade button used to be (bottom-right)
        hBtnRefresh = CreateWindowExW(0, L"Button", t("refresh").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 470, 350, 140, 28, hwnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);

        // Config button (owner-draw, positioned with 20px gap before About at 690)
        HWND hBtnConfig = CreateWindowExW(0, L"Button", t("config_btn").c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 550, 10, 120, 28, hwnd, (HMENU)IDC_BTN_CONFIG, NULL, NULL);
        if (hBtnConfig) {
            SetWindowSubclass(hBtnConfig, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
                switch (msg) {
                case WM_MOUSEMOVE: {
                    SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                    InvalidateRect(h, NULL, TRUE);
                    TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                    break;
                }
                case WM_MOUSELEAVE: {
                    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                    InvalidateRect(h, NULL, TRUE);
                    break;
                }
                }
                return DefSubclassProc(h, msg, wp, lp);
            }, 0, 0);
        }

        // About button at top-right (owner-draw so we can color on hover/press)
        HWND hBtnAbout = CreateWindowExW(0, L"Button", t("about_btn").c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 690, 10, 120, 28, hwnd, (HMENU)IDC_BTN_ABOUT, NULL, NULL);
        if (hBtnAbout) {
            // Create custom tooltip window (same style as Skip column tooltip)
            static const wchar_t *kTipClass = L"WinUpdateSimpleTooltip";
            static bool registered = false;
            if (!registered) {
                WNDCLASSEXW wc = { sizeof(wc) };
                wc.lpfnWndProc = [](HWND hw, UINT um, WPARAM w, LPARAM l)->LRESULT {
                    if (um == WM_PAINT) {
                        PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
                        RECT rc; GetClientRect(hw, &rc);
                        FillRect(hdc, &rc, (HBRUSH)(COLOR_INFOBK + 1));
                        FrameRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
                        int len = GetWindowTextLengthW(hw);
                        std::vector<wchar_t> buf(len + 1);
                        if (len > 0) GetWindowTextW(hw, buf.data(), len + 1);
                        SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
                        SetBkMode(hdc, TRANSPARENT);
                        HFONT hf = (HFONT)SendMessageW(hw, WM_GETFONT, 0, 0);
                        HGDIOBJ old = NULL; if (hf) old = SelectObject(hdc, hf);
                        RECT inner = rc; inner.left += 4; inner.right -= 4; inner.top += 2; inner.bottom -= 2;
                        DrawTextW(hdc, buf.data(), -1, &inner, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                        if (old) SelectObject(hdc, old);
                        EndPaint(hw, &ps);
                        return 0;
                    }
                    return DefWindowProcW(hw, um, w, l);
                };
                wc.hCursor = LoadCursor(NULL, IDC_ARROW);
                wc.hInstance = GetModuleHandleW(NULL);
                wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
                wc.lpszClassName = kTipClass;
                RegisterClassExW(&wc);
                registered = true;
            }
        }

        // record main window handle, initial timestamp and auto-refresh once UI is created (start async refresh)
        g_hMainWindow = hwnd;
        // clean up any stale install temp files from previous runs
        CleanupStaleInstallFiles();
        UpdateLastUpdatedLabel(hwnd);
        // Start with the list hidden and controls disabled while we scan winget.
        if (hList) ShowWindow(hList, SW_HIDE);
        if (hBtnRefresh) EnableWindow(hBtnRefresh, FALSE);
        if (hBtnUpgrade) EnableWindow(hBtnUpgrade, FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_SELECTALL), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_LANG), FALSE);
        // Note: Initial scan will be triggered from wWinMain after window is shown
        break;
    }
    case WM_REFRESH_ASYNC: {
        // wParam: non-zero = manual refresh (user requested). Use this to suppress automatic popups on startup.
        bool manual = (wParam != 0);
        // start background thread to perform winget query + parsing
        // disable Refresh button while running
        g_refresh_in_progress.store(true);
        if (hBtnRefresh) EnableWindow(hBtnRefresh, FALSE);
        if (hBtnUpgrade) EnableWindow(hBtnUpgrade, FALSE);
        ShowLoading(hwnd);
        
        // Update tray tooltip to show scanning status
        if (g_systemTray && g_systemTray->IsActive()) {
            g_systemTray->UpdateNextScanTime(t("tray_scanning"));
        }
        
        std::thread([hwnd, manual]() {
            std::vector<std::pair<std::string,std::string>> results;

            // Run winget upgrade with a 5s timeout (fast). If it returns quickly, parse it; if it times out/empty, try longer.
            auto rup = RunProcessCaptureExitCode(L"winget upgrade", 5000);
            std::string out = rup.second;
            bool timedOut = (rup.first == -2);
            // If initial fast attempt timed out or returned empty, try a longer attempt once
            if (timedOut || out.empty()) {
                auto rup2 = RunProcessCaptureExitCode(L"winget upgrade", 15000);
                if (!rup2.second.empty()) {
                    out = rup2.second;
                    timedOut = false;
                }
            }
            
            // Store output in memory for AppendSkippedRaw to use
            if (!out.empty()) {
                std::lock_guard<std::mutex> lk(g_last_winget_raw_mutex);
                g_last_winget_raw = out;
                AppendLog(std::string("WM_REFRESH_ASYNC: stored winget output in memory, size=") + std::to_string((int)out.size()) + "\n");
                
                // Count total packages from output (lines between separator and "upgrades available")
                std::istringstream iss(out);
                std::string line;
                int count = 0;
                bool foundSeparator = false;
                while (std::getline(iss, line)) {
                    if (!foundSeparator && line.find("----") != std::string::npos) {
                        foundSeparator = true;
                        continue;
                    }
                    if (foundSeparator) {
                        if (line.find("upgrades available") != std::string::npos) break;
                        // Count non-empty lines that likely contain package data
                        std::string trimmed = line;
                        while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n' || isspace((unsigned char)trimmed.back()))) trimmed.pop_back();
                        while (!trimmed.empty() && isspace((unsigned char)trimmed.front())) trimmed.erase(trimmed.begin());
                        if (!trimmed.empty()) count++;
                    }
                }
                if (count > 0) {
                    g_total_winget_packages = count;
                    AppendLog(std::string("Total winget packages detected: ") + std::to_string(count) + "\n");
                }
            }
            if (!out.empty()) {
                // Prefer the in-memory parser chain to extract Id/Name pairs
                auto vec = ParseRawWingetTextInMemory(out);
                std::set<std::pair<std::string,std::string>> found;
                for (auto &p : vec) found.emplace(p.first, p.second);
                for (auto &p : found) results.emplace_back(p.first, p.second);
            }

            // Start background population of available/installed versions without blocking the initial scan.
            try {
                // Run probes in parallel with per-call wait limits to reduce wall time
                auto futAvail = std::async(std::launch::async, MapAvailableVersions);
                auto futInst = std::async(std::launch::async, MapInstalledVersions);
                std::unordered_map<std::string,std::string> avail;
                std::unordered_map<std::string,std::string> inst;
                auto perCallTimeout = std::chrono::milliseconds(5200);
                if (futAvail.wait_for(perCallTimeout) == std::future_status::ready) {
                    try { avail = futAvail.get(); } catch(...) {}
                }
                if (futInst.wait_for(perCallTimeout) == std::future_status::ready) {
                    try { inst = futInst.get(); } catch(...) {}
                }
                {
                    std::lock_guard<std::mutex> lk(g_versions_mutex);
                    if (!avail.empty()) g_last_avail_versions = avail;
                    if (!inst.empty()) g_last_inst_versions = inst;
                }
                // Also capture a startup snapshot (write to logs for verification)
                try {
                    CaptureStartupVersions(out, results, avail, inst, false);
                } catch(...) {}
            } catch(...) {}

            // If winget upgrade failed or timed out, results will be empty
            // No fallback needed - user can simply refresh again
            auto *pv = new std::vector<std::pair<std::string,std::string>>(std::move(results));
            // propagate manual flag to the WM_REFRESH_DONE handler via wParam so UI can decide whether to show popups
            PostMessageA(hwnd, WM_REFRESH_DONE, manual ? 1 : 0, (LPARAM)pv);
        }).detach();
        break;
    }
    case WM_REFRESH_DONE: {
        std::vector<std::pair<std::string,std::string>> *pv = (std::vector<std::pair<std::string,std::string>>*)lParam;
        if (pv) {
            // Reload excluded apps from .ini file to pick up any manual changes
            LoadExcludeSettings(g_excluded_apps);
            
            // Filter out excluded apps before updating global packages
            std::vector<std::pair<std::string,std::string>> filtered;
            WaitForSingleObject(g_excluded_mutex, INFINITE);
            for (const auto& pkg : *pv) {
                if (g_excluded_apps.find(pkg.first) == g_excluded_apps.end()) {
                    filtered.push_back(pkg);
                }
            }
            ReleaseMutex(g_excluded_mutex);
            
            // update global packages and UI
            {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                g_packages = std::move(filtered);
            }
            delete pv;
            // If an install panel is blocking destruction, avoid changing the main list/controls
            if (!g_install_block_destroy.load()) {
                if (hList) PopulateListView(hList);
                if (hList) AdjustListColumns(hList);
                if (hList) UpdateListViewHeaders(hList);
                // Make sure the list is visible after we've populated it
                if (hList) ShowWindow(hList, SW_SHOW);
                // re-enable buttons
                if (hBtnRefresh) EnableWindow(hBtnRefresh, TRUE);
                if (hBtnUpgrade) EnableWindow(hBtnUpgrade, TRUE);
            } else {
                AppendLog("WM_REFRESH_DONE: refresh completed but install panel active; skipping UI re-enable/populate\n");
            }
        }
        HideLoading();
        g_refresh_in_progress.store(false);
        // Ensure main UI controls are enabled after any refresh
        if (hList) EnableWindow(hList, TRUE);
        if (hBtnRefresh) EnableWindow(hBtnRefresh, TRUE);
        if (hBtnUpgrade) EnableWindow(hBtnUpgrade, TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_SELECTALL), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_LANG), TRUE);
        // If the refresh produced no non-skipped and non-excluded packages, show an 'up-to-date' dialog
        {
            std::lock_guard<std::mutex> lk(g_packages_mutex);
            // Count non-skipped and non-excluded packages
            int nonSkippedCount = 0;
            WaitForSingleObject(g_excluded_mutex, INFINITE);
            for (const auto& pkg : g_packages) {
                if (g_skipped_versions.find(pkg.first) == g_skipped_versions.end() &&
                    g_excluded_apps.find(pkg.first) == g_excluded_apps.end()) {
                    nonSkippedCount++;
                }
            }
            ReleaseMutex(g_excluded_mutex);
            
            if (nonSkippedCount == 0) {
                // Only show the 'up-to-date' popup if this was a manual refresh (user requested)
                // If window is visible, show dialog. If hidden in tray, balloon will be shown later.
                if (wParam && IsWindowVisible(hwnd)) {
                    std::wstring msg = t("your_system_updated");
                    MessageBoxW(hwnd, msg.c_str(), L"WinUpdate", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
        // After refresh, let the central skip management purge obsolete entries
        try {
            auto avail_u = GetAvailableVersionsCached();
            std::map<std::string,std::string> avail_map(avail_u.begin(), avail_u.end());
            // Purge entries stored in per-user skip INI that are obsolete (available > skipped)
            PurgeObsoleteSkips(avail_map);
            // Reload per-user skipped map into in-memory `g_skipped_versions` so UI logic uses current state
            try {
                auto m = LoadSkippedMap();
                g_skipped_versions.clear();
                for (auto &p : m) g_skipped_versions[p.first] = p.second;
            
                // Update Unskip button visibility now that we've reloaded per-user skips
                UpdateUnskipButton(hwnd);
            } catch(...) {}
            
            // If in system tray mode and window is hidden, show balloon notification and update tooltip
            if (g_systemTray && g_systemTray->IsActive() && !IsWindowVisible(hwnd)) {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                int nonSkippedCount = 0;
                for (const auto& pkg : g_packages) {
                    if (g_skipped_versions.find(pkg.first) == g_skipped_versions.end()) {
                        nonSkippedCount++;
                    }
                }
                
                // Update tooltip with result
                std::wstring statusLine;
                if (nonSkippedCount == 0) {
                    statusLine = t("tray_no_updates");
                } else if (nonSkippedCount == 1) {
                    statusLine = t("tray_one_update");
                } else {
                    statusLine = std::to_wstring(nonSkippedCount) + L" " + t("tray_updates_available");
                }
                g_systemTray->UpdateNextScanTime(statusLine);
                
                if (nonSkippedCount > 0) {
                    // Always show notification when updates are found
                    std::wstring title = t("tray_balloon_updates_title");
                    std::wstring msg;
                    if (nonSkippedCount == 1) {
                        msg = t("tray_balloon_one_update");
                    } else {
                        wchar_t buf[256];
                        swprintf(buf, 256, t("tray_balloon_multiple_updates").c_str(), nonSkippedCount);
                        msg = buf;
                    }
                    g_systemTray->ShowBalloon(title, msg);
                } else if (wParam) {
                    // Only show "You are updated!" if this was a manual scan (wParam == 1)
                    // Don't show for automatic periodic scans (would be annoying)
                    g_systemTray->ShowBalloon(L"WinUpdate", t("tray_balloon_no_updates"));
                }
            } else if (g_systemTray && g_systemTray->IsActive()) {
                // Window is visible - just update tooltip, no balloon
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                int nonSkippedCount = 0;
                for (const auto& pkg : g_packages) {
                    if (g_skipped_versions.find(pkg.first) == g_skipped_versions.end()) {
                        nonSkippedCount++;
                    }
                }
                
                std::wstring statusLine;
                if (nonSkippedCount == 0) {
                    statusLine = t("tray_no_updates");
                } else if (nonSkippedCount == 1) {
                    statusLine = t("tray_one_update");
                } else {
                    statusLine = std::to_wstring(nonSkippedCount) + L" " + t("tray_updates_available");
                }
                g_systemTray->UpdateNextScanTime(statusLine);
            }
        } catch(...) {}
        
        // Ensure main window is visible and front-most after a refresh completes
        // (but skip if system tray is active and window is hidden)
        if (!g_systemTray || !g_systemTray->IsActive() || IsWindowVisible(hwnd)) {
            AppendLog("WM_REFRESH_DONE: refresh complete, forcing main window to foreground\n");
            TryForceForegroundWindow(hwnd);
        }
        // If an install panel exists and is still blocked for destruction, ensure it stays above the list
        if (g_hInstallPanel && IsWindow(g_hInstallPanel) && g_install_block_destroy.load()) {
            AppendLog("WM_REFRESH_DONE: install panel present; reasserting top Z-order\n");
            SetWindowPos(g_hInstallPanel, HWND_TOP, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE);
            // ensure Done button remains enabled state as previously set by WM_INSTALL_DONE
            HWND hDoneBtn = GetDlgItem(hwnd, IDC_BTN_DONE);
            if (hDoneBtn) EnableWindow(hDoneBtn, TRUE);
        }
        break;
    }
    case WM_COPYDATA: {
        PCOPYDATASTRUCT pcds = (PCOPYDATASTRUCT)lParam;
        if (!pcds || !pcds->lpData) break;
        try {
            std::string payload((char*)pcds->lpData, pcds->cbData ? pcds->cbData - 1 : 0);
            AppendLog(std::string("WM_COPYDATA: payload='") + payload + "'\n");
            // Expect payload starting with "WUP_SKIP\n<appname>\n<available>\n"
            if (payload.rfind("WUP_SKIP\n", 0) == 0) {
                size_t pos = 8; // after prefix
                size_t e1 = payload.find('\n', pos);
                if (e1 == std::string::npos) e1 = payload.size();
                std::string appn = payload.substr(pos, e1 - pos);
                size_t pos2 = (e1 == payload.size()) ? e1 : e1 + 1;
                size_t e2 = payload.find('\n', pos2);
                if (e2 == std::string::npos) e2 = payload.size();
                std::string avail = (pos2 <= e2) ? payload.substr(pos2, e2 - pos2) : std::string();
                // find matching package by displayed name (g_packages[][1] == name)
                int foundIdx = -1;
                // If g_packages is empty (UI may not have populated it yet), try to repopulate
                try {
                    std::lock_guard<std::mutex> lkchk(g_packages_mutex);
                    if (g_packages.empty()) {
                        AppendLog(std::string("WM_COPYDATA: g_packages empty, attempting to repopulate from recent raw winget\n"));
                        try {
                            std::string raw = ReadMostRecentRawWinget();
                            if (!raw.empty()) {
                                ParseWingetTextForPackages(raw);
                                AppendLog(std::string("WM_COPYDATA: ParseWingetTextForPackages populated g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
                            } else {
                                AppendLog("WM_COPYDATA: ReadMostRecentRawWinget returned empty\n");
                            }
                        } catch(...) { AppendLog("WM_COPYDATA: repopulate attempt threw\n"); }
                    }
                } catch(...) {}
                {
                    std::lock_guard<std::mutex> lk(g_packages_mutex);
                    for (int i = 0; i < (int)g_packages.size(); ++i) {
                        if (g_packages[i].second == appn) { foundIdx = i; break; }
                    }
                }
                AppendLog(std::string("WM_COPYDATA: foundIdx=") + std::to_string(foundIdx) + "\n");
                std::string id;
                if (foundIdx >= 0) {
                    try { std::lock_guard<std::mutex> lk(g_packages_mutex); id = g_packages[foundIdx].first; } catch(...) {}
                } else {
                    // tolerant match: try to match by prefix/contains
                    try {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        for (int i = 0; i < (int)g_packages.size(); ++i) {
                            if (!g_packages[i].second.empty() && appn.size() > 0 && g_packages[i].second.find(appn) != std::string::npos) { id = g_packages[i].first; foundIdx = i; break; }
                        }
                    } catch(...) {}
                }
                std::string ver = avail;
                if (!ver.empty() && id.empty() && foundIdx >= 0) {
                    try { std::lock_guard<std::mutex> lk(g_packages_mutex); id = g_packages[foundIdx].first; } catch(...) {}
                }
                bool added = false;
                if (!id.empty()) {
                    // Get full display name from g_packages using the resolved ID
                    std::string fullDisplayName = appn;
                    try {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        for (auto &pkg : g_packages) {
                            if (pkg.first == id) {
                                fullDisplayName = pkg.second;
                                break;
                            }
                        }
                    } catch(...) {}
                    try { added = AddSkippedEntry(id, ver, fullDisplayName); } catch(...) { added = false; }
                }
                AppendLog(std::string("WM_COPYDATA: mapping appname->") + "id='" + id + "' avail='" + ver + "'\n");
                AppendLog(std::string("WM_COPYDATA: AddSkippedEntry returned ") + (added?"true":"false") + "\n");
                try {
                        if (added) {
                        g_skipped_versions[id] = ver;
                        UpdateUnskipButton(hwnd);
                    }
                } catch(...) {}
                // Regardless of whether we resolved an id, trigger a UI refresh so the list is rescanned
                try { SaveSkipConfig(g_locale); } catch(...) {}
                if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
            }
        } catch(...) {}
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndCtl = (HWND)lParam;
        // make static controls blend with window background (transparent) and
        // set last-updated text to dark green
        if (hwndCtl == g_hLastUpdated) {
            SetTextColor(hdcStatic, RGB(0,100,0));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        if (hwndCtl == g_hTitle) {
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    }
    case WM_SIZE: {
        RECT rcClient; GetClientRect(hwnd, &rcClient);
        int cw = rcClient.right - rcClient.left;
        int ch = rcClient.bottom - rcClient.top;
        int padding = 10;

        // Top controls
        HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_LANG);
        if (hCombo && IsWindow(hCombo)) {
            SetWindowPos(hCombo, NULL, padding, 10, 150, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (g_hTitle && IsWindow(g_hTitle)) {
            int titleH = 28;
            int titleW = std::max(240, cw - 260);
            int titleX = (cw - titleW) / 2;
            SetWindowPos(g_hTitle, NULL, titleX, 10, titleW, titleH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (g_hLastUpdated && IsWindow(g_hLastUpdated)) {
            SetWindowPos(g_hLastUpdated, NULL, padding, 40, cw - 2*padding, 16, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Listview: fill available area between header and bottom controls
        if (hList && IsWindow(hList)) {
            int listX = padding;
            int listY = 60;
            int bottomAreaH = 52; // space for checkboxes and buttons
            int listW = std::max(200, cw - 2*padding);
            int listH = std::max(80, ch - listY - bottomAreaH - padding);
            SetWindowPos(hList, NULL, listX, listY, listW, listH, SWP_NOZORDER | SWP_NOACTIVATE);
            AdjustListColumns(hList);
        }

        // Bottom controls: left-aligned checkboxes, center upgrade button, right-aligned unskip/refresh
        HWND hCheckAll = GetDlgItem(hwnd, IDC_BTN_SELECTALL);
        HWND hBtnUpgrade = GetDlgItem(hwnd, IDC_BTN_UPGRADE);
        HWND hBtnUnskip = GetDlgItem(hwnd, IDC_BTN_UNSKIP);
        HWND hBtnRefresh = GetDlgItem(hwnd, IDC_BTN_REFRESH);
        int btnH = 28;
        int checkW = 120;
        if (hCheckAll && IsWindow(hCheckAll)) SetWindowPos(hCheckAll, NULL, padding, ch - 44, checkW, 24, SWP_NOZORDER | SWP_NOACTIVATE);

        int refreshW = 140;
        int aboutW = 120;
        int unskipW = 100;
        int upgradeW = 220;
        int upgradeX = padding + checkW + 20;
        int refreshX = cw - padding - refreshW;
        // center Unskip between Upgrade (right edge) and Refresh (left edge)
        int upgradeRight = upgradeX + upgradeW;
        int unskipX = (upgradeRight + refreshX) / 2 - (unskipW / 2);
        if (hBtnUpgrade && IsWindow(hBtnUpgrade)) SetWindowPos(hBtnUpgrade, NULL, upgradeX, ch - 46, upgradeW, btnH+2, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hBtnUnskip && IsWindow(hBtnUnskip)) SetWindowPos(hBtnUnskip, NULL, unskipX, ch - 44, unskipW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hBtnRefresh && IsWindow(hBtnRefresh)) SetWindowPos(hBtnRefresh, NULL, refreshX, ch - 44, refreshW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

        // About button top-right
        HWND hBtnAbout = GetDlgItem(hwnd, IDC_BTN_ABOUT);
        if (hBtnAbout && IsWindow(hBtnAbout)) SetWindowPos(hBtnAbout, NULL, cw - padding - aboutW, 10, aboutW, 28, SWP_NOZORDER | SWP_NOACTIVATE);

        break;
    }
    case WM_APP+4: {
        // Reserved message: do not auto-destroy the install panel here to keep install output visible
        // This handler will only restore main controls if needed, but will not close panels.
        // wParam may contain a panel handle but we intentionally ignore it to avoid auto-closing.
        AppendLog("WM_APP+4: re-enable main controls (but not destroying panels)\n");
        if (hList) EnableWindow(hList, TRUE);
        if (hBtnRefresh) EnableWindow(hBtnRefresh, TRUE);
        if (hBtnUpgrade) EnableWindow(hBtnUpgrade, TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_SELECTALL), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_LANG), TRUE);
        break;
    }
    case WM_APP+6: {
        // wParam: 1 = show anim, 0 = hide anim
        HWND anim = g_hInstallAnim;
        if (anim && IsWindow(anim)) {
            if (wParam) ShowWindow(anim, SW_SHOW);
            else ShowWindow(anim, SW_HIDE);
        }
        break;
    }
    case WM_APP+8: {
        // Versions cache populated in background: refresh displayed version columns non-blocking
        // If an install panel is active and blocking destruction, avoid repopulating the listview
        if (g_install_block_destroy.load()) {
            AppendLog("WM_APP+8: versions cache ready but install panel active; skipping repopulate\n");
            break;
        }
        if (hList) {
            // repopulate only version columns to avoid flicker
            AppendLog("WM_APP+8: versions cache ready, repopulating listview columns\n");
            PopulateListView(hList);
            AdjustListColumns(hList);
            UpdateListViewHeaders(hList);
        }
        break;
    }
    case WM_APP+7: {
        // Bring main window forward (sent from background thread after installs finish)
        TryForceForegroundWindow(hwnd);
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;
        if (dis->CtlID == IDC_BTN_CONFIG) {
            HWND hBtn = dis->hwndItem;
            BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            COLORREF base = RGB(10,57,129);
            COLORREF hoverCol = RGB(25,95,210);
            COLORREF pressCol = RGB(6,34,80);
            HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
            SetTextColor(hdc, RGB(255,255,255));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hf = g_hLastUpdatedFont ? g_hLastUpdatedFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HGDIOBJ oldf = SelectObject(hdc, hf);
            DrawTextW(hdc, t("config_btn").c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldf);
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
            return 0;
        } else if (dis->CtlID == IDC_BTN_ABOUT) {
            HWND hBtn = dis->hwndItem;
            BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            // base dark-blue color and variations
            COLORREF base = RGB(10,57,129);
            COLORREF hoverCol = RGB(25,95,210);
            COLORREF pressCol = RGB(6,34,80);
            HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
            // draw text
            SetTextColor(hdc, RGB(255,255,255));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hf = g_hLastUpdatedFont ? g_hLastUpdatedFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HGDIOBJ oldf = SelectObject(hdc, hf);
            DrawTextW(hdc, t("about_btn").c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldf);
            if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
            return 0;
        }
        break;
    }
    case WM_INSTALL_DONE: {
        HWND panel = (HWND)wParam;
        if (panel && IsWindow(panel)) {
            // Only enable Done if this matches the current tracked install panel
            if (g_hInstallPanel == panel) {
                HWND hDoneBtn = GetDlgItem(hwnd, IDC_BTN_DONE);
                if (hDoneBtn) EnableWindow(hDoneBtn, TRUE);
            }
            // Do NOT overwrite the install status label here; leave final status/log visible
            // (status label is updated live by the install monitor thread). Avoid showing 'Your system is updated' here.
            // hide animation when finished
            if (g_hInstallAnim) PostMessageW(hwnd, WM_APP+6, 0, 0);
            // log install finished and that we are waiting for user acknowledgement
            AppendLog("WM_INSTALL_DONE: install finished; awaiting Continue press.\n");
            // NOTE: Do NOT re-enable or destroy main UI/panel here. Keep the install panel visible
            // until the user clicks Done so they can review all install output. Done handler will
            // re-enable controls and perform the refresh when the user is satisfied.
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == LOADING_TIMER_ID) {
            // loading timer is handled by the popup window; ignore here
            return 0;
        } else if (g_systemTray) {
            if (wParam == TIMER_SCAN) {
                // Time for periodic scan (automatic, not manual)
                g_systemTray->TriggerScan(false);
            } else if (wParam == TIMER_TOOLTIP) {
                // Update tooltip with next scan time
                g_systemTray->UpdateNextScanTime();
            }
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (!pnm) break;
        if (pnm->idFrom == IDC_LISTVIEW) {
            if (pnm->code == LVN_ITEMCHANGING) {
                LPNMLISTVIEW p = (LPNMLISTVIEW)lParam;
                if (p && (p->uChanged & LVIF_STATE)) {
                    // detect checkbox state change via state image mask
                    if (((p->uNewState ^ p->uOldState) & LVIS_STATEIMAGEMASK) != 0) {
                        int idx = p->iItem;
                        if (IsItemNotApplicable(idx)) {
                            // cancel the change to prevent checking NotApplicable items
                            return TRUE;
                        }
                    }
                }
            } else if (pnm->code == NM_CLICK) {
                // Handle clicks to toggle Skip state in column 2
                HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                POINT pt; GetCursorPos(&pt); ScreenToClient(hListLocal, &pt);
                LVHITTESTINFO ht{}; ht.pt = pt;
                int idx = ListView_HitTest(hListLocal, &ht);
                if (idx >= 0) {
                    int sub = ht.iSubItem;
                    if (sub == 3) {
                        // toggle skip for this item
                        std::string id = g_packages[idx].first;
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        auto it = g_skipped_versions.find(id);
                        if (it != g_skipped_versions.end()) {
                            // confirm unskip
                            if (MessageBoxW(hwnd, t("confirm_unskip").c_str(), t("app_title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                g_skipped_versions.erase(it);
                                SaveSkipConfig(g_locale);
                                PopulateListView(hListLocal);
                                UpdateUnskipButton(hwnd);
                            }
                        } else {
                            // determine available version for this id and add to skip config, confirm
                            try {
                                auto avail = MapAvailableVersions();
                                std::string ver = "";
                                auto f = avail.find(id);
                                if (f != avail.end()) ver = f->second;
                                if (!ver.empty()) {
                                    if (MessageBoxW(hwnd, t("confirm_skip").c_str(), t("app_title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                        g_skipped_versions[id] = ver;
                                        SaveSkipConfig(g_locale);
                                        PopulateListView(hListLocal);
                                        // After the user confirms a Skip, trigger a background rescan
                                        // so the list will be refreshed for subsequent UI changes.
                                        if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
                                    }
                                } else {
                                    MessageBoxW(hwnd, L"Unable to determine version to skip.", t("app_title").c_str(), MB_OK | MB_ICONWARNING);
                                }
                            } catch(...) {}
                        }
                    } else if (sub == 4) {
                        // Handle Exclude column click
                        std::string id = g_packages[idx].first;
                        std::string name = g_packages[idx].second;
                        std::wstring wname = Utf8ToWide(name);
                        
                        if (IsExcluded(id)) {
                            // Already excluded - confirm unexclude
                            std::wstring msg = t("confirm_unexclude");
                            size_t pos = msg.find(L"{0}");
                            if (pos != std::wstring::npos) {
                                msg.replace(pos, 3, wname);
                            }
                            if (MessageBoxW(hwnd, msg.c_str(), t("app_title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                UnexcludeApp(id);
                                PopulateListView(hListLocal);
                                // Trigger rescan to show the newly unexcluded app
                                if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
                            }
                        } else {
                            // Not excluded - confirm exclude
                            std::wstring msg = t("confirm_exclude");
                            size_t pos = msg.find(L"{0}");
                            if (pos != std::wstring::npos) {
                                msg.replace(pos, 3, wname);
                            }
                            if (MessageBoxW(hwnd, msg.c_str(), t("app_title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                ExcludeApp(id, "manual");
                                PopulateListView(hListLocal);
                                // Trigger rescan to remove the excluded app from the list
                                if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
                            }
                        }
                    }
                }
            } else if (pnm->code == NM_CUSTOMDRAW) {
                LPNMTVCUSTOMDRAW cd = (LPNMTVCUSTOMDRAW)lParam;
                // Use custom draw for listview items to gray out NotApplicable text
                LPNMLVCUSTOMDRAW lvc = (LPNMLVCUSTOMDRAW)lParam;
                switch (lvc->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    // clear hyperlink tracking before painting items
                    {
                        HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                        if (hListLocal) Hyperlink_Clear(hListLocal);
                    }
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int idx = (int)lvc->nmcd.dwItemSpec;
                    if (IsItemNotApplicable(idx)) {
                        lvc->clrText = RGB(160,160,160);
                    }
                    return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                }
                case CDDS_SUBITEM | CDDS_ITEMPREPAINT: {
                    // Custom draw subitems 1 and 2 (Installed/Available) with right alignment
                    int idx = (int)lvc->nmcd.dwItemSpec;
                    int sub = lvc->iSubItem;
                    if (sub == 1 || sub == 2) {
                        HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                        if (hListLocal && IsWindow(hListLocal)) {
                            RECT tr{};
                            if (ListView_GetSubItemRect(hListLocal, idx, sub, LVIR_LABEL, &tr)) {
                                wchar_t buf[256] = {0};
                                LVITEMW _lvit{};
                                _lvit.iSubItem = sub;
                                _lvit.cchTextMax = (int)(sizeof(buf)/sizeof(buf[0]));
                                _lvit.pszText = buf;
                                SendMessageW(hListLocal, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&_lvit);
                                
                                // Fill background
                                FillRect(lvc->nmcd.hdc, &tr, (HBRUSH)(COLOR_WINDOW + 1));
                                
                                // Draw text right-aligned
                                SetTextColor(lvc->nmcd.hdc, lvc->clrText);
                                SetBkMode(lvc->nmcd.hdc, TRANSPARENT);
                                SelectObject(lvc->nmcd.hdc, g_hListFont);
                                DrawTextW(lvc->nmcd.hdc, buf, -1, &tr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                                
                                return CDRF_SKIPDEFAULT;
                            }
                        }
                    }
                    return CDRF_NOTIFYPOSTPAINT | CDRF_DODEFAULT;
                }
                case CDDS_ITEMPOSTPAINT: {
                    int idx = (int)lvc->nmcd.dwItemSpec;
                    HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                    if (hListLocal && IsWindow(hListLocal)) {
                        // Draw hyperlinks for Skip (column 3) and Exclude (column 4)
                        for (int sub : {3, 4}) {
                            RECT tr{};
                            if (ListView_GetSubItemRect(hListLocal, idx, sub, LVIR_BOUNDS, &tr)) {
                                wchar_t buf[256] = {0};
                                LVITEMW _lvit{};
                                _lvit.iSubItem = sub;
                                _lvit.cchTextMax = (int)(sizeof(buf)/sizeof(buf[0]));
                                _lvit.pszText = buf;
                                SendMessageW(hListLocal, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&_lvit);
                                // determine hover by hit-testing current cursor position
                                POINT pt; GetCursorPos(&pt); ScreenToClient(hListLocal, &pt);
                                LVHITTESTINFO ht{}; ht.pt = pt;
                                int hit = ListView_HitTest(hListLocal, &ht);
                                bool hovered = (hit == idx && ht.iSubItem == sub);
                                DrawAndTrackHyperlink(lvc->nmcd.hdc, hListLocal, tr, std::wstring(buf), g_hListFont, hovered, idx, sub);
                            }
                        }
                    }
                    return CDRF_DODEFAULT;
                }
                }
            }
        }
        break;
    }
    case WM_TRAYICON:
        // Handle system tray icon messages
        {
            std::ostringstream _log;
            _log << "WM_TRAYICON received: wParam=" << wParam << ", lParam=0x" << std::hex << lParam << std::dec << "\n";
            AppendLog(_log.str());
        }
        if (g_systemTray) {
            return SystemTray::HandleTrayMessage(hwnd, wParam, lParam);
        }
        break;
    case WM_COMMAND: {
        // Log incoming WM_COMMAND for debugging (id, hiword, lParam)
        {
            std::ostringstream _wclog; _wclog << "WM_COMMAND received: id=" << (int)LOWORD(wParam) << " hi=" << (int)HIWORD(wParam) << " lParam=" << (void*)lParam << "\n";
            AppendLog(_wclog.str());
        }
        int id = LOWORD(wParam);
        AppendLog("Checking id=" + std::to_string(id) + " (IDC_BTN_UPGRADE=" + std::to_string(IDC_BTN_UPGRADE) + ")\n");
        
        // DIRECT HANDLER FOR IDC_BTN_UPGRADE - bypassing if/else-if chain
        if (id == IDC_BTN_UPGRADE) {
            AppendLog("IDC_BTN_UPGRADE DIRECT handler entered\n");
            std::vector<std::string> toInstall;
            HWND hList = GetDlgItem(hwnd, IDC_LISTVIEW);
            int count = ListView_GetItemCount(hList);
            for (int i = 0; i < count; ++i) {
                if (ListView_GetCheckState(hList, i)) {
                    LVITEMW lvi{};
                    wchar_t buf[512];
                    lvi.iItem = i; lvi.iSubItem = 0; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.pszText = buf; lvi.cchTextMax = _countof(buf);
                    SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi);
                    int idx = (int)lvi.lParam;
                    if (idx >= 0 && idx < (int)g_packages.size()) {
                        toInstall.push_back(g_packages[idx].first);
                    }
                }
            }
            if (toInstall.empty()) {
                MessageBoxW(hwnd, t("no_packages_selected").c_str(), t("app_title").c_str(), MB_OK | MB_ICONWARNING);
            } else {
                ShowInstallDialog(hwnd, toInstall, t("install_done"), [](const char* key) { return t(key); });
                PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
            }
            break;
        }
        
        AppendLog("  IDCLOSE=" + std::to_string(IDCLOSE) + " IDC_COMBO_LANG=" + std::to_string(IDC_COMBO_LANG) + " IDC_BTN_REFRESH=" + std::to_string(IDC_BTN_REFRESH) + "\n");
        AppendLog("About to check if (id == IDCLOSE)\n");
        if (id == IDCLOSE) {
            // Ctrl+W accelerator
            AppendLog("IDCLOSE matched, posting WM_CLOSE\n");
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        } else if (id == IDC_COMBO_LANG && HIWORD(wParam) == CBN_SELCHANGE) {
            AppendLog("COMBO_LANG matched\n");
            HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_LANG);
                if (hCombo) {
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                std::string newloc = (sel == 1) ? "nb_NO" : (sel == 2) ? "sv_SE" : "en_GB";
                g_locale = newloc;
                LoadLocaleFromFile(g_locale);
                SaveLocaleSetting(g_locale);
                // update UI texts
                UpdateLastUpdatedLabel(hwnd);
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_SELECTALL), t("select_all").c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_UPGRADE), t("upgrade_now").c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_REFRESH), t("refresh").c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_UNSKIP), t("unskip_btn").c_str());
                // update listview column headers
                HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                if (hListLocal) {
                    UpdateListViewHeaders(hListLocal);
                    AdjustListColumns(hListLocal);
                    // Re-populate ListView to update Skip column text with new translation
                    PopulateListView(hListLocal);
                }
                // update window title but do not translate app name
                std::wstring winTitle = std::wstring(L"WinUpdate - ") + t("app_window_suffix");
                SetWindowTextW(hwnd, winTitle.c_str());
                // Update button texts
                HWND hBtnUnskip = GetDlgItem(hwnd, IDC_BTN_UNSKIP);
                if (hBtnUnskip) SetWindowTextW(hBtnUnskip, t("unskip_btn").c_str());
                HWND hBtnConfig = GetDlgItem(hwnd, IDC_BTN_CONFIG);
                if (hBtnConfig) SetWindowTextW(hBtnConfig, t("config_btn").c_str());
                HWND hBtnAbout = GetDlgItem(hwnd, IDC_BTN_ABOUT);
                if (hBtnAbout) SetWindowTextW(hBtnAbout, t("about_btn").c_str());
                // Inform the user about the language change with an info icon
                std::string lang_key = (sel == 0) ? "lang_changed_en" : (sel == 1) ? "lang_changed_nb" : "lang_changed_sv";
                MessageBoxW(hwnd, t(lang_key.c_str()).c_str(), t("app_title").c_str(), MB_OK | MB_ICONINFORMATION);
            }
            break;
        } else if (id == IDC_BTN_REFRESH) {
            AppendLog("IDC_BTN_REFRESH handler\n");
            // update timestamp and start async refresh
            UpdateLastUpdatedLabel(hwnd);
            if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
            break;
        } else if (id == IDC_BTN_ABOUT) {
            // Re-enabled for development
            ShowAboutDialog(hwnd);
            break;
        } else if (id == IDC_BTN_CONFIG) {
            ShowConfigDialog(hwnd, g_locale);
            
            // Check if "Add to systray now" was clicked
            if (WasAddToTrayNowClicked()) {
                // Initialize and add to system tray immediately
                if (!g_systemTray) {
                    g_systemTray = new SystemTray();
                }
                
                g_systemTray->Initialize(hwnd);
                g_systemTray->AddToTray();
                
                // Load polling interval from settings
                char buf[MAX_PATH];
                DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
                std::string settingsPath = (len > 0 && len < MAX_PATH) ? 
                    (std::string(buf) + "\\WinUpdate\\wup_settings.ini") : "wup_settings.ini";
                
                int pollingInterval = 2; // default
                std::ifstream ifs(settingsPath);
                if (ifs) {
                    std::string line;
                    while (std::getline(ifs, line)) {
                        if (line.find("polling_interval") != std::string::npos) {
                            size_t eq = line.find('=');
                            if (eq != std::string::npos) {
                                std::string val = line.substr(eq + 1);
                                pollingInterval = std::stoi(val);
                            }
                        }
                    }
                }
                
                // Start scan timer and tooltip timer
                g_systemTray->StartScanTimer(pollingInterval);
                g_systemTray->StartTooltipTimer();
                
                // Trigger immediate scan
                g_systemTray->TriggerScan();
                
                // Hide main window
                ShowWindow(hwnd, SW_HIDE);
            }
            break;
        } else if (id == IDC_BTN_UNSKIP) {
            // Open Unskip dialog to allow removing skipped entries
            try {
                bool changed = ShowUnskipDialog(hwnd);
                if (changed) {
                    // refresh skip config in-memory and persist
                    try { SaveSkipConfig(g_locale); } catch(...) {}
                    if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
                }
            } catch(...) {}
            break;
        } else if (id == IDM_SCAN_NOW) {
            // System tray "Scan now!" clicked
            if (g_systemTray) {
                g_systemTray->TriggerScan();
            }
            break;
        } else if (id == IDM_OPEN_WINDOW) {
            // System tray "Open main window" clicked
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
        } else if (id == IDM_EXIT) {
            // System tray "Exit" clicked
            if (g_systemTray) {
                g_systemTray->RemoveFromTray();
                delete g_systemTray;
                g_systemTray = nullptr;
            }
            PostQuitMessage(0);
            break;
        } else if (id == IDC_BTN_DONE) {
            AppendLog("IDC_BTN_DONE check, id=" + std::to_string(id) + " IDC_BTN_DONE=" + std::to_string(IDC_BTN_DONE) + "\n");
            // Protect against programmatic or accidental WM_COMMAND posts: only accept real button clicks
            // from the control (lParam will contain the HWND of the control for true clicks). If lParam
            // is zero, ignore the command to avoid auto-continuation.
            // Only accept clicks coming from the actual done button control
            HWND hDoneCtrl = GetDlgItem(hwnd, IDC_BTN_DONE);
            if ((HWND)lParam != hDoneCtrl) {
                // ignore synthetic/posted commands or clicks from other controls
                break;
            }
            // User clicked Done on the install panel: delete temp file
            try { if (!g_last_install_outfile.empty()) { DeleteFileW(g_last_install_outfile.c_str()); g_last_install_outfile.clear(); } } catch(...) {}

            // If configured, restart the app to guarantee a fresh scan/refresh.
            if (g_restart_on_continue.load()) {
                AppendLog("Done pressed: restarting application as requested.\n");
                WCHAR exePath[MAX_PATH] = {0};
                if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
                    STARTUPINFOW si{}; si.cb = sizeof(si);
                    PROCESS_INFORMATION pi{};
                    // Attempt to create a new process instance of the same executable
                    if (CreateProcessW(exePath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
                // Terminate current process so the fresh instance can continue.
                ExitProcess(0);
            }

            // allow panel destruction now that user acknowledged
            g_install_block_destroy.store(false);
            {
                AppendLog("Done pressed: clearing install-block and destroying panel.\n");
            }
            // Re-enable main UI controls now that user dismissed the install panel
            if (hList) EnableWindow(hList, TRUE);
            if (hBtnRefresh) EnableWindow(hBtnRefresh, TRUE);
            if (hBtnUpgrade) EnableWindow(hBtnUpgrade, TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_SELECTALL), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_COMBO_LANG), TRUE);
            // trigger refresh (user requested) and then close panel
            if (!g_refresh_in_progress.load()) PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
            // destroy the tracked install panel only
            if (g_hInstallPanel && IsWindow(g_hInstallPanel)) {
                DestroyWindow(g_hInstallPanel);
                g_hInstallPanel = NULL;
            }
            break;
        } else if (id == IDC_BTN_SELECTALL) {
            AppendLog("IDC_BTN_SELECTALL handler entered, id=" + std::to_string(id) + " IDC_BTN_SELECTALL=" + std::to_string(IDC_BTN_SELECTALL) + "\n");
            // Check all items in the list when button is clicked
            HWND hList = GetDlgItem(hwnd, IDC_LISTVIEW);
            if (!hList) {
                AppendLog("ERROR: hList is NULL\n");
            } else {
                int itemCount = ListView_GetItemCount(hList);
                AppendLog("ListView item count: " + std::to_string(itemCount) + "\n");
                CheckAllItems(hList, TRUE);
                AppendLog("CheckAllItems completed\n");
            }
            break;
        } else if (id == IDC_BTN_UPGRADE) {
            AppendLog("IDC_BTN_UPGRADE handler entered\n");
            // Collect checked items
            std::vector<std::string> toInstall;
            HWND hList = GetDlgItem(hwnd, IDC_LISTVIEW);
            int count = ListView_GetItemCount(hList);
            AppendLog("Item count: " + std::to_string(count) + "\n");
            for (int i = 0; i < count; ++i) {
                if (ListView_GetCheckState(hList, i)) {
                    LVITEMW lvi{};
                    wchar_t buf[512];
                    lvi.iItem = i; lvi.iSubItem = 0; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.pszText = buf; lvi.cchTextMax = _countof(buf);
                    SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi);
                    int idx = (int)lvi.lParam;
                    if (idx >= 0 && idx < (int)g_packages.size()) {
                        toInstall.push_back(g_packages[idx].first);
                    }
                }
            }
            AppendLog("toInstall size: " + std::to_string(toInstall.size()) + "\n");
            if (toInstall.empty()) {
                AppendLog("Showing your_system_updated message\n");
                MessageBoxW(hwnd, t("your_system_updated").c_str(), t("app_title").c_str(), MB_OK | MB_ICONINFORMATION);
            } else {
                AppendLog("Calling ShowInstallDialog\n");
                // Show modal install dialog
                ShowInstallDialog(hwnd, toInstall, t("install_done"), [](const char* key) { return t(key); });
                
                // After install completes, trigger a refresh
                PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);
            }
        } else {
            AppendLog("Unhandled WM_COMMAND id=" + std::to_string(id) + "\n");
        }
        break;
    }
    
    case WM_CLOSE:
        // Force close any open dialogs first
        EnumThreadWindows(GetCurrentThreadId(), [](HWND hwndEnum, LPARAM lParam) -> BOOL {
            HWND hwndMain = (HWND)lParam;
            if (hwndEnum != hwndMain && IsWindow(hwndEnum) && IsWindowVisible(hwndEnum)) {
                HWND parent = GetParent(hwndEnum);
                if (parent == hwndMain || parent == NULL) {
                    // This is a dialog or top-level window, destroy it
                    DestroyWindow(hwndEnum);
                }
            }
            return TRUE;
        }, (LPARAM)hwnd);
        
        // If system tray is active, hide window instead of destroying
        if (g_systemTray && g_systemTray->IsActive()) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        // Otherwise, allow normal close/destroy
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    
    case WM_SHOW_FROM_SECOND_INSTANCE:
        // Another instance was launched - show this window and trigger a scan
        ShowWindow(hwnd, SW_SHOW);
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        // Trigger a refresh scan if one isn't already running
        if (!g_refresh_in_progress.load()) {
            PostMessageW(hwnd, WM_REFRESH_ASYNC, 1, 0);  // 1 = manual refresh
        }
        break;
    
    case WM_DESTROY:
        if (g_hLastUpdatedFont) {
            DeleteObject(g_hLastUpdatedFont);
            g_hLastUpdatedFont = NULL;
        }
        if (g_hTitleFont) {
            DeleteObject(g_hTitleFont);
            g_hTitleFont = NULL;
        }
        if (g_hInstallPanel && IsWindow(g_hInstallPanel)) {
            DestroyWindow(g_hInstallPanel);
            g_hInstallPanel = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    // Check if another instance is already running
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"WinUpdate_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running - find its window and tell it to show itself and scan
        HWND hwndExisting = FindWindowW(CLASS_NAME, NULL);
        if (hwndExisting) {
            // Send custom message to show window and trigger scan
            PostMessageW(hwndExisting, WM_SHOW_FROM_SECOND_INSTANCE, 0, 0);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }
    
    // Check for command-line parameters
    std::wstring cmdLine(pCmdLine ? pCmdLine : L"");
    
    // --hidden: Run hidden scan - only show UI if updates found
    if (cmdLine.find(L"--hidden") != std::wstring::npos) {
        // Initialize translations first
        InitDefaultTranslations();
        std::string saved = LoadLocaleSetting();
        if (!saved.empty()) {
            g_locale = saved;
        }
        LoadLocaleFromFile(g_locale);
        LoadSkipConfig(g_locale);
        
        // Perform hidden scan
        PerformHiddenScan();
        
        // Exit after scan (if UI shown, it will be a separate process)
        return 0;
    }
    
    // --systray: Force systray mode (hide window initially)
    bool forceSysTray = (cmdLine.find(L"--systray") != std::wstring::npos);
    
    WNDCLASSW wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) return 0;

    // Initialize translations: prefer saved language, then environment/OS locale
    InitDefaultTranslations();
    // First check settings file for saved language
    std::string saved = LoadLocaleSetting();
    if (!saved.empty()) {
        g_locale = saved;
    } else {
        // try to read environment LANG or LC_ALL
        char *env = nullptr;
        size_t envsz = 0;
        std::string sysloc;
        try {
            if (_dupenv_s(&env, &envsz, "LANG") == 0 && env) sysloc = std::string(env);
            else if (_dupenv_s(&env, &envsz, "LC_ALL") == 0 && env) sysloc = std::string(env);
        } catch(...) { }
        if (env) { free(env); env = nullptr; }
        if (!sysloc.empty()) {
            size_t p = sysloc.find_first_of("._");
            std::string prefix = (p==std::string::npos) ? sysloc : sysloc.substr(0,p);
            g_locale = prefix;
        }
        if (g_locale.empty()) {
            WCHAR buf[32] = {0};
            if (GetUserDefaultLocaleName(buf, (int)std::size(buf))) {
                std::wstring wln(buf);
                if (wln.size() >= 2) g_locale = std::string(wln.begin(), wln.begin()+2);
            }
        }
        if (g_locale.empty()) g_locale = "en";
    }
    // attempt to load translations for the locale
    LoadLocaleFromFile(g_locale);
    // load per-locale skip configuration
    LoadSkipConfig(g_locale);
    // load excluded apps
    LoadExcludeSettings(g_excluded_apps);

    std::wstring winTitle = std::wstring(L"WinUpdate - ") + t("app_window_suffix");
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, winTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 430, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    
    // Store main window handle globally
    g_hMainWindow = hwnd;
    
    // load and set application icons (embedded in resources)
    HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    
    // Check if we should start in system tray mode (Mode 2)
    bool startInTray = false;
    char buf[MAX_PATH];
    DWORD lenEnv = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string settingsPath = (lenEnv > 0 && lenEnv < MAX_PATH) ? 
        (std::string(buf) + "\\WinUpdate\\wup_settings.ini") : "wup_settings.ini";
    
    int mode = 0;
    int pollingInterval = 2;
    std::ifstream ifs(settingsPath);
    if (ifs) {
        std::string line;
        bool inSystemTray = false;
        while (std::getline(ifs, line)) {
            if (line.find("[systemtraystatus]") != std::string::npos) {
                inSystemTray = true;
            } else if (line[0] == '[') {
                inSystemTray = false;
            } else if (inSystemTray) {
                if (line.find("mode=") != std::string::npos) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos) {
                        std::string val = line.substr(eq + 1);
                        mode = std::stoi(val);
                    }
                } else if (line.find("polling_interval") != std::string::npos) {
                    size_t eq = line.find('=');
                    if (eq != std::string::npos) {
                        std::string val = line.substr(eq + 1);
                        pollingInterval = std::stoi(val);
                    }
                }
            }
        }
    }
    
    if (mode == 2 || forceSysTray) {
        startInTray = true;
        // Don't show window initially
        nCmdShow = SW_HIDE;
    }
    
    // Verify startup shortcut matches mode - fix if needed
    VerifyStartupShortcut(mode);
    
    ShowWindow(hwnd, nCmdShow);
    
    // If starting in system tray mode, initialize tray icon
    if (startInTray) {
        g_systemTray = new SystemTray();
        g_systemTray->Initialize(hwnd);
        g_systemTray->AddToTray();
        g_systemTray->StartScanTimer(pollingInterval);
        g_systemTray->StartTooltipTimer();
        
        // Set initial tooltip with "no updates" status (before first scan)
        g_systemTray->UpdateNextScanTime(t("tray_no_updates"));
        
        // Trigger immediate scan on startup (will run silently in background)
        g_systemTray->TriggerScan();
    } else {
        // Normal mode: show loading animation and trigger initial scan
        ShowLoading(hwnd);
        if (!g_refresh_in_progress.load()) {
            PostMessageW(hwnd, WM_REFRESH_ASYNC, 0, 0);
        }
    }

    // Create accelerator table for Ctrl+W
    ACCEL accel[1];
    accel[0].fVirt = FVIRTKEY | FCONTROL;
    accel[0].key = 'W';
    accel[0].cmd = IDCLOSE;
    HACCEL hAccel = CreateAcceleratorTableW(accel, 1);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    // Cleanup accelerator table
    if (hAccel) {
        DestroyAcceleratorTable(hAccel);
    }
    
    // Cleanup mutex on exit
    if (hMutex) {
        CloseHandle(hMutex);
    }
    
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LPWSTR cmdLineW = GetCommandLineW();
    // If invoked with --debug-parse, run winget (text) and print parsed entries to console then exit
    std::wstring cmdLine(cmdLineW ? cmdLineW : L"");
    if (cmdLine.find(L"--debug-parse") != std::wstring::npos) {
        // create a console to print results
        AllocConsole();
        FILE *f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        // Read the run log (it contains prior winget captures which are more stable)
        std::string logtxt = ReadFileUtf8(L"wup_run_log.txt");
        if (logtxt.empty()) {
            // fallback to running winget directly
            std::wstring cmd = L"cmd /C winget upgrade --accept-source-agreements --accept-package-agreements";
            auto res = RunProcessCaptureExitCode(cmd, 15000);
            logtxt = res.second;
        }
        if (logtxt.empty()) {
            printf("No winget output available (run log empty).\n");
        } else {
            // Try fast parse of fresh winget upgrade output first
            std::set<std::pair<std::string,std::string>> found;
            std::wstring cmd = L"cmd /C winget upgrade --accept-source-agreements --accept-package-agreements";
            auto res = RunProcessCaptureExitCode(cmd, 15000);
            std::string listOut;
            if (!res.second.empty()) {
                // prefer list-based mapping first
                auto resList = RunProcessCaptureExitCode(L"cmd /C winget list", 8000);
                std::string listOut = resList.second;
                if (!listOut.empty() && !res.second.empty()) {
                    FindUpdatesUsingKnownList(listOut, res.second, found);
                }
                // If upgrade output indicates the 'not applicable' banner, mark list-based candidates as NotApplicable
                bool sawNotApplicable = (res.second.find("A newer package version is available in a configured source, but it does not apply to your system or requirements") != std::string::npos);
                if (sawNotApplicable && !listOut.empty()) {
                    // If winget reports 'not applicable', avoid trusting the whitespace parser (it can produce false positives).
                    // Instead, collect NotApplicable ids from the list output and report them.
                    std::istringstream lss2(listOut);
                    std::string line3;
                    std::set<std::string> localNA2;
                    std::regex anyRe2("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m3;
                    while (std::getline(lss2, line3)) {
                        if (std::regex_search(line3, m3, anyRe2)) {
                            std::string id = m3[2].str();
                            std::string installed = m3[3].str();
                            std::string available = m3[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA2.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA2.empty()) {
                        printf("No applicable updates (all candidates are NotApplicable)\n");
                        printf("NotApplicable IDs:\n");
                        for (auto &s : localNA2) printf("%s\n", s.c_str());
                        return 0;
                    }
                }
                if (found.empty()) {
                    ParseUpgradeFast(res.second, found);
                }
                if (found.empty() && sawNotApplicable && !listOut.empty()) {
                    // collect ids from list where available > installed and report as NotApplicable
                    std::istringstream lss(listOut);
                    std::string line2;
                    std::set<std::string> localNA;
                    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m2;
                    while (std::getline(lss, line2)) {
                        if (std::regex_search(line2, m2, anyRe)) {
                            std::string id = m2[2].str();
                            std::string installed = m2[3].str();
                            std::string available = m2[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA.empty()) {
                        printf("No applicable updates (all candidates are NotApplicable)\n");
                        printf("NotApplicable IDs:\n");
                        for (auto &s : localNA) printf("%s\n", s.c_str());
                        return 0;
                    }
                }
            }
            // If we have a list output, probe candidate ids for NotApplicable status
            std::set<std::string> candidateIds;
            if (!listOut.empty()) {
                std::istringstream lss(listOut);
                std::string lline;
                std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                std::smatch mm;
                while (std::getline(lss, lline)) {
                    if (std::regex_search(lline, mm, anyRe)) {
                        std::string id = mm[2].str();
                        std::string installed = mm[3].str();
                        std::string available = mm[4].str();
                        try { if (CompareVersions(installed, available) < 0) candidateIds.insert(id); } catch(...) {}
                    }
                }
            }

            std::set<std::string> localNA;
            // probe candidate ids that were not found by the generic parsing
            for (auto &id : candidateIds) {
                bool alreadyFound = false;
                for (auto &p : found) if (p.first == id) { alreadyFound = true; break; }
                if (alreadyFound) continue;
                // run per-id upgrade probe with short timeout
                std::wstring cmd = L"cmd /C winget upgrade --id \"" + std::wstring(id.begin(), id.end()) + L"\" --accept-source-agreements --accept-package-agreements";
                auto r = RunProcessCaptureExitCode(cmd, 2500);
                std::string out = r.second;
                if (out.find("does not apply to your system or requirements") != std::string::npos || out.find("No applicable upgrade found") != std::string::npos) {
                    localNA.insert(id);
                } else {
                    // if probe returns an applicable upgrade, try to extract id/name
                    std::set<std::pair<std::string,std::string>> f2;
                    ExtractUpdatesFromText(out, f2);
                    for (auto &pp : f2) if (pp.first == id) found.insert(pp);
                }
            }

            if (!localNA.empty()) {
                printf("NotApplicable IDs:\n");
                for (auto &s : localNA) printf("%s\n", s.c_str());
            }

            if (found.empty()) {
                printf("No updates found in run log.\n");
            } else {
                printf("Parsed packages (updates available):\n");
                for (auto &p : found) printf("Id: %s\tName: %s\n", p.first.c_str(), p.second.c_str());
            }
        }
        // exit immediately without pausing
        return 0;
    }
    return wWinMain(hInstance, hPrevInstance, cmdLineW, nCmdShow);
}
