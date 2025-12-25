// Simple console helper: run `winget list` and `winget upgrade`, parse trailing tokens and print maps
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include <memory>
#include <unordered_map>

static std::string run_and_capture(const char *cmd) {
    std::string result;
    FILE *pipe = _popen(cmd, "r");
    if (!pipe) return result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    _pclose(pipe);
    return result;
}

static void parse_tail_table(const std::string &txt, std::unordered_map<std::string,std::string> &inst, std::unordered_map<std::string,std::string> &avail) {
    std::istringstream iss(txt);
    std::string line;
    bool seenSep = false;
    while (std::getline(iss, line)) {
        // trim
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        std::string t = line;
        while (!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        if (t.empty()) continue;
        if (!seenSep) { if (t.find("----") != std::string::npos) { seenSep = true; } continue; }
        if (t.find("upgrades available") != std::string::npos) break;
        std::istringstream ls(t);
        std::vector<std::string> toks; std::string tok;
        while (ls >> tok) toks.push_back(tok);
        if (toks.size() >= 4) {
            int n = (int)toks.size();
            std::string id = toks[n-4];
            std::string instv = toks[n-3];
            std::string availv = toks[n-2];
            inst[id] = instv;
            avail[id] = availv;
        }
    }
}

int main() {
    std::cout << "Capturing winget list...\n";
    std::string list = run_and_capture("cmd /C winget list");
    std::cout << list << "\n";
    std::cout << "Capturing winget upgrade...\n";
    std::string upg = run_and_capture("cmd /C winget upgrade");
    std::cout << upg << "\n";

    std::unordered_map<std::string,std::string> inst, avail;
    parse_tail_table(upg, inst, avail);

    std::cout << "\nInstalled map (id\tversion):\n";
    for (auto &p : inst) std::cout << p.first << '\t' << p.second << '\n';
    std::cout << "\nAvailable map (id\tversion):\n";
    for (auto &p : avail) std::cout << p.first << '\t' << p.second << '\n';
    return 0;
}
