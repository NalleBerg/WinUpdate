#pragma once
#include <string>
#include <atomic>

// Enable/disable logging and restart behavior are exposed here.
extern std::atomic<bool> g_enable_logging;
extern std::atomic<bool> g_restart_on_continue;

// Append text to the run log (no throw).
void AppendLog(const std::string &s);
