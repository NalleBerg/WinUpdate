#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

// Configuration
#define ENABLE_LOGGING true  // Set to false to disable all logging

// Forward declaration for SQLite
typedef struct sqlite3 sqlite3;

struct PackageInfo {
    std::string packageId;
    std::string name;
    std::string version;
    std::string publisher;
    std::string moniker;
    std::string description;
    std::string shortDescription;
    std::string homepage;
    std::string license;
    std::string author;
    std::string copyright;
    std::string licenseUrl;
    std::string privacyUrl;
    std::string packageUrl;
    std::vector<std::string> tags;
};

struct UpdateStats {
    int packagesAdded = 0;
    int packagesRemoved = 0;
    int tagsAdded = 0;
    int tagsFromWinget = 0;
    int tagsFromInference = 0;
    int tagsFromCorrelation = 0;
    int uncategorized = 0;
    double elapsedSeconds = 0.0;
};

class WinProgramUpdater {
public:
    WinProgramUpdater(const std::wstring& dbPath);
    ~WinProgramUpdater();

    // Main update function
    bool UpdateDatabase(UpdateStats& stats);

    // Logging
    void WriteLog(const UpdateStats& stats);

private:
    // Database operations
    bool OpenDatabase();
    void CloseDatabase();
    bool OpenSearchDatabase();
    void CloseSearchDatabase();
    bool ExecuteSQL(const std::string& sql);
    bool ExecuteSQLSearch(const std::string& sql);
    std::vector<std::string> QueryPackageIds();
    bool HasTags(const std::string& packageId);
    void AddPackage(const PackageInfo& pkg);
    std::string GetLogPath();
    void RemovePackage(const std::string& packageId);
    void AddTag(const std::string& packageId, const std::string& tag);
    int GetCategoryId(const std::string& category);
    int GetPackageDbId(const std::string& packageId);

    // Winget operations
    void PopulateSearchDatabase();
    std::vector<std::string> GetNewPackages();
    std::vector<std::string> GetDeletedPackages();
    std::vector<std::string> GetWingetPackages();
    PackageInfo GetPackageInfo(const std::string& packageId, int attempt = 1);
    std::string ExecuteWingetCommand(const std::string& command);

    // Tag inference
    void ApplyNameBasedInference(UpdateStats& stats);
    void ApplyCorrelationAnalysis(UpdateStats& stats);
    void TagUncategorized(UpdateStats& stats);
    std::vector<std::string> ExtractTagsFromText(const std::string& name, 
                                                   const std::string& packageId,
                                                   const std::string& moniker);

    // Utility functions
    bool IsNumericOnly(const std::string& packageId);
    std::string Trim(const std::string& str);
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
    std::string GetAppDataPath();
    void PruneOldLogEntries(const std::string& logPath);

    // Tag pattern mappings
    void InitializeTagPatterns();
    std::map<std::string, std::string> tagPatterns_;

    // Database
    sqlite3* db_;
    sqlite3* searchDb_;
    std::wstring dbPath_;
    std::wstring searchDbPath_;

    // Constants
    static constexpr int MAX_RETRIES = 3;
    static constexpr int LOG_RETENTION_DAYS = 90;
};
