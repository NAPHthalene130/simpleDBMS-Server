/**
 * @file DbVersionStorageTest.cpp
 * @brief 数据库版本号存储层单元测�? * @details 测试 SystemCatalogManager �?.ver 文件的创建、读取、递增�? *          持久化、溢出处理等完整存储逻辑�? * @author NAPH130
 */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

void writeReport(const std::vector<TestStepResult> &steps, bool overall) {
    std::ofstream ofs("DbVersionStorageTestReport.md", std::ios::trunc);
    if (!ofs.good()) return;
    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    ofs << "# DbVersion Storage Test Report\n\n";
    ofs << "- Overall: " << (overall ? "PASS" : "FAIL") << "\n";
    ofs << "- Pass Rate: " << gPassedTests << "/" << gTotalTests << " (" << pct << "%)\n\n";
    ofs << "## Steps\n\n| ID | Step | Result | Detail |\n|---|---|---|---|\n";
    for (const auto &s : steps) {
        ofs << "| " << s.id << " | " << s.name << " | "
            << (s.passed ? "PASS" : "FAIL") << " | " << s.detail << " |\n";
    }
    if (!overall) {
        ofs << "\n## Failed Steps\n\n";
        for (const auto &s : steps) {
            if (!s.passed)
                ofs << "- **#" << s.id << " " << s.name << "**: " << s.detail << "\n";
        }
    }
}

} // namespace

