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

WinProgramUpdater::WinProgramUpdater(const std::wstring& dbPath)
    : db_(nullptr), searchDb_(nullptr), dbPath_(dbPath) {
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
    std::string sql = "INSERT OR REPLACE INTO apps (package_id, name, version, publisher, moniker, "
                      "description, short_description, homepage, license, author, copyright, "
                      "license_url, privacy_url, package_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pkg.packageId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, pkg.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pkg.version.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pkg.publisher.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, pkg.moniker.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, pkg.description.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, pkg.shortDescription.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, pkg.homepage.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, pkg.license.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, pkg.author.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, pkg.copyright.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 12, pkg.licenseUrl.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 13, pkg.privacyUrl.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 14, pkg.packageUrl.c_str(), -1, SQLITE_STATIC);
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
    std::string fullCmd = "winget " + command + " --accept-source-agreements";
    std::string result;
    
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }
    
    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    
    if (CreateProcessA(nullptr, const_cast<char*>(fullCmd.c_str()), nullptr, nullptr, 
                       TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hWritePipe);
        
        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result += buffer;
        }
        
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hWritePipe);
    }
    
    CloseHandle(hReadPipe);
    return result;
}

std::vector<std::string> WinProgramUpdater::GetWingetPackages() {
    std::vector<std::string> packages;
    std::string output = ExecuteWingetCommand("search . --source winget");
    
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
        
        // Parse package ID (second column)
        // Format: Name   PackageId   Version   Source
        std::istringstream lineStream(line);
        std::string name, packageId;
        lineStream >> name >> packageId;
        
        // Validate package ID format (should contain at least one dot)
        if (!packageId.empty() && packageId.find('.') != std::string::npos) {
            // Skip if it's a number (version column)
            if (packageId.find_first_not_of("0123456789.") == std::string::npos) {
                continue;
            }
            packages.push_back(packageId);
#ifdef _CONSOLE
            // Show first few IDs for verification
            if (packages.size() <= 5) {
                std::wcout << L"  ID " << packages.size() << L": " << StringToWString(packageId) << std::endl;
            }
#endif
        }
    }
    
    return packages;
}

void WinProgramUpdater::PopulateSearchDatabase() {
    // Empty the search database
    ExecuteSQLSearch("DELETE FROM search_results;");
    
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
    
    // Find packages in main DB but not in search DB
    const char* sql = 
        "SELECT package_id FROM apps "
        "WHERE package_id NOT IN (SELECT package_id FROM search_results);";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) {
                deletedPackages.push_back(reinterpret_cast<const char*>(text));
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
    
    while (std::getline(stream, line)) {
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = Trim(line.substr(0, colonPos));
        std::string value = Trim(line.substr(colonPos + 1));
        
        if (key == "Found") {
            info.name = value.substr(0, value.find(" ["));
        } else if (key == "Version") {
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
    
    return info;
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
    
    if (!OpenDatabase()) {
        return false;
    }
    
    if (!OpenSearchDatabase()) {
        CloseDatabase();
        return false;
    }
    
    // Step 1: Populate search database with winget search results
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 1: Query winget ===" << std::endl;
    auto stepStart = std::chrono::high_resolution_clock::now();
#endif
    PopulateSearchDatabase();
    
#ifdef _CONSOLE
    auto stepEnd = std::chrono::high_resolution_clock::now();
    auto stepDuration = std::chrono::duration_cast<std::chrono::seconds>(stepEnd - stepStart).count();
    std::wcout << L"   Time: " << stepDuration << L" seconds" << std::endl;
#endif
    
    // Attach search database to main database for SQL comparisons
#ifdef _CONSOLE
    std::wcout << L"Attaching search database..." << std::endl;
#endif
    // Use relative path for ATTACH (same directory as main DB)
    std::string attachSql = "ATTACH DATABASE 'WinProgramsSearch.db' AS search_db;";
    if (!ExecuteSQL(attachSql)) {
#ifdef _CONSOLE
        std::wcout << L"Failed to attach search database" << std::endl;
        std::wcout << L"SQL: " << StringToWString(attachSql) << std::endl;
#endif
        CloseSearchDatabase();
        CloseDatabase();
        return false;
    }
    
    // Step 2: Find new packages (in search but not in main)
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 2: Find new packages ===" << std::endl;
#endif
    
    auto newPackages = GetNewPackages();
    
#ifdef _CONSOLE
    std::wcout << L"Found " << newPackages.size() << L" new packages" << std::endl;
#endif
    
    // Add new packages
    for (const auto& packageId : newPackages) {
#ifdef _CONSOLE
        std::wcout << L"  Processing: " << StringToWString(packageId) << L"..." << std::flush;
#endif
        PackageInfo info = GetPackageInfo(packageId);
        if (!info.name.empty()) {
            AddPackage(info);
            stats.packagesAdded++;
            stats.tagsFromWinget += info.tags.size();
#ifdef _CONSOLE
            std::wcout << L" ✓ Added (" << info.tags.size() << L" tags)" << std::endl;
#endif
        } else {
#ifdef _CONSOLE
            std::wcout << L" ✗ Skipped (no info)" << std::endl;
#endif
        }
    }
    
    // Step 3: Find deleted packages (in main but not in search)
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 3: Find deleted packages ===" << std::endl;
#endif
    
    auto deletedPackages = GetDeletedPackages();
    
#ifdef _CONSOLE
    std::wcout << L"Found " << deletedPackages.size() << L" deleted packages" << std::endl;
#endif
    
    // Remove deleted packages
    for (const auto& packageId : deletedPackages) {
#ifdef _CONSOLE
        std::wcout << L"  Removing: " << StringToWString(packageId) << std::endl;
#endif
        RemovePackage(packageId);
        stats.packagesRemoved++;
    }
    
    // Step 4: Update tags for packages with zero tags
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 4: Update tags for zero-tag packages ===" << std::endl;
#endif
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT package_id FROM apps WHERE id NOT IN (SELECT DISTINCT app_id FROM app_categories);";
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::vector<std::string> zeroTagPackages;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            zeroTagPackages.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        
#ifdef _CONSOLE
        std::wcout << L"Found " << zeroTagPackages.size() << L" packages with zero tags" << std::endl;
#endif
        
        for (const auto& packageId : zeroTagPackages) {
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
#ifdef _CONSOLE
            if (addedTags > 0) {
                std::wcout << L" ✓ Added " << addedTags << L" tags" << std::endl;
            } else {
                std::wcout << L" (no tags)" << std::endl;
            }
#endif
        }
    }
    
    // Step 5: Apply inference
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 5: Apply name-based inference ===" << std::endl;
#endif
    ApplyNameBasedInference(stats);
    
    // Step 6: Apply correlation
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 6: Apply correlation analysis ===" << std::endl;
#endif
    ApplyCorrelationAnalysis(stats);
    
    // Step 7: Tag uncategorized
#ifdef _CONSOLE
    std::wcout << L"\n=== Step 7: Tag uncategorized ===" << std::endl;
#endif
    TagUncategorized(stats);
    
    stats.tagsAdded = stats.tagsFromWinget + stats.tagsFromInference + stats.tagsFromCorrelation;
    
    // Detach search database
    ExecuteSQL("DETACH DATABASE search_db;");
    
    CloseSearchDatabase();
    CloseDatabase();
    
    // Calculate elapsed time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    stats.elapsedSeconds = static_cast<double>(duration);
    
#ifdef _CONSOLE
    int hours = duration / 3600;
    int minutes = (duration % 3600) / 60;
    int seconds = duration % 60;
    std::wcout << L"\n=== Update Complete ===" << std::endl;
    std::wcout << L"Time the update took: " 
               << std::setfill(L'0') << std::setw(2) << hours << L":"
               << std::setfill(L'0') << std::setw(2) << minutes << L":"
               << std::setfill(L'0') << std::setw(2) << seconds << std::endl;
#endif
    
    return true;
}

std::string WinProgramUpdater::GetLogPath() {
    // Get executable path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    std::wstring wExePath(exePath);
    size_t lastSlash = wExePath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return "";  // Should never happen
    }
    
    // Get parent directory (go up one level)
    std::wstring exeDir = wExePath.substr(0, lastSlash);
    lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return "";  // Should never happen
    }
    
    std::wstring parentDir = exeDir.substr(0, lastSlash);
    std::wstring logDir = parentDir + L"\\log";
    
    // Convert to UTF-8
    std::string result = WStringToString(logDir);
    
    // Create directory if it doesn't exist
    CreateDirectoryA(result.c_str(), nullptr);
    
    return result;
}

