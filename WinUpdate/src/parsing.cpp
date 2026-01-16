#include "parsing.h"
#include "skip_update.h"
#include "logging.h"
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>

// Local static CompareVersions (originally in main.cpp) copied here to avoid
// cross-translation-unit linkage issues.
static int CompareVersions(const std::string &a, const std::string &b) {
    std::istringstream sa(a);
    std::istringstream sb(b);
    while (sa.good() || sb.good()) {
        std::string ta, tb;
        if (!(sa >> ta)) ta.clear();
        if (!(sb >> tb)) tb.clear();
        long va = 0, vb = 0;
        try { va = std::stol(ta.empty()?"0":ta); } catch(...) { va = 0; }
        try { vb = std::stol(tb.empty()?"0":tb); } catch(...) { vb = 0; }
        if (va < vb) return -1;
        if (va > vb) return 1;
        if (!sa.good() && !sb.good()) break;
    }
    return 0;
}

// Parse text output and pick only entries where an available version is greater
void ParseWingetTextForUpdates(const std::string &text) {
    // Reuse global package vector
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        g_packages.clear();
    }
    std::istringstream iss(text);
    std::string line;
    std::regex lineRe("^\\s*(.+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)\\s*$");
    std::smatch m;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        if (line.empty()) continue;
        if (std::regex_match(line, m, lineRe)) {
            std::string name = m[1].str();
            std::string id = m[2].str();
            std::string installed = m[3].str();
            std::string available = m[4].str();
            if (CompareVersions(installed, available) < 0) {
                try {
                    try { AppendLog(std::string("ParseWingetTextForUpdates: candidate id='") + id + "' avail='" + available + "' name='" + name + "'\n"); } catch(...) {}
                    bool skipped = false;
                    try { skipped = IsSkipped(id, available); } catch(...) { skipped = false; }
                    try { AppendLog(std::string("ParseWingetTextForUpdates: IsSkipped returned ") + (skipped?"true":"false") + std::string(" for id='") + id + "'\n"); } catch(...) {}
                    if (!skipped) {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        g_packages.emplace_back(id, name);
                    }
                } catch(...) {
                    std::lock_guard<std::mutex> lk(g_packages_mutex);
                    g_packages.emplace_back(id, name);
                }
            }
        }
    }
}

// Very fast upgrade output parser
void ParseUpgradeFast(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
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
        if (CompareVersions(installed, available) < 0) {
            try {
                try { AppendLog(std::string("ParseUpgradeFast: candidate id='") + id + "' avail='" + available + "' name='" + name + "'\n"); } catch(...) {}
                bool skipped = false;
                try { skipped = IsSkipped(id, available); } catch(...) { skipped = false; }
                try { AppendLog(std::string("ParseUpgradeFast: IsSkipped returned ") + (skipped?"true":"false") + std::string(" for id='") + id + "'\n"); } catch(...) {}
                if (!skipped) outSet.emplace(id, name);
            } catch(...) { outSet.emplace(id, name); }
        }
    }
}

// More tolerant extractor
void ExtractUpdatesFromText(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)");
    std::smatch m;
    std::string::const_iterator it = text.begin();
    while (std::regex_search(it, text.cend(), m, anyRe)) {
        std::string name = m[1].str();
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
        name = trim(name);
        if (!id.empty() && CompareVersions(installed, available) < 0) {
            try {
                try { AppendLog(std::string("ExtractUpdatesFromText: candidate id='") + id + "' avail='" + available + "' name='" + name + "'\n"); } catch(...) {}
                bool skipped = false;
                try { skipped = IsSkipped(id, available); } catch(...) { skipped = false; }
                try { AppendLog(std::string("ExtractUpdatesFromText: IsSkipped returned ") + (skipped?"true":"false") + std::string(" for id='") + id + "'\n"); } catch(...) {}
                if (!skipped) outSet.emplace(id, name);
            } catch(...) { outSet.emplace(id, name); }
        }
        it = m.suffix().first;
    }
}

