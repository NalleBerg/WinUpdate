#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

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
    std::vector<unsigned char> iconData;
    std::string iconType;
    std::vector<std::string> tags;
    // Additional metadata fields
    std::string publisherUrl;
    std::string publisherSupportUrl;
    std::string copyrightUrl;
    std::string releaseNotesUrl;
    std::string releaseDate;
    std::string source;
    std::string installerType;
    std::string architecture;
    std::string documentationUrl;
    std::string installerUrl;
    std::string installerSha256;
    std::string offlineDistributionSupported;
    std::string commands;
};

struct UpdateStats {
    int packagesAdded = 0;
    int packagesRemoved = 0;
    int packagesUpdated = 0;
    int tagsAdded = 0;
    int tagsFromWinget = 0;
    int tagsFromInference = 0;
    int tagsFromCorrelation = 0;
    int uncategorized = 0;
    double elapsedSeconds = 0.0;
};

// Callback typedefs for GUI integration
typedef void (*LogCallbackFunc)(const std::string& message, void* userData);
typedef void (*StatsCallbackFunc)(int found, int added, int deleted, void* userData);

class WinProgramUpdater {
public:
    WinProgramUpdater(const std::wstring& dbPath);
    ~WinProgramUpdater();

    // Main update function
    bool UpdateDatabase(UpdateStats& stats);

    // Callback setters for GUI
    void SetLogCallback(LogCallbackFunc callback, void* userData);
    void SetStatsCallback(StatsCallbackFunc callback, void* userData);
    void SetCancelFlag(std::atomic<bool>* flag);

    // Logging
    void WriteAppDataLog(const UpdateStats& stats, const std::string& duration);

    // Installed apps sync
    bool SyncInstalledApps();

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
    void FetchIconFromHomepage(const std::string& homepage, std::vector<unsigned char>& iconData, std::string& iconType);

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
    std::string GetAppDataLogPath();
    void PruneAppDataLog();
    void Log(const std::string& message);
    bool IsCancelled() const;
    void NotifyStats(int found, int added, int deleted);

    // Tag pattern mappings
    void InitializeTagPatterns();
    std::map<std::string, std::string> tagPatterns_;

    // Database
    sqlite3* db_;
    sqlite3* searchDb_;
    std::wstring dbPath_;
    std::wstring searchDbPath_;

    // Callbacks for GUI
    LogCallbackFunc logCallback_;
    void* logUserData_;
    StatsCallbackFunc statsCallback_;
    void* statsUserData_;
    std::atomic<bool>* cancelFlag_;

    // Constants
    static constexpr int MAX_RETRIES = 3;
    static constexpr int LOG_RETENTION_DAYS = 90;
};