void WinProgramUpdater::PruneOldLogEntries(const std::string& logPath) {
    std::ifstream inFile(logPath);
    if (!inFile) return;
    
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * LOG_RETENTION_DAYS);
    
    std::vector<std::string> validEntries;
    std::string line;
    bool inEntry = false;
    std::string currentEntry;
    
    while (std::getline(inFile, line)) {
        if (line.find("Run time: ") == 0) {
            // Parse timestamp
            std::string timestamp = line.substr(10, 19);
            std::tm tm = {};
            std::istringstream ss(timestamp);
            ss >> std::get_time(&tm, "%Y.%m.%d-%H:%M:%S");
            
            auto entryTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            
            if (!currentEntry.empty() && inEntry) {
                validEntries.push_back(currentEntry);
            }
            
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
    
    if (!currentEntry.empty() && inEntry) {
        validEntries.push_back(currentEntry);
    }
    
    inFile.close();
    
    // Write back
    std::ofstream outFile(logPath, std::ios::trunc);
    for (const auto& entry : validEntries) {
        outFile << entry;
    }
}

void WinProgramUpdater::WriteLog(const UpdateStats& stats) {
#if !ENABLE_LOGGING
    return;  // Logging disabled
#endif
    
    std::string logDir = GetLogPath();
    if (logDir.empty()) {
#ifdef _CONSOLE
        std::wcout << L"ERROR: GetLogPath returned empty!" << std::endl;
#endif
        return;
    }
    
#ifdef _CONSOLE
    std::wcout << L"Log directory: " << StringToWString(logDir) << std::endl;
#endif
    
    std::string logPath = logDir + "\\WinProgramUpdaterLog.txt";
    
#ifdef _CONSOLE
    std::wcout << L"Log path: " << StringToWString(logPath) << std::endl;
#endif
    
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
    
    // Format elapsed time as HH:MM:SS
    int totalSeconds = static_cast<int>(stats.elapsedSeconds);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    
    // Create new entry
    std::ostringstream newEntry;
    newEntry << "Time: " 
             << std::put_time(&tm, "%Y.%m.%d-%H:%M:%S") << "\n"
             << "Packages added: " << stats.packagesAdded << "\n"
             << "Packages removed: " << stats.packagesRemoved << "\n"
             << "Time to complete: " 
             << std::setfill('0') << std::setw(2) << hours << ":"
             << std::setfill('0') << std::setw(2) << minutes << ":"
             << std::setfill('0') << std::setw(2) << seconds << "\n\n";
    
    // Write new entry at top
    std::ofstream outFile(logPath, std::ios::trunc);
    if (outFile) {
        outFile << newEntry.str() << existingContent;
        outFile.close();
    }
    
    // Prune old entries
    PruneOldLogEntries(logPath);
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
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}
