#pragma once
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

inline void writeReport(
    const std::string &testName,
    int passed, int failed,
    const std::vector<int> &passedIds,
    const std::vector<int> &failedIds)
{
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ts;
    ts << (tm.tm_year + 1900) << "-" << std::setw(2) << std::setfill('0') << (tm.tm_mon + 1) << "-"
       << std::setw(2) << tm.tm_mday << " " << std::setw(2) << tm.tm_hour << ":"
       << std::setw(2) << tm.tm_min << ":" << std::setw(2) << tm.tm_sec;

    auto logPath = std::filesystem::path(SERVER_PROJECT_ROOT) / "src" / "test" / "report.log";
    std::ofstream log(logPath, std::ios::app);
    if (log.is_open()) {
        log << "==========\n" << testName << "\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
        if (!passedIds.empty()) {
            log << "Passed: ";
            for (size_t i = 0; i < passedIds.size(); ++i) {
                if (i > 0) log << ",";
                log << passedIds[i];
            }
            log << "\n";
        }
        if (!failedIds.empty()) {
            log << "Failed: ";
            for (size_t i = 0; i < failedIds.size(); ++i) {
                if (i > 0) log << ",";
                log << failedIds[i];
            }
            log << "\n";
        }
    }
}
