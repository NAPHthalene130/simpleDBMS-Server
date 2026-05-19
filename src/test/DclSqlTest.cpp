/**
 * @brief DCL SQL 测试 — 验证 GRANT/REVOKE 用户管理
 * @author NAPH130
 */
#include "Core.h"
#include "core/SqlPipeline.h"
#include "log/LogWriter.h"
#include "models/network/NetworkExecutionContext.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

int main()
{
    Core core;
    core.start();
    core.stop();

    SqlPipeline *pipeline = core.getSqlPipeline();

    NetworkExecutionContext netCtx;
    netCtx.setConnectionId("dcl-test");
    netCtx.setCurrentUser("tester");

    int passed = 0;
    int failed = 0;

    auto sendSql = [&](const std::string &sql) -> nlohmann::json {
        const std::string req = NetData("sql", sql).toJson();
        const NetData resp = pipeline->handleRequest(req, &netCtx);
        auto j = nlohmann::json::parse(resp.getContent());
        if (j.value("success", false)) {
            const std::string db = j.value("dbName", "");
            if (!db.empty()) netCtx.setCurrentDbName(db);
        }
        return j;
    };

    auto check = [&](bool ok, const std::string &desc, const std::string &extra = "") {
        if (ok) { std::cout << "PASS: " << desc << std::endl; ++passed; }
        else { std::cout << "FAIL: " << desc; if (!extra.empty()) std::cout << " [" << extra << "]"; std::cout << std::endl; ++failed; }
    };

    // a) USE DATABASE system
    check(sendSql("USE DATABASE system;").value("success", false), "USE DATABASE system");

    // b) SELECT * FROM user -> at least 1 row
    {
        auto j = sendSql("SELECT * FROM user;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() >= 1;
        check(ok, "SELECT * FROM user -> >=1 row");
    }

    // c) GRANT (create user)
    check(sendSql("GRANT ALL PRIVILEGES TO testuser IDENTIFIED BY testpass123;").value("success", false), "GRANT create user testuser");

    // d) SELECT created user
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool found = false;
        for (auto &row : rs) {
            if (row.is_array() && row.size() >= 2 && row[0].get<std::string>() == "testuser"
                && row[1].get<std::string>() == "testpass123") found = true;
        }
        check(ok && found, "SELECT user where id=testuser -> found");
    }

    // e) GRANT duplicate -> should FAIL
    {
        auto j = sendSql("GRANT ALL PRIVILEGES TO testuser IDENTIFIED BY testpass123;");
        check(!j.value("success", false), "GRANT duplicate -> fail");
    }

    // f) REVOKE (delete user)
    check(sendSql("REVOKE ALL PRIVILEGES FROM testuser;").value("success", false), "REVOKE testuser");

    // g) SELECT deleted user -> should be gone or SELECT fails
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser';");
        bool ok = true;
        if (j.value("success", false)) {
            auto rs = j.value("resultSet", nlohmann::json::array());
            ok = rs.size() == 0;  // user should be gone
        }
        check(ok, "SELECT after REVOKE -> user removed");
    }

    // h) REVOKE nonexistent user -> should FAIL
    {
        auto j = sendSql("REVOKE ALL PRIVILEGES FROM nonexistent;");
        check(!j.value("success", false), "REVOKE nonexistent -> fail");
    }

    // i) GRANT without password -> should FAIL
    {
        auto j = sendSql("GRANT ALL PRIVILEGES TO testuser2;");
        check(!j.value("success", false), "GRANT without password -> fail");
    }

    // Report
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nDclSqlTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;

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
        log << "==========\nDclSqlTest\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
    }

    return failed > 0 ? 1 : 0;
}
