#include "WinProgramUpdater.h"
#include <windows.h>
#include <shlobj.h>
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <unordered_set>

WinProgramUpdater::WinProgramUpdater(const std::wstring& dbPath)
    : db_(nullptr), searchDb_(nullptr), dbPath_(dbPath),
      logCallback_(nullptr), logUserData_(nullptr),
      statsCallback_(nullptr), statsUserData_(nullptr),
      cancelFlag_(nullptr) {
    // Set search database path in same directory as main database
    size_t lastSlash = dbPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        searchDbPath_ = dbPath.substr(0, lastSlash + 1) + L"WinProgramsSearch.db";
    } else {
        searchDbPath_ = L"WinProgramsSearch.db";
    }
    InitializeTagPatterns();
}

WinProgramUpdater::~WinProgramUpdater() {
    CloseDatabase();
    CloseSearchDatabase();
}

// ========== Callback Setters ==========

void WinProgramUpdater::SetLogCallback(LogCallbackFunc callback, void* userData) {
    logCallback_ = callback;
    logUserData_ = userData;
}

void WinProgramUpdater::SetStatsCallback(StatsCallbackFunc callback, void* userData) {
    statsCallback_ = callback;
    statsUserData_ = userData;
}

void WinProgramUpdater::SetCancelFlag(std::atomic<bool>* flag) {
    cancelFlag_ = flag;
}

// ========== Logging Function ==========

void WinProgramUpdater::Log(const std::string& message) {
    // DEBUG: Write to file when we see the sync message
    if (message.find("Synchronizing currently installed") != std::string::npos) {
        char* appdata = getenv("LOCALAPPDATA");
        std::string debugPath = appdata ? std::string(appdata) + "\\sync_trigger_debug.txt" : "sync_trigger_debug.txt";
        std::ofstream debugFile(debugPath, std::ios::app);
        debugFile << "Log() called with sync message: " << message << "\n";
        debugFile << "About to check if SyncInstalledApps will be called...\n";
        debugFile.close();
    }
    
    if (logCallback_) {
        // Convert Unix-style \n to Windows \r\n for multiline Edit control
        std::string windowsMessage = message;
        size_t pos = 0;
        while ((pos = windowsMessage.find("\n", pos)) != std::string::npos) {
            if (pos == 0 || windowsMessage[pos - 1] != '\r') {
                windowsMessage.insert(pos, "\r");
                pos += 2;
            } else {
                pos += 1;
            }
        }
        logCallback_(windowsMessage, logUserData_);
    }
    #ifdef _CONSOLE
    std::cout << message;
    #endif
}

void WinProgramUpdater::InitializeTagPatterns() {
    // Technology/Hardware
    tagPatterns_["USB"] = "usb";
    tagPatterns_["Bluetooth"] = "bluetooth";
    tagPatterns_["WiFi|Wi-Fi"] = "wifi";
    tagPatterns_["HDMI"] = "hdmi";
    tagPatterns_["GPU"] = "gpu";
    tagPatterns_["CPU"] = "cpu";
    
    // Application types
    tagPatterns_["Browser"] = "browser";
    tagPatterns_["Client"] = "client";
    tagPatterns_["Server"] = "server";
    tagPatterns_["Manager"] = "manager";
    tagPatterns_["Viewer"] = "viewer";
    tagPatterns_["Editor"] = "editor";
    tagPatterns_["Player"] = "player";
    tagPatterns_["Launcher"] = "launcher";
    tagPatterns_["Download"] = "download";
    
    // Functions
    tagPatterns_["Emulator"] = "emulator";
    tagPatterns_["Driver"] = "driver";
    tagPatterns_["Manual"] = "manual";
    tagPatterns_["Toolkit"] = "toolkit";
    tagPatterns_["SDK"] = "development";
    tagPatterns_["CLI|Command.?Line"] = "cli";
    tagPatterns_["Mock"] = "testing";
    tagPatterns_["Test"] = "testing";
    tagPatterns_["Debug"] = "development";
    tagPatterns_["Simulator"] = "emulator";
    
    // File formats/protocols
    tagPatterns_["INI"] = "configuration";
    tagPatterns_["JSON"] = "data";
    tagPatterns_["XML"] = "data";
    tagPatterns_["YAML"] = "configuration";
    tagPatterns_["CSV"] = "data";
    tagPatterns_["SQL"] = "database";
    tagPatterns_["HTML"] = "web";
    tagPatterns_["FTP"] = "network";
    tagPatterns_["HTTP"] = "web";
    tagPatterns_["ODBC"] = "database";
    tagPatterns_["API"] = "development";
    
    // Media
    tagPatterns_["Video"] = "video";
    tagPatterns_["Audio"] = "audio";
    tagPatterns_["Image"] = "graphics";
    tagPatterns_["Photo"] = "graphics";
    tagPatterns_["Music"] = "audio";
    tagPatterns_["PDF"] = "document";
    
    // Categories
    tagPatterns_["Game"] = "gaming";
    tagPatterns_["Utility"] = "utilities";
    tagPatterns_["Security"] = "security";
    tagPatterns_["Password"] = "security";
    tagPatterns_["Recovery"] = "utilities";
    tagPatterns_["Backup"] = "backup";
    tagPatterns_["Chocolatey"] = "package-manager";
    tagPatterns_["Winget"] = "winget";
}

bool WinProgramUpdater::OpenDatabase() {
    std::string dbPathUtf8 = WStringToString(dbPath_);
    int rc = sqlite3_open(dbPathUtf8.c_str(), &db_);
    if (rc != SQLITE_OK) {
        CloseDatabase();
        return false;
    }
    
    return true;
}

void WinProgramUpdater::CloseDatabase() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool WinProgramUpdater::OpenSearchDatabase() {
    std::string dbPathUtf8 = WStringToString(searchDbPath_);
    int rc = sqlite3_open(dbPathUtf8.c_str(), &searchDb_);
    if (rc != SQLITE_OK) {
        CloseSearchDatabase();
        return false;
    }
    
    // Create table if not exists
    const char* createTable = 
        "CREATE TABLE IF NOT EXISTS search_results ("
        "package_id TEXT PRIMARY KEY COLLATE NOCASE"
        ");";
    
    return ExecuteSQLSearch(createTable);
}

void WinProgramUpdater::CloseSearchDatabase() {
    if (searchDb_) {
        sqlite3_close(searchDb_);
        searchDb_ = nullptr;
    }
}

bool WinProgramUpdater::ExecuteSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }
    return true;
}

bool WinProgramUpdater::ExecuteSQLSearch(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(searchDb_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }
    return true;
}

std::vector<std::string> WinProgramUpdater::QueryPackageIds() {
    std::vector<std::string> ids;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM apps ORDER BY package_id;";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) {
                ids.push_back(reinterpret_cast<const char*>(text));
            }
        }
        sqlite3_finalize(stmt);
    }
    return ids;
}

