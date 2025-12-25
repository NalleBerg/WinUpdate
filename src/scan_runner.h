#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

// Run the scanning/parsing steps and return the available/installed maps.
// Returns true on success (maps populated), false otherwise.
bool ScanAndPopulateMaps(std::unordered_map<std::string,std::string> &avail, std::unordered_map<std::string,std::string> &inst);
// Run the full scan: produce candidate list (id,name) and available/installed maps.
// This blocks until winget output is parsed and maps are ready.
bool RunFullScan(std::vector<std::pair<std::string,std::string>> &outResults,
				 std::unordered_map<std::string,std::string> &avail,
				 std::unordered_map<std::string,std::string> &inst,
				 int timeoutMs = 12000);
