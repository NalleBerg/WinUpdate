#include "logging.h"
#include <fstream>

std::atomic<bool> g_enable_logging{true};
std::atomic<bool> g_restart_on_continue{true};

void AppendLog(const std::string &s) {
    if (!g_enable_logging.load()) return;
    try {
        std::ofstream ofs("wup_run_log.txt", std::ios::app | std::ios::binary);
        if (ofs) ofs << s;
    } catch(...) {}
}
