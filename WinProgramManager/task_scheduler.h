#pragma once
#include <string>

// Try to create the scheduled updater task on first run.
// currentLang: value to write to INI if task creation succeeds.
void TryCreateUpdaterTaskIfFirstRun(const std::wstring& currentLang);

// Lightweight task info for querying the updater scheduled task
struct TaskInfo {
	bool exists = false;
	bool enabled = false;
	int intervalDays = -1; // -1 = unknown
	std::wstring nextRun;  // human readable Next Run time
	std::wstring rawInfo;  // full schtasks output
};

// Query the scheduled task information for WinProgramUpdaterWeekly
TaskInfo QueryUpdaterTaskInfo();

// Create or update the scheduled task. startTime should be "HH:MM" (24h).
// Returns true on success.
// runIfUnavailable -> when true, sets StartWhenAvailable in the task so it runs
// as soon as possible if a scheduled start was missed.
bool CreateOrUpdateUpdaterTask(int intervalDays, const std::wstring& startTime, bool runIfUnavailable);

// Enable/Disable/Delete task
bool EnableUpdaterTask();
bool DisableUpdaterTask();

// Run the updater now: prefer schtasks /Run, fallback to launching updater --hidden
bool RunUpdaterNow();