int main() {
    const std::string DB = "ver_test_db";

    std::vector<TestStepResult> steps;
    bool overall = true;
    bool fatal = false;

    std::cout << "\n========== DbVersion Storage Test ==========\n";

    try {
        Core core;
        auto *scm = core.getStorageManager()->getSystemCatalogManager();
        if (scm == nullptr) {
            std::cerr << "FATAL: SystemCatalogManager is null\n";
            return 1;
        }

        // ====================== 1. 不存在的数据库版本号�?0 ======================
        {
            uint64_t ver = scm->getDatabaseVersion("non_existent_db_xyz");
            bool p = (ver == 0);
            appendStep(steps, 1, "getDatabaseVersion non-existent db returns 0", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-1\n";
        }

        // ====================== 2. 空数据库名返�?0 ======================
        {
            uint64_t ver = scm->getDatabaseVersion("");
            bool p = (ver == 0);
            appendStep(steps, 2, "getDatabaseVersion empty name returns 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-2\n";
        }

        // ====================== 3. addDatabaseVersion 空名安全返回 ======================
        {
            // 不应崩溃
            scm->addDatabaseVersion("");
            bool p = true;
            appendStep(steps, 3, "addDatabaseVersion empty name does not crash", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-3\n";
        }

        // ====================== 4. 创建数据库后版本号为 0 ======================
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = created && (ver == 0);
            appendStep(steps, 4, "Version is 0 after CREATE DATABASE", p,
                       p ? "ok" : "created=" + std::to_string(created) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-1\n";
        }

        // ====================== 5. 首次 addDatabaseVersion �?0 递增�?1 ======================
        {
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 1);
            appendStep(steps, 5, "First addDatabaseVersion: 0->1", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-2\n";
        }

        // ====================== 6. 连续递增版本�?======================
        {
            scm->addDatabaseVersion(DB); // 1 -> 2
            scm->addDatabaseVersion(DB); // 2 -> 3
            scm->addDatabaseVersion(DB); // 3 -> 4
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 4);
            appendStep(steps, 6, "Sequential addDatabaseVersion 1->4", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-3\n";
        }

        // ====================== 7. .ver 文件存在�?======================
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            bool p = std::filesystem::exists(verPath);
            appendStep(steps, 7, ".ver file created on disk", p,
                       p ? verPath.string() : "file not found: " + verPath.string());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-4\n";
        }

        // ====================== 8. .ver 文件大小正确 ======================
        {
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            auto fileSize = std::filesystem::file_size(verPath);
            bool p = (fileSize == sizeof(uint64_t));
            appendStep(steps, 8, ".ver file size is sizeof(uint64_t)", p,
                       p ? std::to_string(fileSize) + " bytes" : "got " + std::to_string(fileSize));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-5\n";
        }

        // ====================== 9. 版本号持久化（重新读取） ======================
        {
            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB);
            bool p = (ver1 == ver2 && ver1 == 4);
            appendStep(steps, 9, "Version persists across reads", p,
                       p ? "ok" : "ver1=" + std::to_string(ver1) + " ver2=" + std::to_string(ver2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-6\n";
        }

        // ====================== 10. 大数值递增测试 ======================
        {
            // 手动写入一个大值到 .ver 文件，然后测试递增
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
            appendStep(steps, 10, "Large value increment 99998->99999", p,
                       p ? "ok" : "before=" + std::to_string(beforeAdd) + " after=" + std::to_string(afterAdd));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-1\n";
        }

        // ====================== 11. 递增多次到较大�?======================
        {
            for (int i = 0; i < 100; ++i) {
                scm->addDatabaseVersion(DB);
            }
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == 100099);
            appendStep(steps, 11, "Increment 100 times to 100099", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-2\n";
        }

        // ====================== 12. UINT64_MAX 溢出回到 0 ======================
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
            appendStep(steps, 12, "UINT64_MAX overflow wraps to 0", p,
                       p ? "ok" : "before=" + std::to_string(beforeAdd) + " after=" + std::to_string(afterAdd));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-3\n";
        }

        // ====================== 13. 溢出不等于最小值加1（确保使�?=比较而非>�?======================
        {
            // �?UINT64_MAX-1 递增�?UINT64_MAX 应该正常
            auto verPath = scm->getDataRootPath() / DB / (DB + ".ver");
            {
                uint64_t val = UINT64_MAX - 1;
                std::ofstream ofs(verPath, std::ios::binary | std::ios::trunc);
                ofs.write(reinterpret_cast<const char *>(&val), sizeof(uint64_t));
            }
            scm->addDatabaseVersion(DB);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = (ver == UINT64_MAX);
            appendStep(steps, 13, "UINT64_MAX-1 -> UINT64_MAX normal increment", p,
                       p ? "ok" : "got " + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-4\n";
        }

        // ====================== 14. 删除数据库后版本查询安全 ======================
        {
            bool dropped = scm->dropDatabase(DB);
            // 数据库已删除，版本文件可能随目录一起被移除
            // getDatabaseVersion 应安全处理不存在的文件
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = dropped && (ver == 0);
            appendStep(steps, 14, "dropDatabase then getDatabaseVersion returns 0", p,
                       p ? "ok" : "dropped=" + std::to_string(dropped) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-1\n";
        }

        // ====================== 15. 重新创建同名数据库版本从 0 开始 ======================
        {
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB));
            bool created = scm->createDatabase(dbInfo);
            uint64_t ver = scm->getDatabaseVersion(DB);
            bool p = created && (ver == 0);
            appendStep(steps, 15, "Re-create database resets version to 0", p,
                       p ? "ok" : "created=" + std::to_string(created) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-2\n";
        }

        // ====================== 16. 多个数据库版本独�?======================
        {
            const std::string DB2 = "ver_test_db2";
            DatabaseBlock dbInfo2;
            dbInfo2.setName(makeDbName(DB2));
            scm->createDatabase(dbInfo2);

            scm->addDatabaseVersion(DB);   // DB:  0->1
            scm->addDatabaseVersion(DB);   // DB:  1->2
            scm->addDatabaseVersion(DB2);  // DB2: 0->1

            uint64_t ver1 = scm->getDatabaseVersion(DB);
            uint64_t ver2 = scm->getDatabaseVersion(DB2);
            bool p = (ver1 == 2 && ver2 == 1);
            appendStep(steps, 16, "Multiple databases have independent versions", p,
                       p ? "ok" : "DB=" + std::to_string(ver1) + " DB2=" + std::to_string(ver2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-3\n";

            // 清理
            scm->dropDatabase(DB);
            scm->dropDatabase(DB2);
        }

        // ====================== 17. getAllDatabases + getVersion 组合 ======================
        {
            const std::string DB3 = "ver_test_combodb";
            DatabaseBlock dbInfo;
            dbInfo.setName(makeDbName(DB3));
            scm->createDatabase(dbInfo);
            scm->addDatabaseVersion(DB3);
            scm->addDatabaseVersion(DB3);

            auto allDbs = scm->getAllDatabases();
            bool found = false;
            uint64_t ver = 0;
            for (const auto &db : allDbs) {
                const auto *name = reinterpret_cast<const char *>(db.getName().data());
                if (std::string(name) == DB3) {
                    found = true;
                    ver = scm->getDatabaseVersion(DB3);
                    break;
                }
            }
            bool p = found && (ver == 2);
            appendStep(steps, 17, "getAllDatabases + getDatabaseVersion consistency", p,
                       p ? "ok" : "found=" + std::to_string(found) + " ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-4\n";

            scm->dropDatabase(DB3);
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

    writeReport(steps, overall);
    return overall ? 0 : 1;
}
