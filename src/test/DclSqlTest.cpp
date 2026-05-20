/**
 * @brief DCL SQL 测试 — 验证 GRANT/REVOKE 用户管理 (扩展版)
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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "TestUtils.h"

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

#include "TestUtils.h"

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
    int testId = 0;
    std::vector<int> passedIds, failedIds;

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
        ++testId;
        if (ok) { std::cout << "PASS: " << desc << std::endl; ++passed; passedIds.push_back(testId); }
        else { std::cout << "FAIL: " << desc; if (!extra.empty()) std::cout << " [" << extra << "]"; std::cout << std::endl; ++failed; failedIds.push_back(testId); }
    };

    auto userExists = [](const nlohmann::json &j, const std::string &userId, const std::string &password = "") -> bool {
        if (!j.value("success", false)) return false;
        auto rs = j.value("resultSet", nlohmann::json::array());
        for (auto &row : rs) {
            if (row.is_array() && row.size() >= 2 && row[0].get<std::string>() == userId) {
                if (password.empty() || row[1].get<std::string>() == password) return true;
            }
        }
        return false;
    };

    auto userCount = [](const nlohmann::json &j) -> int {
        if (!j.value("success", false)) return -1;
        auto rs = j.value("resultSet", nlohmann::json::array());
        return static_cast<int>(rs.size());
    };

    // 1) USE DATABASE system
    check(sendSql("USE DATABASE system;").value("success", false), "USE DATABASE system");

    // 2) USE DATABASE system again — should always succeed
    check(sendSql("USE DATABASE system;").value("success", false), "USE DATABASE system (2nd time)");

    // 3) SELECT * FROM user — at least 1 row
    {
        auto j = sendSql("SELECT * FROM user;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() >= 1;
        check(ok, "SELECT * FROM user -> >=1 row");
    }

    // 4) Verify root user exists
    {
        auto j = sendSql("SELECT * FROM user;");
        check(userExists(j, "root"), "root user exists");
    }

    // 5) GRANT (create user)
    check(sendSql("GRANT ALL PRIVILEGES TO testuser IDENTIFIED BY testpass123;").value("success", false), "GRANT create user testuser");

    // 6) Verify root still exists after GRANT
    {
        auto j = sendSql("SELECT * FROM user;");
        check(userExists(j, "root"), "root user still exists after GRANT");
    }

    // 7) SELECT created user
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser';");
        check(userExists(j, "testuser", "testpass123"), "SELECT user where id=testuser -> found");
    }

    // 8) GRANT duplicate — should FAIL
    {
        auto j = sendSql("GRANT ALL PRIVILEGES TO testuser IDENTIFIED BY testpass123;");
        check(!j.value("success", false), "GRANT duplicate -> fail");
    }

    // 9) GRANT user with a different password
    check(sendSql("GRANT ALL PRIVILEGES TO testuser2 IDENTIFIED BY diffpass456;").value("success", false), "GRANT create user testuser2");

    // 10) Verify testuser2 exists
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser2';");
        check(userExists(j, "testuser2", "diffpass456"), "SELECT user where id=testuser2 -> found");
    }

    // 11) GRANT user with special characters in password
    check(sendSql("GRANT ALL PRIVILEGES TO specuser IDENTIFIED BY p@ss!w0rd#;").value("success", false), "GRANT user with special chars in password");

    // 12) Verify specuser exists
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'specuser';");
        check(userExists(j, "specuser"), "SELECT user where id=specuser -> found");
    }

    // 13) GRANT user3
    check(sendSql("GRANT ALL PRIVILEGES TO user3 IDENTIFIED BY pass3;").value("success", false), "GRANT create user3");

    // 14) GRANT user4
    check(sendSql("GRANT ALL PRIVILEGES TO user4 IDENTIFIED BY pass4;").value("success", false), "GRANT create user4");

    // 15) SELECT COUNT(*) — verify total (root + testuser + testuser2 + specuser + user3 + user4 = 6)
    {
        auto j = sendSql("SELECT * FROM user;");
        int cnt = userCount(j);
        check(cnt == 6, "user count after 5 grants -> 6", "count=" + std::to_string(cnt));
    }

    // 16) REVOKE testuser2
    check(sendSql("REVOKE ALL PRIVILEGES FROM testuser2;").value("success", false), "REVOKE testuser2");

    // 17) Verify testuser2 removed
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser2';");
        bool ok = true;
        if (j.value("success", false)) {
            ok = j.value("resultSet", nlohmann::json::array()).size() == 0;
        }
        check(ok, "SELECT after REVOKE testuser2 -> removed");
    }

    // 18) REVOKE testuser
    check(sendSql("REVOKE ALL PRIVILEGES FROM testuser;").value("success", false), "REVOKE testuser");

    // 19) Verify testuser removed
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'testuser';");
        bool ok = true;
        if (j.value("success", false)) {
            ok = j.value("resultSet", nlohmann::json::array()).size() == 0;
        }
        check(ok, "SELECT after REVOKE testuser -> removed");
    }

    // 20) REVOKE user3
    check(sendSql("REVOKE ALL PRIVILEGES FROM user3;").value("success", false), "REVOKE user3");

    // 21) REVOKE user4
    check(sendSql("REVOKE ALL PRIVILEGES FROM user4;").value("success", false), "REVOKE user4");

    // 22) REVOKE specuser
    check(sendSql("REVOKE ALL PRIVILEGES FROM specuser;").value("success", false), "REVOKE specuser");

    // 23) Final — only root remains
    {
        auto j = sendSql("SELECT * FROM user;");
        int cnt = userCount(j);
        bool rootOk = userExists(j, "root");
        check(cnt == 1 && rootOk, "final user table -> only root remains", "count=" + std::to_string(cnt));
    }

    // 24) REVOKE nonexistent user — should FAIL
    {
        auto j = sendSql("REVOKE ALL PRIVILEGES FROM nonexistent;");
        check(!j.value("success", false), "REVOKE nonexistent -> fail");
    }

    // 25) GRANT without password — should FAIL
    {
        auto j = sendSql("GRANT ALL PRIVILEGES TO nopass;");
        check(!j.value("success", false), "GRANT without password -> fail");
    }

    // 26) GRANT without ALL keyword — should FAIL
    {
        auto j = sendSql("GRANT PRIVILEGES TO userbad IDENTIFIED BY pass;");
        check(!j.value("success", false), "GRANT without ALL keyword -> fail");
    }

    // 27) GRANT with wrong syntax — should FAIL
    {
        auto j = sendSql("GRANT ALL PRIVILEGES userwrong IDENTIFIED BY pass;");
        check(!j.value("success", false), "GRANT with wrong syntax -> fail");
    }

    // 28) Re-GRANT user after REVOKE — should succeed
    check(sendSql("GRANT ALL PRIVILEGES TO testuser IDENTIFIED BY newpass789;").value("success", false), "GRANT same name after REVOKE -> success");

    // Cleanup: REVOKE the re-created user
    sendSql("REVOKE ALL PRIVILEGES FROM testuser;");

    // 29) DROP DATABASE system — should FAIL
    {
        auto j = sendSql("DROP DATABASE system;");
        check(!j.value("success", false), "DROP DATABASE system -> fail");
    }

    // Report
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nDclSqlTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;
    writeReport("DclSqlTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
