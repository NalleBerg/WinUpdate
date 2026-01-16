// Lightweight winget versions helpers (extracted from main.cpp)
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

// Return mappings Id->InstalledVersion and Id->AvailableVersion
std::unordered_map<std::string,std::string> MapInstalledVersions();
std::unordered_map<std::string,std::string> MapAvailableVersions();

// exported thin wrappers used by the GUI to ensure the robust implementations are used
std::unordered_map<std::string,std::string> MapInstalledVersions_ext();
std::unordered_map<std::string,std::string> MapAvailableVersions_ext();

// Try various in-memory parsers to extract id->version pairs from raw winget text
std::vector<std::pair<std::string,std::string>> ParseRawWingetTextInMemory(const std::string &text);
