#pragma once
#include <unordered_map>
#include <string>

// Run the scanning/parsing steps and return the available/installed maps.
// Returns true on success (maps populated), false otherwise.
bool ScanAndPopulateMaps(std::unordered_map<std::string,std::string> &avail, std::unordered_map<std::string,std::string> &inst);