bool WinProgramUpdater::HasTags(const std::string& packageId) {
    int dbId = GetPackageDbId(packageId);
    if (dbId <= 0) return false;
    
    std::string sql = "SELECT COUNT(*) FROM app_categories WHERE app_id = " + std::to_string(dbId) + ";";
    sqlite3_stmt* stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count > 0;
}

int WinProgramUpdater::GetPackageDbId(const std::string& packageId) {
    std::string sql = "SELECT id FROM apps WHERE package_id = ? COLLATE NOCASE;";
    sqlite3_stmt* stmt;
    int id = -1;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, packageId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return id;
}

int WinProgramUpdater::GetCategoryId(const std::string& category) {
    std::string sql = "SELECT id FROM categories WHERE category_name = ? COLLATE NOCASE;";
    sqlite3_stmt* stmt;
    int id = -1;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    // Create if doesn't exist
    if (id == -1) {
        std::string insertSql = "INSERT INTO categories (category_name) VALUES (?);";
        if (sqlite3_prepare_v2(db_, insertSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                id = static_cast<int>(sqlite3_last_insert_rowid(db_));
            }
            sqlite3_finalize(stmt);
        }
    }
    return id;
}

void WinProgramUpdater::AddPackage(const PackageInfo& pkg) {
    // Validate package has a name (allow all characters including international ones)
    if (pkg.name.empty()) {
        // Skip packages with missing names
        return;
    }
    
    std::string sql = "INSERT OR REPLACE INTO apps (package_id, name, version, publisher, moniker, "
                      "description, homepage, license, author, copyright, "
                      "license_url, privacy_url, icon_data, icon_type) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pkg.packageId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pkg.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pkg.version.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pkg.publisher.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pkg.moniker.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pkg.description.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, pkg.homepage.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, pkg.license.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, pkg.author.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, pkg.copyright.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, pkg.licenseUrl.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 12, pkg.privacyUrl.c_str(), -1, SQLITE_STATIC);
        
        if (!pkg.iconData.empty()) {
            sqlite3_bind_blob(stmt, 13, pkg.iconData.data(), pkg.iconData.size(), SQLITE_STATIC);
            sqlite3_bind_text(stmt, 14, pkg.iconType.c_str(), -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 13);
            sqlite3_bind_null(stmt, 14);
        }
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    // Add tags
    for (const auto& tag : pkg.tags) {
        AddTag(pkg.packageId, tag);
    }
}

void WinProgramUpdater::RemovePackage(const std::string& packageId) {
    int dbId = GetPackageDbId(packageId);
    if (dbId <= 0) return;
    
    // Remove tags first
    ExecuteSQL("DELETE FROM app_categories WHERE app_id = " + std::to_string(dbId) + ";");
    // Remove package
    ExecuteSQL("DELETE FROM apps WHERE id = " + std::to_string(dbId) + ";");
}

