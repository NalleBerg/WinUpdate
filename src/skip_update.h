#pragma once
#include <string>
#include <map>

// Add a skipped entry (id -> version). Returns true on success.
bool AddSkippedEntry(const std::string &id, const std::string &version);

// Remove a skipped entry by id. Returns true if removed.
bool RemoveSkippedEntry(const std::string &id);

// Load skipped map from per-user INI (id -> version)
std::map<std::string,std::string> LoadSkippedMap();

// Save skipped map to INI
bool SaveSkippedMap(const std::map<std::string,std::string> &m);

// Check whether a given availableVersion should be skipped according to stored skips.
// If stored skip exists and storedVersion == availableVersion -> return true.
// If storedVersion < availableVersion -> remove skip and return false.
// If storedVersion > availableVersion -> return true.
bool IsSkipped(const std::string &id, const std::string &availableVersion);

// Purge obsolete skipped entries using currentAvailable map (id->availableVersion)
void PurgeObsoleteSkips(const std::map<std::string,std::string> &currentAvail);

// Append a raw skipped line to the per-user INI under [skipped].
// This writes a line in the format: identifier<tab>version
// Returns true on success.
bool AppendSkippedRaw(const std::string &identifier, const std::string &version);
// Attempt to migrate any skipped entries that use display-names into ID-based entries.
// Returns true if any entries were migrated and saved.
bool MigrateSkippedEntries();
