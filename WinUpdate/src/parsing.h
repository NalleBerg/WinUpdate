#pragma once
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <unordered_map>
#include <mutex>
#include <atomic>

// Externs for globals defined in main.cpp used by parsing functions
extern std::vector<std::pair<std::string,std::string>> g_packages;
extern std::mutex g_packages_mutex;
extern std::string g_last_winget_raw;
extern std::mutex g_last_winget_raw_mutex;
extern std::atomic<bool> g_refresh_in_progress;

// Parsing helpers
void ParseWingetTextForUpdates(const std::string &text);
void ParseUpgradeFast(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet);
void ExtractUpdatesFromText(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet);
void FindUpdatesUsingKnownList(const std::string &listText, const std::string &upgradeText, std::set<std::pair<std::string,std::string>> &outSet);
std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text);
std::string ReadMostRecentRawWinget();
void ParseWingetTextForPackages(const std::string &text);