void WinProgramUpdater::AddTag(const std::string& packageId, const std::string& tag) {
    int dbId = GetPackageDbId(packageId);
    if (dbId <= 0) return;
    
    int categoryId = GetCategoryId(tag);
    if (categoryId <= 0) return;
    
    std::string sql = "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, dbId);
        sqlite3_bind_int(stmt, 2, categoryId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string WinProgramUpdater::ExecuteWingetCommand(const std::string& command) {
    // Use temp file to avoid pipe buffering issues with winget
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempFile = std::string(tempPath) + "winget_output_" + std::to_string(GetTickCount()) + ".txt";
    
    // Redirect winget output to temp file using cmd.exe
    std::string fullCmd = "cmd.exe /c \"winget " + command + " --accept-source-agreements --disable-interactivity > \"" + tempFile + "\" 2>&1\"";
    
    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    
    if (CreateProcessA(nullptr, const_cast<char*>(fullCmd.c_str()), nullptr, nullptr, 
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Wait up to 2 minutes for regional latency
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        if (waitResult == WAIT_TIMEOUT) {
            // Timeout - delete temp file and return empty
            DeleteFileA(tempFile.c_str());
            return "";
        }
        
        // Give file system a moment to flush
        Sleep(100);
    } else {
        return "";
    }
    
    // Read output from temp file
    std::string result;
    std::ifstream file(tempFile, std::ios::binary);
    if (file.is_open()) {
        // Read entire file into string
        result = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
    }
    
    // Clean up temp file
    DeleteFileA(tempFile.c_str());
    
    return result;
}

std::vector<std::string> WinProgramUpdater::GetWingetPackages() {
    std::vector<std::string> packages;
    std::string output = ExecuteWingetCommand("search \"\" --source winget");
    
#ifdef _CONSOLE
    std::wcout << L"Output length: " << output.length() << L" characters" << std::endl;
#endif
    
    if (output.empty()) {
        return packages;  // Return empty if command failed
    }
    
    std::istringstream stream(output);
    std::string line;
    bool inResults = false;
    int lineCount = 0;
    
    // Regex to split on 2+ spaces
    std::regex splitRegex("\\s{2,}");
    
    // Regex to validate package ID: alphanumeric with dots/dashes/plus signs, must contain non-numeric chars
    std::regex idRegex("^[A-Za-z0-9][\\w.+_-]*\\.[\\w.+_-]+$");
    
    while (std::getline(stream, line)) {
        lineCount++;
        
        // Skip progress indicators (single character lines or spinner)
        if (line.length() <= 2) continue;
        
        // Look for separator line (dashes)
        if (line.find("---") != std::string::npos && line.length() > 10) {
            inResults = true;
            continue;
        }
        
        if (!inResults) continue;
        if (line.empty() || line.length() < 10) continue;
        
        // Skip lines that are just whitespace
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        
        // Split line on 2+ spaces to get columns
        // Format: Name   PackageId   Version   Source
        std::vector<std::string> columns;
        std::sregex_token_iterator iter(line.begin(), line.end(), splitRegex, -1);
        std::sregex_token_iterator end;
        
        for (; iter != end; ++iter) {
            std::string col = Trim(iter->str());
            if (!col.empty()) {
                columns.push_back(col);
            }
        }
        
        // Second column should be PackageID
        if (columns.size() >= 2) {
            std::string packageId = columns[1];
            
            // Validate package ID format
            if (std::regex_match(packageId, idRegex)) {
                // Additional check: ensure it's not pure numeric (like version numbers)
                if (packageId.find_first_not_of("0123456789.-") != std::string::npos) {
                    packages.push_back(packageId);
#ifdef _CONSOLE
                    // Show first few IDs for verification
                    if (packages.size() <= 5) {
                        std::wcout << L"  ID " << packages.size() << L": " << StringToWString(packageId) << std::endl;
                    }
#endif
                }
            }
        }
    }
    
    return packages;
}

void WinProgramUpdater::PopulateSearchDatabase() {
    // Keep existing search database and update with new packages
    // (Do not delete - preserve for future use)
    
#ifdef _CONSOLE
    std::wcout << L"Querying winget search..." << std::endl;
#endif
    
    // Get all packages from winget
    auto packages = GetWingetPackages();
    
#ifdef _CONSOLE
    std::wcout << L"Found " << packages.size() << L" packages from winget" << std::endl;
    std::wcout << L"Populating search database..." << std::endl;
#endif
    
    // Insert all packages into search database
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR IGNORE INTO search_results (package_id) VALUES (?);";
    
    for (const auto& pkg : packages) {
        if (IsNumericOnly(pkg)) continue;
        
        if (sqlite3_prepare_v2(searchDb_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pkg.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
#ifdef _CONSOLE
    // Count what we actually inserted
    sqlite3_stmt* countStmt;
    int count = 0;
    if (sqlite3_prepare_v2(searchDb_, "SELECT COUNT(*) FROM search_results;", -1, &countStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            count = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }
    std::wcout << L"Search database populated with " << count << L" packages" << std::endl;
#endif
}

std::vector<std::string> WinProgramUpdater::GetNewPackages() {
    std::vector<std::string> newPackages;
    
    // Find packages in search DB but not in main DB
    const char* sql = 
        "SELECT package_id FROM search_results "
        "WHERE package_id NOT IN (SELECT package_id FROM apps);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(searchDb_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) {
                newPackages.push_back(reinterpret_cast<const char*>(text));
            }
        }
        sqlite3_finalize(stmt);
    }
    
    return newPackages;
}

std::vector<std::string> WinProgramUpdater::GetDeletedPackages() {
    std::vector<std::string> deletedPackages;
    
    // Step 3.1: Run comprehensive winget search to get ALL available packages
    std::string searchOutput = ExecuteWingetCommand("search .");
    
    // Step 3.2: Parse all package IDs from the comprehensive search
    std::unordered_set<std::string> availablePackageIds;
    std::istringstream stream(searchOutput);
    std::string line;
    bool inResultsSection = false;
    
    while (std::getline(stream, line)) {
        // Skip until we find the header line with "Name" and "Id"
        if (!inResultsSection) {
            if (line.find("Name") != std::string::npos && line.find("Id") != std::string::npos) {
                inResultsSection = true;
                std::getline(stream, line); // Skip the separator line (dashes)
            }
            continue;
        }
        
        // Skip empty lines and progress indicators
        if (line.empty() || line.length() <= 2) continue;
        if (line.find_first_not_of(" \t\r\n-\\/|") == std::string::npos) continue;
        
        // Parse line using right-to-left tokenization
        // Format: Name [spaces] Id [spaces] Version [spaces] Source
        std::vector<std::string> tokens;
        std::string token;
        bool inToken = false;
        
        // Right-to-left parsing
        for (int i = line.length() - 1; i >= 0; i--) {
            char c = line[i];
            if (c == ' ' || c == '\t') {
                if (inToken) {
                    std::reverse(token.begin(), token.end());
                    tokens.push_back(token);
                    token.clear();
                    inToken = false;
                    if (tokens.size() >= 3) break; // Got Source, Version, and Id
                }
            } else {
                token += c;
                inToken = true;
            }
        }
        
        // Package ID is the 3rd token from the right (Source, Version, Id)
        if (tokens.size() >= 3) {
            std::string packageId = tokens[2]; // 0=Source, 1=Version, 2=Id
            if (!packageId.empty()) {
                availablePackageIds.insert(packageId);
            }
        }
    }
    
    // Step 3.3: Find packages in DB that are NOT available AND NOT installed
    const char* sql = 
        "SELECT package_id FROM apps "
        "WHERE package_id NOT IN (SELECT package_id FROM installed_apps);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) {
                std::string packageId = reinterpret_cast<const char*>(text);
                // Only mark for deletion if NOT in comprehensive available list
                if (availablePackageIds.find(packageId) == availablePackageIds.end()) {
                    deletedPackages.push_back(packageId);
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    return deletedPackages;
}

PackageInfo WinProgramUpdater::GetPackageInfo(const std::string& packageId, int attempt) {
    PackageInfo info;
    info.packageId = packageId;
    
    std::string output = ExecuteWingetCommand("show \"" + packageId + "\"");
    
    if (output.empty() && attempt < MAX_RETRIES) {
        Sleep(1000 * attempt);  // Exponential backoff
        return GetPackageInfo(packageId, attempt + 1);
    }
    
    std::istringstream stream(output);
    std::string line;
    bool foundName = false;
    
    while (std::getline(stream, line)) {
        // Skip spinner lines (single char or whitespace)
        if (line.length() <= 2 || line.find_first_not_of(" \t\r\n-\\/|") == std::string::npos) {
            continue;
        }
        
        // Look for "Found <Name> [PackageId]" line (may not exist in batch mode)
        if (!foundName && line.find("Found ") == 0) {
            foundName = true;
            size_t start = 6; // After "Found "
            size_t end = line.find(" [");
            if (end != std::string::npos && end > start) {
                info.name = Trim(line.substr(start, end - start));
            }
            continue;
        }
        
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = Trim(line.substr(0, colonPos));
        std::string value = Trim(line.substr(colonPos + 1));
        
        if (key == "Version") {
            info.version = value;
        } else if (key == "Publisher") {
            info.publisher = value;
        } else if (key == "Moniker") {
            info.moniker = value;
        } else if (key == "Description") {
            info.description = value;
        } else if (key == "Short Description") {
            info.shortDescription = value;
        } else if (key == "Homepage") {
            info.homepage = value;
        } else if (key == "License") {
            info.license = value;
        } else if (key == "Author") {
            info.author = value;
        } else if (key == "Copyright") {
            info.copyright = value;
        } else if (key == "License Url") {
            info.licenseUrl = value;
        } else if (key == "Privacy Url") {
            info.privacyUrl = value;
        } else if (key == "Package Url") {
            info.packageUrl = value;
        } else if (key == "Tags") {
            std::istringstream tagStream(value);
            std::string tag;
            while (tagStream >> tag) {
                if (!tag.empty() && tag != ",") {
                    // Remove trailing comma
                    if (tag.back() == ',') tag.pop_back();
                    info.tags.push_back(tag);
                }
            }
        }
    }
    
    // If no "Found" line was present (batch mode), use packageId as name
    if (info.name.empty() && !info.version.empty()) {
        // Extract a readable name from packageId (e.g., "7zip.7zip" -> "7zip")
        info.name = packageId;
        size_t dotPos = packageId.find('.');
        if (dotPos != std::string::npos && dotPos < packageId.length() - 1) {
            // Use the part after the first dot if it exists
            info.name = packageId.substr(dotPos + 1);
        }
    }
    
    // Fetch icon from homepage if available
    if (!info.homepage.empty()) {
        FetchIconFromHomepage(info.homepage, info.iconData, info.iconType);
    }
    
    return info;
}

void WinProgramUpdater::FetchIconFromHomepage(const std::string& homepage, std::vector<unsigned char>& iconData, std::string& iconType) {
    if (homepage.empty()) return;
    
    // Use PowerShell to fetch the homepage HTML, find icon URL, and download it
    std::string psScript = 
        "$url = '" + homepage + "'; "
        "try { "
        "$response = Invoke-WebRequest -Uri $url -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop; "
        "$html = $response.Content; "
        
        // Look for various icon patterns
        "if ($html -match '<link[^>]*rel=[\"''](?:icon|shortcut icon)[\"''][^>]*href=[\"'']([^\"'']+)[\"'']') { "
        "  $icon = $Matches[1]; "
        "} elseif ($html -match '<link[^>]*href=[\"'']([^\"'']+)[\"''][^>]*rel=[\"''](?:icon|shortcut icon)[\"'']') { "
        "  $icon = $Matches[1]; "
        "} else { "
        "  $icon = '/favicon.ico'; "
        "} "
        
        // Convert relative URL to absolute
        "if ($icon -notmatch '^https?://') { "
        "  $uri = [System.Uri]$url; "
        "  if ($icon -match '^//') { "
        "    $icon = $uri.Scheme + ':' + $icon; "
        "  } elseif ($icon -match '^/') { "
        "    $icon = $uri.Scheme + '://' + $uri.Host + $icon; "
        "  } else { "
        "    $icon = $uri.Scheme + '://' + $uri.Host + '/' + $icon; "
        "  } "
        "} "
        
        // Download the icon
        "try { "
        "  $iconResponse = Invoke-WebRequest -Uri $icon -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop; "
        "  $tempFile = [System.IO.Path]::GetTempFileName(); "
        "  [System.IO.File]::WriteAllBytes($tempFile, $iconResponse.Content); "
        "  Write-Output $tempFile; "
        "  Write-Output $icon; "
        "} catch { Write-Output ''; } "
        "} catch { Write-Output ''; }";
    
    std::string command = "powershell -NoProfile -Command \"" + psScript + "\"";
    
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return;
    }
    
    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(nullptr, const_cast<char*>(command.c_str()), 
                        nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return;
    }
    
    CloseHandle(hWrite);
    
    std::string output;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    
    WaitForSingleObject(pi.hProcess, 15000); // 15 second timeout
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    
    // Parse output: first line is temp file path, second line is icon URL
    std::istringstream stream(output);
    std::string tempFile, iconUrl;
    std::getline(stream, tempFile);
    std::getline(stream, iconUrl);
    
    tempFile = Trim(tempFile);
    iconUrl = Trim(iconUrl);
    
    if (!tempFile.empty() && !iconUrl.empty()) {
        // Read the icon file
        std::ifstream file(tempFile, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            iconData.resize(size);
            if (file.read(reinterpret_cast<char*>(iconData.data()), size)) {
                // Determine icon type from URL
                if (iconUrl.find(".png") != std::string::npos) {
                    iconType = "png";
                } else if (iconUrl.find(".jpg") != std::string::npos || iconUrl.find(".jpeg") != std::string::npos) {
                    iconType = "jpg";
                } else {
                    iconType = "ico";
                }
            }
            file.close();
        }
        
        // Clean up temp file
        DeleteFileA(tempFile.c_str());
    }
}

std::vector<std::string> WinProgramUpdater::ExtractTagsFromText(const std::string& name,
                                                                  const std::string& packageId,
                                                                  const std::string& moniker) {
    std::vector<std::string> tags;
    
    for (const auto& pattern : tagPatterns_) {
        std::regex re(pattern.first, std::regex_constants::icase);
        
        if (std::regex_search(name, re) || 
            std::regex_search(packageId, re) ||
            (!moniker.empty() && std::regex_search(moniker, re))) {
            
            // Avoid duplicates
            if (std::find(tags.begin(), tags.end(), pattern.second) == tags.end()) {
                tags.push_back(pattern.second);
            }
        }
    }
    
    return tags;
}

bool WinProgramUpdater::IsNumericOnly(const std::string& packageId) {
    return std::regex_match(packageId, std::regex("^[0-9.]+$"));
}

void WinProgramUpdater::ApplyNameBasedInference(UpdateStats& stats) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, package_id, name, moniker FROM apps "
                      "WHERE id NOT IN (SELECT DISTINCT app_id FROM app_categories);";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string packageId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string moniker = sqlite3_column_text(stmt, 3) ? 
                                  reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) : "";
            
            auto tags = ExtractTagsFromText(name, packageId, moniker);
            for (const auto& tag : tags) {
                AddTag(packageId, tag);
                stats.tagsFromInference++;
            }
        }
        sqlite3_finalize(stmt);
    }
}

