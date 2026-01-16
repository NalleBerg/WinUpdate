// Central definitions for globals referenced across translation units
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

// Package list: pair of <id, display name>
std::vector<std::pair<std::string,std::string>> g_packages;
std::mutex g_packages_mutex;

// In-memory storage of last winget upgrade output
std::string g_last_winget_raw;
std::mutex g_last_winget_raw_mutex;

// Refresh in progress flag
std::atomic<bool> g_refresh_in_progress{false};

// Total winget packages detected during last scan
std::atomic<int> g_total_winget_packages{11107};