// Build a map of Id->Name from a full winget listing then scan upgrade output
void FindUpdatesUsingKnownList(const std::string &listText, const std::string &upgradeText, std::set<std::pair<std::string,std::string>> &outSet) {
    // populate g_packages from the listText
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        ParseWingetTextForPackages(listText);
    }
    std::unordered_map<std::string,std::string> pkgmap;
    for (auto &p : g_packages) pkgmap[p.first] = p.second;
    if (pkgmap.empty() && !upgradeText.empty()) {
        auto extra = ExtractIdsFromNameIdText(upgradeText);
        for (auto &p : extra) pkgmap[p.first] = p.second;
    }
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        g_packages.clear();
    }
    if (pkgmap.empty()) return;

    std::regex anyRe(R"(([
\S ]+?)\s+([^\s]+)\s+(\d+(?:\.[0-9]+)*)\s+(\d+(?:\.[0-9]+)*))");
    std::smatch m;
    std::string::const_iterator it = upgradeText.begin();
    while (std::regex_search(it, upgradeText.cend(), m, anyRe)) {
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        if (!id.empty() && pkgmap.count(id) && CompareVersions(installed, available) < 0) {
            try {
                try { AppendLog(std::string("FindUpdatesUsingKnownList: candidate id='") + id + "' avail='" + available + "' name='" + pkgmap[id] + "'\n"); } catch(...) {}
                bool skipped = false;
                try { skipped = IsSkipped(id, available); } catch(...) { skipped = false; }
                try { AppendLog(std::string("FindUpdatesUsingKnownList: IsSkipped returned ") + (skipped?"true":"false") + std::string(" for id='") + id + "'\n"); } catch(...) {}
                if (!skipped) outSet.emplace(id, pkgmap[id]);
            } catch(...) { outSet.emplace(id, pkgmap[id]); }
        }
        it = m.suffix().first;
    }
}

std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text) {
    std::vector<std::pair<std::string,std::string>> ids;
    std::istringstream iss(text);
    std::string ln;
    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    while (std::getline(iss, ln)) {
        std::string t = trim(ln);
        if (t.empty()) continue;
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

std::string ReadMostRecentRawWinget() {
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
            std::ifstream ifs(best, std::ios::binary);
            if (ifs) {
                std::ostringstream ss; ss << ifs.rdbuf();
                return ss.str();
            }
        }
    } catch(...) {}
    return std::string();
}

// Parse winget full list table into g_packages
void ParseWingetTextForPackages(const std::string &text) {
    std::lock_guard<std::mutex> lk(g_packages_mutex);
    g_packages.clear();
    
    AppendLog(std::string("ParseWingetTextForPackages: input text length=") + std::to_string((int)text.size()) + "\n");
    
    std::istringstream iss(text);
    std::string line;
    bool pastHeader = false;
    int lineNum = 0;
    
    while (std::getline(iss, line)) {
        lineNum++;
        // Remove trailing newlines
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        
        // Skip until we find the separator line
        if (!pastHeader) {
            if (line.find("----") != std::string::npos) {
                pastHeader = true;
                AppendLog(std::string("ParseWingetTextForPackages: found separator at line ") + std::to_string(lineNum) + "\n");
            }
            continue;
        }
        
        // Stop at footer
        if (line.find("upgrades available") != std::string::npos) {
            AppendLog(std::string("ParseWingetTextForPackages: found footer at line ") + std::to_string(lineNum) + "\n");
            break;
        }
        
        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        
        // Split line into tokens
        std::istringstream ls(line);
        std::vector<std::string> tokens;
        std::string tok;
        while (ls >> tok) tokens.push_back(tok);
        
        AppendLog(std::string("ParseWingetTextForPackages: line ") + std::to_string(lineNum) + " has " + std::to_string((int)tokens.size()) + " tokens\n");
        
        // Need at least 5 tokens: Name Id Version Available Source
        if (tokens.size() < 5) {
            AppendLog(std::string("ParseWingetTextForPackages: SKIP line ") + std::to_string(lineNum) + " - not enough tokens\n");
            continue;
        }
        
        size_t n = tokens.size();
        // Parse from right to left:
        // tokens[n-1] = Source (e.g., "winget")
        // tokens[n-2] = Available version
        // tokens[n-3] = Current version
        // tokens[n-4] = Package ID
        // tokens[0 .. n-5] = Name (can contain spaces)
        
        std::string source = tokens[n-1];
        std::string available = tokens[n-2];
        std::string version = tokens[n-3];
        std::string id = tokens[n-4];
        
        // Build name from remaining tokens
        std::string name;
        for (size_t i = 0; i + 4 < n; ++i) {
            if (i > 0) name += " ";
            name += tokens[i];
        }
        if (name.empty()) name = id;
        
        AppendLog(std::string("ParseWingetTextForPackages: parsed id='") + id + "' name='" + name + "' avail='" + available + "'\n");
        
        // Check if this package should be filtered out (skipped)
        try {
            bool skipped = IsSkipped(id, available);
            if (!skipped) {
                g_packages.emplace_back(id, name);
                AppendLog(std::string("ParseWingetTextForPackages: ADDED id='") + id + "' name='" + name + "'\n");
            } else {
                AppendLog(std::string("ParseWingetTextForPackages: SKIPPED id='") + id + "' (user skipped)\n");
            }
        } catch(...) {
            // If IsSkipped fails, add it anyway
            g_packages.emplace_back(id, name);
            AppendLog(std::string("ParseWingetTextForPackages: ADDED id='") + id + "' (IsSkipped threw)\n");
        }
    }
    
    AppendLog(std::string("ParseWingetTextForPackages: finished, g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
}
