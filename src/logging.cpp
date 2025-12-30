#include "logging.h"
#include <fstream>
#include <filesystem>

std::atomic<bool> g_enable_logging{true};
std::atomic<bool> g_restart_on_continue{true};

void AppendLog(const std::string &s) {
    if (!g_enable_logging.load()) return;
    try {
        // ensure logs directory exists and write there so workspace can read logs reliably
        try { std::filesystem::create_directories(std::filesystem::path("logs")); } catch(...) {}
        std::ofstream ofs("logs/wup_run_log.txt", std::ios::app | std::ios::binary);
        if (ofs) ofs << s;
    } catch(...) {}
}