void WinProgramUpdater::ApplyCorrelationAnalysis(UpdateStats& stats) {
    // Build co-occurrence matrix
    std::map<std::string, std::map<std::string, int>> coOccurrence;
    std::map<std::string, int> tagCounts;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT app_id FROM app_categories GROUP BY app_id HAVING COUNT(*) > 1;";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int appId = sqlite3_column_int(stmt, 0);
            
            // Get all tags for this app
            std::vector<std::string> appTags;
            sqlite3_stmt* tagStmt;
            std::string tagSql = "SELECT c.category_name FROM categories c "
                                "JOIN app_categories ac ON c.id = ac.category_id "
                                "WHERE ac.app_id = " + std::to_string(appId) + ";";
            
            if (sqlite3_prepare_v2(db_, tagSql.c_str(), -1, &tagStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(tagStmt) == SQLITE_ROW) {
                    std::string tag = reinterpret_cast<const char*>(sqlite3_column_text(tagStmt, 0));
                    appTags.push_back(tag);
                    tagCounts[tag]++;
                }
                sqlite3_finalize(tagStmt);
            }
            
            // Build co-occurrence for each pair
            for (size_t i = 0; i < appTags.size(); i++) {
                for (size_t j = 0; j < appTags.size(); j++) {
                    if (i != j) {
                        coOccurrence[appTags[i]][appTags[j]]++;
                    }
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Apply correlation rules (66.67% threshold, min 6 samples)
    const double CORRELATION_THRESHOLD = 0.6667;
    const int MIN_SAMPLES = 6;
    
    for (const auto& source : coOccurrence) {
        if (tagCounts[source.first] < MIN_SAMPLES) continue;
        
        for (const auto& target : source.second) {
            double correlation = static_cast<double>(target.second) / tagCounts[source.first];
            
            if (correlation >= CORRELATION_THRESHOLD) {
                // Apply inference rule: packages with source tag should have target tag
                std::string applySql = 
                    "INSERT OR IGNORE INTO app_categories (app_id, category_id) "
                    "SELECT ac.app_id, (SELECT id FROM categories WHERE category_name = ?) "
                    "FROM app_categories ac "
                    "JOIN categories c ON ac.category_id = c.id "
                    "WHERE c.category_name = ? "
                    "AND ac.app_id NOT IN ("
                    "  SELECT app_id FROM app_categories "
                    "  WHERE category_id = (SELECT id FROM categories WHERE category_name = ?)"
                    ");";
                
                sqlite3_stmt* applyStmt;
                if (sqlite3_prepare_v2(db_, applySql.c_str(), -1, &applyStmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(applyStmt, 1, target.first.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(applyStmt, 2, source.first.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_text(applyStmt, 3, target.first.c_str(), -1, SQLITE_STATIC);
                    
                    if (sqlite3_step(applyStmt) == SQLITE_DONE) {
                        stats.tagsFromCorrelation += sqlite3_changes(db_);
                    }
                    sqlite3_finalize(applyStmt);
                }
            }
        }
    }
}

void WinProgramUpdater::TagUncategorized(UpdateStats& stats) {
    int categoryId = GetCategoryId("uncategorized");
    
    std::string sql = "INSERT INTO app_categories (app_id, category_id) "
                      "SELECT id, " + std::to_string(categoryId) + " "
                      "FROM apps WHERE id NOT IN (SELECT DISTINCT app_id FROM app_categories);";
    
    if (ExecuteSQL(sql)) {
        stats.uncategorized = sqlite3_changes(db_);
    }
}

bool WinProgramUpdater::UpdateDatabase(UpdateStats& stats) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // BEGIN: UpdateDatabase - Main 8-step database update procedure
    Log("=== WinProgram Database Updater ===\n");
    
    // Clear search cache to ensure fresh data
    Log("Clearing search cache...\n");
#ifdef _CONSOLE
    std::wcout << L"Clearing search cache..." << std::endl;
#endif
    DeleteFileW(searchDbPath_.c_str());
    Log("Cache cleared!\n\n");
    
    if (!OpenDatabase()) {
        return false;
    }
    
    if (!OpenSearchDatabase()) {
        CloseDatabase();
        return false;
    }
    
    // BEGIN: Step 1 - Query winget for available packages
    // Step 1: Populate search database with winget search results
    Log("=== Step 1: Query winget ===\n");
    Log("Executing 'winget search .' to enumerate all available packages...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 1: Query winget ===" << std::endl;
    auto stepStart = std::chrono::high_resolution_clock::now();
#endif
    PopulateSearchDatabase();
    Log("Step 1 complete! All available packages retrieved from winget.\n\n");
    
#ifdef _CONSOLE
    auto stepEnd = std::chrono::high_resolution_clock::now();
    auto stepDuration = std::chrono::duration_cast<std::chrono::seconds>(stepEnd - stepStart).count();
    std::wcout << L"   Time: " << stepDuration << L" seconds" << std::endl;
#endif
    // END: Step 1
    
    // Attach search database to main database for SQL comparisons
    Log("Attaching search database for comparison...\n");
#ifdef _CONSOLE
    std::wcout << L"Attaching search database..." << std::endl;
#endif
    // Convert searchDbPath_ (wstring) to UTF-8 string for SQL
    std::string searchDbPathUtf8 = WStringToString(searchDbPath_);
    // Escape single quotes in path for SQL
    std::string escapedPath;
    for (char c : searchDbPathUtf8) {
        if (c == '\'') escapedPath += "''";
        else escapedPath += c;
    }
    std::string attachSql = "ATTACH DATABASE '" + escapedPath + "' AS search_db;";
    if (!ExecuteSQL(attachSql)) {
        Log("ERROR: Failed to attach search database!\n");
        Log("Path attempted: " + searchDbPathUtf8 + "\n");
#ifdef _CONSOLE
        std::wcout << L"Failed to attach search database" << std::endl;
        std::wcout << L"SQL: " << StringToWString(attachSql) << std::endl;
#endif
        CloseSearchDatabase();
        CloseDatabase();
        return false;
    }
    Log("Database attached successfully!\n");
    
    // BEGIN: Step 2 - Find and add new packages to database
    // Step 2: Find new packages (in search but not in main)
    Log("\n=== Step 2: Find new packages ===\n");
    Log("Comparing winget results with database to find new packages...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 2: Find new packages ===" << std::endl;
#endif
    
    auto newPackages = GetNewPackages();
    
    std::string countMsg = "Found " + std::to_string(newPackages.size()) + " new packages to add.\n";
    Log(countMsg);
#ifdef _CONSOLE
    std::wcout << L"Found " << newPackages.size() << L" new packages" << std::endl;
#endif
    
    // Add new packages
    int processedCount = 0;
    for (const auto& packageId : newPackages) {
        processedCount++;
        std::string progressMsg = "[" + std::to_string(processedCount) + "/" + std::to_string(newPackages.size()) + "] ";
        Log(progressMsg + "Processing: " + packageId + "...\n");
#ifdef _CONSOLE
        std::wcout << L"  Processing: " << StringToWString(packageId) << L"..." << std::flush;
#endif
        PackageInfo info = GetPackageInfo(packageId);
        if (!info.name.empty()) {
            AddPackage(info);
            stats.packagesAdded++;
            stats.tagsFromWinget += info.tags.size();
            std::string successMsg = "   ✓ Added: " + info.name + " (" + std::to_string(info.tags.size()) + " tags)\n";
            Log(successMsg);
#ifdef _CONSOLE
            std::wcout << L" ✓ Added (" << info.tags.size() << L" tags)" << std::endl;
#endif
        } else {
            Log("   ✗ Skipped (no package info available)\n");
#ifdef _CONSOLE
            std::wcout << L" ✗ Skipped (no info)" << std::endl;
#endif
        }
    }
    
    // Step 2 (continued): Cross-reference with installed packages
    Log("Cross-referencing with installed packages...\n");
    Log("Synchronizing currently installed applications...\n");
    
    // First, sync installed apps to get current system state
    if (SyncInstalledApps()) {
        Log("Installed apps synchronized.\n");
    } else {
        Log("Warning: Failed to sync installed apps.\n");
    }
    
    // Now find packages that are: installed + in winget + not yet in database
    // This catches packages with special characters or any other edge cases
    Log("Checking for installed packages not yet in database...\n");
    
    const char* sqlFindMissing = 
        "SELECT DISTINCT sr.package_id "
        "FROM search_db.search_results sr "
        "INNER JOIN installed_apps ia ON sr.package_id = ia.package_id "
        "WHERE sr.package_id NOT IN (SELECT package_id FROM apps);";
    
    sqlite3_stmt* stmtMissing;
    std::vector<std::string> missingInstalledPackages;
    
    if (sqlite3_prepare_v2(db_, sqlFindMissing, -1, &stmtMissing, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmtMissing) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmtMissing, 0);
            if (text) {
                missingInstalledPackages.push_back(reinterpret_cast<const char*>(text));
            }
        }
        sqlite3_finalize(stmtMissing);
    }
    
    if (missingInstalledPackages.size() > 0) {
        std::string foundMsg = "Found " + std::to_string(missingInstalledPackages.size()) + 
                               " package(s) not yet in database.\n";
        Log(foundMsg);
        
        int addedFromInstalled = 0;
        for (const auto& packageId : missingInstalledPackages) {
            Log("  Adding package: " + packageId + "...\n");
            PackageInfo info = GetPackageInfo(packageId);
            if (!info.name.empty()) {
                AddPackage(info);
                stats.packagesAdded++;
                stats.tagsFromWinget += info.tags.size();
                addedFromInstalled++;
                Log("   ✓ Added\n");
            } else {
                Log("   ✗ Could not retrieve package info\n");
            }
            
            // Add delay between winget calls to prevent overwhelming the system
            Sleep(300);
        }
        
        std::string resultMsg = "Added " + std::to_string(addedFromInstalled) + 
                                " package(s) to database.\n";
        Log(resultMsg);
    } else {
        Log("All packages are already in database.\n");
    }
    
    std::string step2Summary = "\nStep 2 Summary: Added " + std::to_string(stats.packagesAdded) + " new packages with " + 
                                std::to_string(stats.tagsFromWinget) + " tags from winget.\n";
    Log(step2Summary);
    // END: Step 2
    
    // BEGIN: Step 3 - Find and remove packages deleted from winget
    // Step 3: Find and remove deleted packages (comprehensive check)
    // Run "winget search ." to get ALL available packages, then safely delete
    // packages that are NOT available AND NOT installed
    Log("\n=== Step 3: Find deleted packages ===\n");
    Log("Running comprehensive winget search to identify obsolete packages...\n");
    Log("This step ensures packages no longer in winget (and not installed) are removed.\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 3: Find deleted packages (comprehensive) ===" << std::endl;
#endif
    
    // Get executable directory for script path
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = std::string(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    // Convert database path to UTF-8
    std::wstring wDbPath = dbPath_;
    std::string dbPathUtf8;
    int size = WideCharToMultiByte(CP_UTF8, 0, wDbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        dbPathUtf8.resize(size - 1);
        WideCharToMultiByte(CP_UTF8, 0, wDbPath.c_str(), -1, &dbPathUtf8[0], size, nullptr, nullptr);
    }
    
    // Use PowerShell script for comprehensive check via "winget search ."
    // This ensures we only delete packages that are truly gone from winget
    std::string checkScript = exeDir + "\\scripts\\check_deleted_packages.ps1";
    std::string checkCommand = "powershell.exe -ExecutionPolicy Bypass -NoProfile -File \"" + 
                               checkScript + "\" -DatabasePath \"" + dbPathUtf8 + "\"";
    
    STARTUPINFOA checkSi = {};
    PROCESS_INFORMATION checkPi = {};
    checkSi.cb = sizeof(checkSi);
    checkSi.dwFlags = STARTF_USESHOWWINDOW;
    checkSi.wShowWindow = SW_HIDE;
    
    if (CreateProcessA(nullptr, const_cast<char*>(checkCommand.c_str()), nullptr, nullptr, 
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &checkSi, &checkPi)) {
        WaitForSingleObject(checkPi.hProcess, 600000); // 10 minute timeout for comprehensive search
        
        DWORD checkExitCode = 0;
        GetExitCodeProcess(checkPi.hProcess, &checkExitCode);
        
        CloseHandle(checkPi.hProcess);
        CloseHandle(checkPi.hThread);
        
#ifdef _CONSOLE
        if (checkExitCode == 0) {
            std::wcout << L"Cleanup completed successfully" << std::endl;
        } else {
            std::wcout << L"Cleanup script exited with code: " << checkExitCode << std::endl;
        }
#endif
        if (checkExitCode == 0) {
            Log("Step 3 complete! Obsolete packages have been removed.\n\n");
        } else {
            std::string errMsg = "Step 3 warning: Cleanup script exited with code " + std::to_string(checkExitCode) + "\n\n";
            Log(errMsg);
        }
    } else {
        Log("ERROR: Failed to execute cleanup script!\n\n");
#ifdef _CONSOLE
        std::wcout << L"Failed to execute cleanup script" << std::endl;
#endif
    }
    // END: Step 3
    
    // BEGIN: Step 4 - Update tags for packages with zero tags
    // Step 4: Update tags for packages with zero tags (only if not yet checked)
    Log("=== Step 4: Update tags for zero-tag packages ===\n");
    Log("Finding packages without tags and querying winget for their metadata...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 4: Update tags for zero-tag packages ===" << std::endl;
#endif
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM apps WHERE tags_updated = 0 AND id NOT IN (SELECT DISTINCT app_id FROM app_categories);";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::vector<std::string> zeroTagPackages;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            zeroTagPackages.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        
        std::string foundMsg = "Found " + std::to_string(zeroTagPackages.size()) + " packages with zero tags (not yet checked).\n\n";
        Log(foundMsg);
#ifdef _CONSOLE
        std::wcout << L"Found " << zeroTagPackages.size() << L" packages with zero tags (not yet checked)" << std::endl;
#endif
        
        int step4Count = 0;
        for (const auto& packageId : zeroTagPackages) {
            step4Count++;
            std::string progressMsg = "[" + std::to_string(step4Count) + "/" + std::to_string(zeroTagPackages.size()) + "] ";
            Log(progressMsg + "Updating: " + packageId + "...\n");
#ifdef _CONSOLE
            std::wcout << L"  Updating tags for: " << StringToWString(packageId) << L"..." << std::flush;
#endif
            PackageInfo info = GetPackageInfo(packageId);
            int addedTags = 0;
            for (const auto& tag : info.tags) {
                AddTag(packageId, tag);
                stats.tagsFromWinget++;
                addedTags++;
            }
            
            // Mark this package as checked (whether tags were found or not)
            std::string updateSql = "UPDATE apps SET tags_updated = 1 WHERE package_id = ?;";
            sqlite3_stmt* updateStmt;
            if (sqlite3_prepare_v2(db_, updateSql.c_str(), -1, &updateStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(updateStmt, 1, packageId.c_str(), -1, SQLITE_STATIC);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);
            }
            
            if (addedTags > 0) {
                std::string successMsg = "   ✓ Added " + std::to_string(addedTags) + " tags\n";
                Log(successMsg);
            } else {
                Log("   (no tags found in winget metadata)\n");
            }
#ifdef _CONSOLE
            if (addedTags > 0) {
                std::wcout << L" ✓ Added " << addedTags << L" tags" << std::endl;
            } else {
                std::wcout << L" (no tags found)" << std::endl;
            }
#endif
        }
        Log("\nStep 4 complete! All zero-tag packages have been checked.\n\n");
    }
    // END: Step 4
    
    // BEGIN: Step 5 - Apply name-based inference for categorization
    // Step 5: Apply inference
    Log("=== Step 5: Apply name-based inference ===\n");
    Log("Using package names to infer categories...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 5: Apply name-based inference ===" << std::endl;
#endif
    ApplyNameBasedInference(stats);
    Log("Step 5 complete! Name-based inference applied.\n\n");
    // END: Step 5
    
    // BEGIN: Step 6 - Apply correlation analysis for categorization
    // Step 6: Apply correlation
    Log("=== Step 6: Apply correlation analysis ===\n");
    Log("Analyzing relationships between packages to infer categories...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 6: Apply correlation analysis ===" << std::endl;
#endif
    ApplyCorrelationAnalysis(stats);
    Log("Step 6 complete! Correlation analysis applied.\n\n");
    // END: Step 6
    
    // BEGIN: Step 7 - Tag remaining uncategorized packages
    // Step 7: Tag uncategorized
    Log("=== Step 7: Tag uncategorized ===\n");
    Log("Assigning default category to remaining uncategorized packages...\n");
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 7: Tag uncategorized ===" << std::endl;
#endif
    TagUncategorized(stats);
    Log("Step 7 complete! All packages now have categories.\n\n");
    // END: Step 7
    
    stats.tagsAdded = stats.tagsFromWinget + stats.tagsFromInference + stats.tagsFromCorrelation;
    
    // Detach search database
    ExecuteSQL("DETACH DATABASE search_db;");
    
    CloseSearchDatabase();
    CloseDatabase();
    
    // Calculate elapsed time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    stats.elapsedSeconds = static_cast<double>(duration);
    
    // Format duration as HH:MM:SS for APPDATA log
    int hours = duration / 3600;
    int minutes = (duration % 3600) / 60;
    int seconds = duration % 60;
    std::ostringstream durationStr;
    durationStr << std::setfill('0') << std::setw(2) << hours << ":"
                << std::setfill('0') << std::setw(2) << minutes << ":"
                << std::setfill('0') << std::setw(2) << seconds;
    
    // Create completion summary for GUI
    std::string completionMsg = "\n=== Update Complete ===\n";
    completionMsg += "Time elapsed: " + durationStr.str() + "\n";
    completionMsg += "Packages added: " + std::to_string(stats.packagesAdded) + "\n";
    completionMsg += "Total tags added: " + std::to_string(stats.tagsAdded) + "\n";
    completionMsg += "  - From winget: " + std::to_string(stats.tagsFromWinget) + "\n";
    completionMsg += "  - From inference: " + std::to_string(stats.tagsFromInference) + "\n";
    completionMsg += "  - From correlation: " + std::to_string(stats.tagsFromCorrelation) + "\n";
    completionMsg += "\nDatabase update successful!\n";
    Log(completionMsg);
    
#ifdef _CONSOLE
    std::wcout << L"\n=== Update Complete ===" << std::endl;
    std::wcout << L"Time the update took: " << StringToWString(durationStr.str()) << std::endl;
#endif
    // END: UpdateDatabase
    
    // Write permanent log
    WriteAppDataLog(stats, durationStr.str());
    
    return true;
}

std::string WinProgramUpdater::GetAppDataLogPath() {
    // Get %APPDATA% directory
    wchar_t* appDataPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath);
    
    if (FAILED(hr) || !appDataPath) {
        return "";
    }
    
    std::wstring wAppDataPath(appDataPath);
    CoTaskMemFree(appDataPath);
    
    // Create WinProgramManager\log directory
    std::wstring baseDir = wAppDataPath + L"\\WinProgramManager";
    std::wstring logDir = baseDir + L"\\log";
    
    // Create directories
    CreateDirectoryW(baseDir.c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);
    
    // Convert to UTF-8 and return full log file path
    std::string result = WStringToString(logDir) + "\\WinProgramUpdater.log";
    return result;
}

void WinProgramUpdater::WriteAppDataLog(const UpdateStats& stats, const std::string& duration) {
    std::string logPath = GetAppDataLogPath();
    if (logPath.empty()) {
        return;
    }
    
    // Read existing content
    std::string existingContent;
    std::ifstream inFile(logPath);
    if (inFile) {
        std::ostringstream ss;
        ss << inFile.rdbuf();
        existingContent = ss.str();
        inFile.close();
    }
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &nowTime);
    
    // Create new entry in exact format user requested
    std::ostringstream newEntry;
    newEntry << "[" 
             << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] \n"
             << "Update completed: \n"
             << "+" << stats.packagesAdded << " added, \n"
             << "-" << stats.packagesRemoved << " removed, \n"
             << "~" << stats.packagesUpdated << " updated, \n"
             << stats.tagsFromInference + stats.tagsFromCorrelation << " tags inferred\n"
             << "Time update took: " << duration << "\n\n";
    
    // Write new entry at top (prepend)
    std::ofstream outFile(logPath, std::ios::trunc);
    if (outFile) {
        outFile << newEntry.str() << existingContent;
        outFile.close();
    }
    
    // Prune old entries (3 months)
    PruneAppDataLog();
}

void WinProgramUpdater::PruneAppDataLog() {
    std::string logPath = GetAppDataLogPath();
    if (logPath.empty()) {
        return;
    }
    
    std::ifstream inFile(logPath);
    if (!inFile) return;
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * 90);  // 3 months = ~90 days
    
    std::vector<std::string> validEntries;
    std::string line;
    std::string currentEntry;
    bool inEntry = false;
    
    while (std::getline(inFile, line)) {
        // Check if this is a timestamp line (starts with [YYYY-MM-DD)
        if (line.length() > 0 && line[0] == '[' && line.length() > 20) {
            // Save previous entry if valid
            if (!currentEntry.empty() && inEntry) {
                validEntries.push_back(currentEntry);
            }
            
            // Parse timestamp [2026-01-19 14:30:45]
            std::string timestamp = line.substr(1, 19);
            std::tm tm = {};
            std::istringstream ss(timestamp);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            
            auto entryTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            
            if (entryTime >= cutoff) {
                currentEntry = line + "\n";
                inEntry = true;
            } else {
                currentEntry.clear();
                inEntry = false;
            }
        } else if (inEntry) {
            currentEntry += line + "\n";
        }
    }
    
    // Save last entry
    if (!currentEntry.empty() && inEntry) {
        validEntries.push_back(currentEntry);
    }
    
    inFile.close();
    
    // Write back only valid entries
    std::ofstream outFile(logPath, std::ios::trunc);
    for (const auto& entry : validEntries) {
        outFile << entry;
    }
}

std::string WinProgramUpdater::Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

std::wstring WinProgramUpdater::StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string WinProgramUpdater::WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return "";  // Only null terminator
    std::string result(size - 1, 0);  // Exclude null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

bool WinProgramUpdater::SyncInstalledApps() {
    // DEBUG: Log function entry
    char* appdata = getenv("LOCALAPPDATA");
    std::string debugPath = appdata ? std::string(appdata) + "\\sync_debug.txt" : "sync_debug.txt";
    std::ofstream debugLog(debugPath, std::ios::app);
    debugLog << "=== SyncInstalledApps CALLED at " << time(nullptr) << " ===\n";
    debugLog.close();
    
    if (!db_) {
        std::ofstream debugLog2(debugPath, std::ios::app);
        debugLog2 << "ERROR: db_ is null!\n";
        debugLog2.close();
        return false;
    }
    
    // Create installed_apps table if it doesn't exist
    const char* createTableSql = 
        "CREATE TABLE IF NOT EXISTS installed_apps ("
        "    package_id TEXT PRIMARY KEY,"
        "    installed_date TEXT,"
        "    last_seen TEXT,"
        "    installed_version TEXT,"
        "    source TEXT,"
        "    FOREIGN KEY (package_id) REFERENCES apps(package_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_installed_last_seen ON installed_apps(last_seen);";
    
    if (!ExecuteSQL(createTableSql)) {
        std::ofstream debugLog3(debugPath, std::ios::app);
        debugLog3 << "ERROR: Failed to create table!\n";
        debugLog3.close();
        return false;
    }
    
    std::ofstream debugLog3b(debugPath, std::ios::app);
    debugLog3b << "Table created successfully\n";
    debugLog3b.close();
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    
    // Execute winget list to get installed packages
    std::ofstream debugLog4(debugPath, std::ios::app);
    debugLog4 << "About to call ExecuteWingetCommand...\n";
    debugLog4.close();
    
    std::string output = ExecuteWingetCommand("list");
    
    std::ofstream debugLog5(debugPath, std::ios::app);
    debugLog5 << "ExecuteWingetCommand returned, output length: " << output.length() << " bytes\n";
    if (output.length() > 0) {
        debugLog5 << "First 200 chars: " << output.substr(0, std::min(size_t(200), output.length())) << "\n";
    }
    debugLog5.close();
    
    if (output.empty()) {
        std::ofstream debugLog6(debugPath, std::ios::app);
        debugLog6 << "ERROR: output is empty, returning false!\n";
        debugLog6.close();
        return false;
    }
    
    // Replace carriage returns with newlines (winget uses \r for spinner animation)
    std::replace(output.begin(), output.end(), '\r', '\n');
    
    // Parse winget list output
    // Format: Name  Id  Version  Available  Source
    // Parse from right to left: rightmost is Source (or Version if no Source), then Version, then Id
    std::istringstream stream(output);
    std::string line;
    bool pastHeader = false;
    
    std::vector<std::tuple<std::string, std::string, std::string>> installedPackages; // (id, version, source)
    
    while (std::getline(stream, line)) {
        // Skip empty or very short lines
        if (line.length() < 10) continue;
        
        // Skip spinner animation lines (only contain -, \, |, /, and whitespace)
        bool isSpinnerLine = true;
        for (char c : line) {
            if (c != ' ' && c != '-' && c != '\\' && c != '|' && c != '/' && c != '\t' && c != '\r' && c != '\n') {
                isSpinnerLine = false;
                break;
            }
        }
        if (isSpinnerLine) continue;
        
        // Skip header line
        if (line.find("Name") != std::string::npos && line.find("Id") != std::string::npos) {
            pastHeader = true;
            continue;
        }
        
        // Skip separator line (dashes)
        if (line.find("---") != std::string::npos) {
            pastHeader = true;
            continue;
        }
        
        if (!pastHeader) continue;
        
        // DEBUG: Log first few lines being processed
        static int lineCount = 0;
        if (lineCount < 5) {
            std::ofstream debugLogLine(debugPath, std::ios::app);
            debugLogLine << "Processing line " << lineCount << ": [" << line << "]\n";
            debugLogLine.close();
            lineCount++;
        }
        
        // Split line into tokens by whitespace
        std::vector<std::string> tokens;
        std::istringstream tokenStream(line);
        std::string token;
        while (tokenStream >> token) {
            tokens.push_back(token);
        }
        
        // Need at least 2 tokens (Name and Id)
        if (tokens.size() < 2) continue;
        
        // Parse from right to left
        // If rightmost token contains at least one digit, skip this line (malformed)
        std::string rightmost = tokens.back();
        bool hasDigit = false;
        for (char c : rightmost) {
            if (std::isdigit(c)) {
                hasDigit = true;
                break;
            }
        }
        if (hasDigit) continue;
        
        // Extract fields from right to left
        // Note: When updates are available, winget list shows: Name | Id | Version | Available | Source
        // We need to identify package ID by pattern, not by fixed position
        std::string source, version, id;
        
        // Define regex pattern for package ID (vendor.product format)
        // Must have at least one letter before and after the dot to distinguish from version numbers
        // Allows: letters, numbers, dots, underscores, hyphens, and plus signs
        std::regex idRegex("^[A-Za-z0-9+_-]*[A-Za-z][A-Za-z0-9+._-]*\\.[A-Za-z0-9+_-]*[A-Za-z][A-Za-z0-9+._-]*$");
        
        if (tokens.size() >= 3) {
            source = tokens[tokens.size() - 1];  // Rightmost token is always source
            
            // Find package ID by scanning tokens right-to-left (skip source token)
            int idIndex = -1;
            for (int i = static_cast<int>(tokens.size()) - 2; i >= 0; --i) {
                if (std::regex_match(tokens[i], idRegex)) {
                    idIndex = i;
                    break;
                }
            }
            
            if (idIndex >= 0) {
                // Found package ID - version is immediately to the right of it
                id = tokens[idIndex];
                version = tokens[idIndex + 1];  // Could be followed by Available, but this is installed version
            } else {
                // Fallback: use old positional logic if no regex match
                version = tokens[tokens.size() - 2];
                id = tokens[tokens.size() - 3];
            }
        } else if (tokens.size() == 2) {
            // No source column
            version = tokens[tokens.size() - 1];
            id = tokens[tokens.size() - 2];
        }
        
        // Validate ID (should contain at least one dot or backslash for package IDs)
        if (id.empty() || (id.find('.') == std::string::npos && id.find('\\') == std::string::npos)) continue;
        
        // DEBUG: Log parsed package
        std::ofstream debugLogParse(debugPath, std::ios::app);
        debugLogParse << "Parsed: ID=" << id << ", Version=" << version << ", Source=" << source << "\n";
        debugLogParse.close();
        
        installedPackages.push_back(std::make_tuple(id, version, source));
    }
    
    // Update database with installed packages
#ifdef _CONSOLE
    std::wcout << L"   Found " << installedPackages.size() << L" installed packages" << std::endl;
#endif
    
    for (const auto& pkg : installedPackages) {
        std::string id = std::get<0>(pkg);
        std::string version = std::get<1>(pkg);
        std::string source = std::get<2>(pkg);
        
        // Check if already exists
        std::string checkSql = "SELECT installed_date FROM installed_apps WHERE package_id = ?;";
        sqlite3_stmt* stmt;
        std::string installedDate = timestamp;
        
        if (sqlite3_prepare_v2(db_, checkSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Package exists, keep original install date
                const unsigned char* date = sqlite3_column_text(stmt, 0);
                if (date) {
                    installedDate = reinterpret_cast<const char*>(date);
                }
            }
            sqlite3_finalize(stmt);
        }
        
        // Insert or update
        std::string upsertSql = 
            "INSERT OR REPLACE INTO installed_apps (package_id, installed_date, last_seen, installed_version, source) "
            "VALUES (?, ?, ?, ?, ?);";
        
        if (sqlite3_prepare_v2(db_, upsertSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, installedDate.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, timestamp, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, version.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, source.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    
    // Remove packages that are no longer installed (not seen in this sync)
    std::string deleteSql = "DELETE FROM installed_apps WHERE last_seen != ?;";
    sqlite3_stmt* deleteStmt;
    if (sqlite3_prepare_v2(db_, deleteSql.c_str(), -1, &deleteStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(deleteStmt, 1, timestamp, -1, SQLITE_STATIC);
        sqlite3_step(deleteStmt);
        sqlite3_finalize(deleteStmt);
    }
    
    return true;
}
