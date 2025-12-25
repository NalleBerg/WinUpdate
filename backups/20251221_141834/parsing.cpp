#include "parsing.h"
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
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                g_packages.emplace_back(id, name);
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
        if (CompareVersions(installed, available) < 0) outSet.emplace(id, name);
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
        if (!id.empty() && CompareVersions(installed, available) < 0) outSet.emplace(id, name);
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
            outSet.emplace(id, pkgmap[id]);
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
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) return;

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

    std::vector<int> colStarts;
    std::vector<std::string> colNames = {"Name","Id","Version","Available","Source"};
    for (auto &cn : colNames) {
        size_t p = header.find(cn);
        if (p != std::string::npos) colStarts.push_back((int)p);
    }
    if (colStarts.size() < 2) {
        for (int i = sepIdx + 1, lastAdded = -1; i < (int)lines.size(); ++i) {
            const std::string &ln = lines[i];
            if (ln.find("upgrades available") != std::string::npos) break;
            if (trim(ln).empty()) continue;
            std::istringstream ls(ln);
            std::vector<std::string> toks;
            std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() < 4) continue;
            std::regex verRe2(R"(^[0-9]+(\.[0-9]+)*$)");
            size_t n = toks.size();
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
                if (CompareVersions(installed, available) < 0) g_packages.emplace_back(id, name);
            } else {
                continue;
            }
        }
        return;
    }

    int lastAdded = -1;
    for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (ln.find("upgrades available") != std::string::npos) break;
        if (trim(ln).empty()) continue;
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
        g_packages.emplace_back(id, name);
        lastAdded = (int)g_packages.size()-1;
    }

    std::set<std::string> seenIds;
    for (auto &p : g_packages) seenIds.insert(p.first);
    std::regex verRe(R"(^[0-9]+(\.[0-9]+)*$)");
    for (const auto &ln : lines) {
        std::istringstream ls(ln);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
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
                    g_packages.emplace_back(id, name);
                    seenIds.insert(id);
                }
                break;
            }
        }
    }
}
