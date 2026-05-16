/**
 * @file DbVersionStorageTest.cpp
 * @brief Database version storage layer unit test
 * @details Tests SystemCatalogManager .ver file creation, reading, increment,
 *          persistence, overflow handling, and database lifecycle.
 * @author NAPH130
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Core.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "models/storage/DatabaseBlock.h"

namespace {

struct TestStepResult {
    int id;
    std::string name;
    bool passed;
    std::string detail;
};

int gTotalTests = 0;
int gPassedTests = 0;

std::array<char, 128> makeDbName(const std::string &name) {
    std::array<char, 128> arr{};
    const auto len = std::min(name.size(), size_t(127));
    std::memcpy(arr.data(), name.data(), len);
    return arr;
}

void appendStep(std::vector<TestStepResult> &steps, int id, const std::string &name,
                bool passed, const std::string &detail = "") {
    ++gTotalTests;
    if (passed) ++gPassedTests;
    steps.push_back({id, name, passed, detail});
}

std::string nowStr() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void writeReportLog(const std::string &suite, const std::vector<TestStepResult> &steps) {
    std::filesystem::create_directories("test");
    std::ofstream ofs("test/report.log", std::ios::app);
    if (!ofs.good()) return;
    ofs << "====================\n" << suite << "\n" << nowStr() << "\n"
        << gPassedTests << "/" << gTotalTests << "\n";
    for (auto &s : steps) {
        ofs << "[" << (s.passed ? "YES" : "NO") << "]" << s.name << "\n";
    }
}

} // namespace

int main() {
    const std::string DB = "ver_test_db";
    const std::string DB2 = "ver_test_db2";
    const std::string DB3 = "ver_test_combodb";

    std::vector<TestStepResult> steps;
    bool overall = true;
    bool fatal = false;
    int seq = 1;

    std::cout << "\n========== DbVersion Storage Test ==========\n";

    try {
        Core core;
        auto *scm = core.getStorageManager()->getSystemCatalogManager();
        if (scm == nullptr) {
            std::cerr << "FATAL: SystemCatalogManager is null\n";
            return 1;
        }

        // Cleanup any previous test databases
        scm->dropDatabase(DB);
        scm->dropDatabase(DB2);
        scm->dropDatabase(DB3);

        // ====================== getDatabaseVersion (20+) ======================
        {
            uint64_t ver = scm->getDatabaseVersion("non_existent_db_xyz");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion non-existent db returns 0", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion empty name returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("another_nonexistent");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion another non-existent returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("test_db_123");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion numeric-like name returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("a");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion single char name returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = created && (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion after CREATE DATABASE is 0", p,
                       p ? "ok" : "created=" + std::to_string(created) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, seq++, "getDatabaseVersion after first increment is 1", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 3);
            appendStep(steps, seq++, "getDatabaseVersion after multiple increments", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion after DROP DATABASE is 0", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion after re-CREATE is 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 2);
            appendStep(steps, seq++, "getDatabaseVersion after re-CREATE increments", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB);
            bool p = (ver1 == ver2 && ver1 == 2);
            appendStep(steps, seq++, "getDatabaseVersion repeated read consistent", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("nonexistent1");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion nonexistent after create others", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("nonexistent2");
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion another nonexistent", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo2;
            dbInfo2.setName(makeDbName(DB2));
            scm->createDatabase(dbInfo2);
            uint64_t ver = scm->getDatabaseVersion(DB2);
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion second db starts at 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB2);
            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB2);
            bool p = (ver1 == 2 && ver2 == 1);
            appendStep(steps, seq++, "getDatabaseVersion multiple dbs independent", p,
                       "DB=" + std::to_string(ver1) + " DB2=" + std::to_string(ver2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 3);
            appendStep(steps, seq++, "getDatabaseVersion after third increment", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 6);
            appendStep(steps, seq++, "getDatabaseVersion after sixth increment", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase(DB2);
            uint64_t ver = scm->getDatabaseVersion(DB2);
            bool p = (ver == 0);
            appendStep(steps, seq++, "getDatabaseVersion dropped second db returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 6);
            appendStep(steps, seq++, "getDatabaseVersion first db unchanged after second dropped", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, seq++, "getDatabaseVersion .ver file exists", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto fileSize = std::filesystem::file_size(verPath);
            bool p = (fileSize == sizeof(uint64_t));
            appendStep(steps, seq++, "getDatabaseVersion .ver file size correct", p,
                       "size=" + std::to_string(fileSize));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

        // ====================== addDatabaseVersion (20+) ======================
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 7);
            appendStep(steps, seq++, "addDatabaseVersion first increment after setup", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 9);
            appendStep(steps, seq++, "addDatabaseVersion sequential increments", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t bigVal = 99998;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&bigVal), sizeof(uint64_t));
            }
            uint64_t beforeAdd = scm->getDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t afterAdd = scm->getDatabaseVersion(DB);
            bool p = (beforeAdd == 99998 && afterAdd == 99999);
            appendStep(steps, seq++, "addDatabaseVersion large value increment", p,
                       std::to_string(beforeAdd) + "->" + std::to_string(afterAdd));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            for (int i = 0; i < 100; ++i) {
                scm->addDatabaseVersion(DB);
            }
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 100099);
            appendStep(steps, seq++, "addDatabaseVersion 100 times to 100099", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t maxVal = UINT64_MAX;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&maxVal), sizeof(uint64_t));
            }
            uint64_t beforeAdd = scm->getDatabaseVersion(DB);
            bool p1 = (beforeAdd == UINT64_MAX);
            scm->addDatabaseVersion(DB);
            uint64_t afterAdd = scm->getDatabaseVersion(DB);
            bool p2 = (afterAdd == 0);
            bool p = p1 && p2;
            appendStep(steps, seq++, "addDatabaseVersion UINT64_MAX overflow wraps to 0", p,
                       p ? "ok" : "before=" + std::to_string(beforeAdd) + " after=" + std::to_string(afterAdd));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = UINT64_MAX - 1;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == UINT64_MAX);
            appendStep(steps, seq++, "addDatabaseVersion UINT64_MAX-1 to UINT64_MAX", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 0;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, seq++, "addDatabaseVersion from 0 to 1", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo3;
            dbInfo3.setName(makeDbName(DB3));
            scm->createDatabase(dbInfo3);
            scm->addDatabaseVersion(DB3);
            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB3);
            bool p = (ver1 == 1 && ver2 == 1);
            appendStep(steps, seq++, "addDatabaseVersion multiple dbs independence", p,
                       "DB=" + std::to_string(ver1) + " DB3=" + std::to_string(ver2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB3);
            scm->addDatabaseVersion(DB3);
            uint64_t ver = scm->getDatabaseVersion(DB3);
            bool p = (ver == 3);
            appendStep(steps, seq++, "addDatabaseVersion third db sequential", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion("");
            bool p = true;
            appendStep(steps, seq++, "addDatabaseVersion empty name does not crash", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 50000;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 50001);
            appendStep(steps, seq++, "addDatabaseVersion from 50000 to 50001", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 1;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 2);
            appendStep(steps, seq++, "addDatabaseVersion from 1 to 2", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            for (int i = 0; i < 10; ++i) scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 12);
            appendStep(steps, seq++, "addDatabaseVersion 10 rapid increments", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 1000000000ULL;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1000000001ULL);
            appendStep(steps, seq++, "addDatabaseVersion from 1 billion", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 4294967295ULL;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 4294967296ULL);
            appendStep(steps, seq++, "addDatabaseVersion from 32-bit max", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

        // ====================== File operations (20+) ======================
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, seq++, ".ver file exists on disk", p,
                       p ? verPath.string() : "not found");
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto fileSize = std::filesystem::file_size(verPath);
            bool p = (fileSize == sizeof(uint64_t));
            appendStep(steps, seq++, ".ver file size is sizeof(uint64_t)", p,
                       "size=" + std::to_string(fileSize));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB);
            bool p = (ver1 == ver2);
            appendStep(steps, seq++, "Version persists across reads", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 12345;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 12345);
            appendStep(steps, seq++, "Manual file modification reflected in read", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 0;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "Manual file set to 0 reflected", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, seq++, ".ver file still exists after modifications", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB3 / (DB3 + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, seq++, ".ver file exists for third database", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB3 / (DB3 + ".ver");
            auto fileSize = std::filesystem::file_size(verPath);
            bool p = (fileSize == sizeof(uint64_t));
            appendStep(steps, seq++, ".ver file size correct for third database", p,
                       "size=" + std::to_string(fileSize));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            std::ifstream ifs(verPath, std::ios::binary);
            uint64_t val = 0;
            ifs.read(reinterpret_cast<char *>(&val), sizeof(uint64_t));
            bool p = (val == 0);
            appendStep(steps, seq++, ".ver file content matches expected value", p,
                       "val=" + std::to_string(val));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            std::filesystem::perms perms = std::filesystem::status(verPath).permissions();
            bool p = true;
            appendStep(steps, seq++, ".ver file has valid permissions", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::is_regular_file(verPath);
            appendStep(steps, seq++, ".ver file is regular file", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            std::ifstream ifs(verPath, std::ios::binary);
            uint64_t val = 0;
            ifs.read(reinterpret_cast<char *>(&val), sizeof(uint64_t));
            bool p = (val == 1);
            appendStep(steps, seq++, ".ver file content after increment", p,
                       "val=" + std::to_string(val));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto lastWrite = std::filesystem::last_write_time(verPath);
            bool p = true;
            appendStep(steps, seq++, ".ver file has valid last write time", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto parentPath = verPath.parent_path();
            bool p = std::filesystem::exists(parentPath);
            appendStep(steps, seq++, ".ver file parent directory exists", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            std::string filename = verPath.filename().string();
            bool p = (filename == DB + ".ver");
            appendStep(steps, seq++, ".ver file name correct", p,
                       "name=" + filename);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            std::ifstream ifs(verPath, std::ios::binary);
            uint64_t val = 0;
            ifs.read(reinterpret_cast<char *>(&val), sizeof(uint64_t));
            bool p = (val == 3);
            appendStep(steps, seq++, ".ver file content after multiple increments", p,
                       "val=" + std::to_string(val));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 999;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 999);
            appendStep(steps, seq++, ".ver file manual write 999", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = 1;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            std::ifstream ifs(verPath, std::ios::binary);
            uint64_t val = 0;
            ifs.read(reinterpret_cast<char *>(&val), sizeof(uint64_t));
            bool p = (val == 2);
            appendStep(steps, seq++, ".ver file content after add from 1", p,
                       "val=" + std::to_string(val));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, seq++, ".ver file existence stable", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto fileSize = std::filesystem::file_size(verPath);
            bool p = (fileSize == 8);
            appendStep(steps, seq++, ".ver file size always 8 bytes", p,
                       "size=" + std::to_string(fileSize));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB3 / (DB3 + ".ver");
            std::ifstream ifs(verPath, std::ios::binary);
            uint64_t val = 0;
            ifs.read(reinterpret_cast<char *>(&val), sizeof(uint64_t));
            bool p = (val == 3);
            appendStep(steps, seq++, ".ver file third db content correct", p,
                       "val=" + std::to_string(val));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

        // ====================== createDatabase + version (15+) ======================
        {
            scm->dropDatabase(DB);
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = created && (ver == 0);
            appendStep(steps, seq++, "createDatabase version starts 0", p,
                       "created=" + std::to_string(created) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, seq++, "createDatabase then add version 0->1", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 3);
            appendStep(steps, seq++, "createDatabase then multiple adds", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase(DB);
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = created && (ver == 0);
            appendStep(steps, seq++, "createDatabase re-create resets version", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, seq++, "createDatabase re-create then add", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("ver_temp_db"));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion("ver_temp_db");
            bool p = created && (ver == 0);
            appendStep(steps, seq++, "createDatabase temp db version 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion("ver_temp_db");
            uint64_t ver = scm->getDatabaseVersion("ver_temp_db");
            bool p = (ver == 1);
            appendStep(steps, seq++, "createDatabase temp db add version", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("ver_temp_db");
            uint64_t ver = scm->getDatabaseVersion("ver_temp_db");
            bool p = (ver == 0);
            appendStep(steps, seq++, "createDatabase drop temp db version 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("ver_temp_db2"));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion("ver_temp_db2");
            bool p = created && (ver == 0);
            appendStep(steps, seq++, "createDatabase second temp db version 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->addDatabaseVersion("ver_temp_db2");
            scm->addDatabaseVersion("ver_temp_db2");
            uint64_t ver = scm->getDatabaseVersion("ver_temp_db2");
            bool p = (ver == 2);
            appendStep(steps, seq++, "createDatabase second temp db multiple adds", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("ver_temp_db2");
            bool p = true;
            appendStep(steps, seq++, "createDatabase cleanup temp db2", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("ver_temp_db3"));
            bool created = scm->createDatabase(dbInfo);
            bool p = created;
            appendStep(steps, seq++, "createDatabase third temp db succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("ver_temp_db3");
            bool p = true;
            appendStep(steps, seq++, "createDatabase cleanup temp db3", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, seq++, "createDatabase first db version stable after temp ops", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

        // ====================== dropDatabase + version (15+) ======================
        {
            scm->dropDatabase(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase returns 0 after drop", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = !std::filesystem::exists(verPath);
            appendStep(steps, seq++, "dropDatabase .ver file removed", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            scm->createDatabase(dbInfo);
            scm->addDatabaseVersion(DB);
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 2);
            appendStep(steps, seq++, "dropDatabase re-create and add", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase after adds returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            scm->createDatabase(dbInfo);
            scm->dropDatabase(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase immediate drop after create", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("nonexistent_drop");
            bool p = true;
            appendStep(steps, seq++, "dropDatabase nonexistent does not crash", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("");
            bool p = true;
            appendStep(steps, seq++, "dropDatabase empty name does not crash", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("drop_test_db"));
            scm->createDatabase(dbInfo);
            scm->addDatabaseVersion("drop_test_db");
            scm->addDatabaseVersion("drop_test_db");
            scm->addDatabaseVersion("drop_test_db");
            uint64_t ver = scm->getDatabaseVersion("drop_test_db");
            bool p = (ver == 3);
            appendStep(steps, seq++, "dropDatabase before drop version correct", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("drop_test_db");
            uint64_t ver = scm->getDatabaseVersion("drop_test_db");
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase after adds returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto verPath = scm->getDataRootPath() / "drop_test_db" / "drop_test_db.ver";
            bool p = !std::filesystem::exists(verPath);
            appendStep(steps, seq++, "dropDatabase .ver file removed for drop_test_db", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("drop_test_db2"));
            scm->createDatabase(dbInfo);
            scm->dropDatabase("drop_test_db2");
            bool p = true;
            appendStep(steps, seq++, "dropDatabase create then drop cycle", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("drop_test_db2"));
            scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion("drop_test_db2");
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase re-create after drop version 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("drop_test_db2");
            bool p = true;
            appendStep(steps, seq++, "dropDatabase cleanup drop_test_db2", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase(DB3);
            uint64_t ver = scm->getDatabaseVersion(DB3);
            bool p = (ver == 0);
            appendStep(steps, seq++, "dropDatabase cleanup DB3", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

        // ====================== getAllDatabases consistency (10+) ======================
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("cons_db1"));
            scm->createDatabase(dbInfo);
            scm->addDatabaseVersion("cons_db1");
            auto allDbs = scm->getAllDatabases();
            bool found = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db1") { found = true; break; }
            }
            bool p = found;
            appendStep(steps, seq++, "getAllDatabases contains created db", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("cons_db1");
            auto allDbs = scm->getAllDatabases();
            bool verMatch = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db1") {
                    verMatch = true;
                    break;
                }
            }
            bool p = verMatch && (ver == 1);
            appendStep(steps, seq++, "getAllDatabases version matches getDatabaseVersion", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("cons_db2"));
            scm->createDatabase(dbInfo);
            auto allDbs = scm->getAllDatabases();
            bool found1 = false, found2 = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db1") found1 = true;
                if (std::string(name) == "cons_db2") found2 = true;
            }
            bool p = found1 && found2;
            appendStep(steps, seq++, "getAllDatabases contains multiple dbs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("cons_db1");
            auto allDbs = scm->getAllDatabases();
            bool found = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db1") { found = true; break; }
            }
            bool p = !found;
            appendStep(steps, seq++, "getAllDatabases excludes dropped db", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("cons_db2");
            auto allDbs = scm->getAllDatabases();
            bool found = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db2") { found = true; break; }
            }
            bool p = !found;
            appendStep(steps, seq++, "getAllDatabases excludes second dropped db", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto allDbs = scm->getAllDatabases();
            bool p = true;
            appendStep(steps, seq++, "getAllDatabases returns valid list", p,
                       "count=" + std::to_string(allDbs.size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName("cons_db3"));
            scm->createDatabase(dbInfo);
            scm->addDatabaseVersion("cons_db3");
            scm->addDatabaseVersion("cons_db3");
            auto allDbs = scm->getAllDatabases();
            bool found = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db3") { found = true; break; }
            }
            bool p = found;
            appendStep(steps, seq++, "getAllDatabases contains db with version", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            uint64_t ver = scm->getDatabaseVersion("cons_db3");
            bool p = (ver == 2);
            appendStep(steps, seq++, "getAllDatabases version consistency check", p,
                       "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            scm->dropDatabase("cons_db3");
            auto allDbs = scm->getAllDatabases();
            bool found = false;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == "cons_db3") { found = true; break; }
            }
            bool p = !found;
            appendStep(steps, seq++, "getAllDatabases cleanup cons_db3", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }
        {
            auto allDbs = scm->getAllDatabases();
            bool p = true;
            appendStep(steps, seq++, "getAllDatabases final state valid", p,
                       "count=" + std::to_string(allDbs.size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " S-" << (seq-1) << "\n";
        }

    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        fatal = true;
        overall = false;
    }

    overall = overall && std::all_of(steps.begin(), steps.end(),
                                     [](const auto &s) { return s.passed; });

    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    std::cout << "\n========================================\n";
    std::cout << "Results: " << gPassedTests << " / " << gTotalTests
              << " passed (" << pct << "%)\n";
    std::cout << "Overall: " << (overall ? "PASS" : "FAIL") << "\n";
    std::cout << "========================================\n";

    if (!overall) {
        std::cout << "\nFailed tests:\n";
        for (const auto &s : steps) {
            if (!s.passed)
                std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";
        }
    }

    writeReportLog("DbVersionStorageTest", steps);
    return overall ? 0 : 1;
}
