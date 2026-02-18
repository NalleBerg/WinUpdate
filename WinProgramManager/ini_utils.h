#pragma once
#include <string>

// Read a value from an INI file, handling UTF-8/UTF-16/ANSI encodings.
std::wstring ReadIniValue(const std::wstring& iniPath, const std::wstring& section, const std::wstring& key);

// Write a minimal [Settings] INI as UTF-8 with BOM containing Language and UpdaterTaskCreated
bool WriteSettingsIniUtf8(const std::wstring& iniPath, const std::wstring& language, const std::wstring& updaterTaskCreated);
