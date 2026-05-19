/**
 * @brief ClientTest — 模拟完整客户端请求管线
 * @details 涵盖用户登录、数据库操作、业务查询、ALTER 操作到最终的清理全流程。
 * @author NAPH130
 */
#include "Core.h"
#include "core/SqlPipeline.h"
#include "models/network/NetworkTransferData.h"
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

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

int main()
{
    Core core;
    core.start(); // 初始化系统数据库与默认用户 root
    SqlPipeline *pipeline = core.getSqlPipeline();

    int passed = 0;
    int failed = 0;

    // 作者：NAPH130
    NetworkExecutionContext netCtx;
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

    // 作者：NAPH130
    auto check = [&](bool ok, const std::string &desc, const std::string &extra = "") {
        if (ok) { std::cout << "PASS: " << desc << std::endl; ++passed; }
        else { std::cout << "FAIL: " << desc; if (!extra.empty()) std::cout << " [" << extra << "]"; std::cout << std::endl; ++failed; }
    };

    // =========================================================================
    // Phase 1: Client Login Simulation
    // 作者：NAPH130
    // 切换到系统数据库，验证默认 root 用户是否存在
    // =========================================================================
    check(sendSql("USE DATABASE system;").value("success", false), "USE DATABASE system for login");

    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'root';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "root";
        check(ok, "Login: root user exists in system.user");
    }

    // =========================================================================
    // Phase 2: Session Setup
    // 作者：NAPH130
    // 建立客户端会话上下文，创建测试数据库与表并初始化数据
    // =========================================================================
    netCtx.setConnectionId("client-test-001");
    netCtx.setCurrentUser("root");

    check(sendSql("CREATE DATABASE client_test_db;").value("success", false), "CREATE DATABASE client_test_db");
    check(sendSql("USE DATABASE client_test_db;").value("success", false), "USE DATABASE client_test_db");

    check(sendSql("CREATE TABLE accounts (id INT, name VARCHAR(100), balance FLOAT);").value("success", false), "CREATE TABLE accounts");

    check(sendSql("INSERT INTO accounts VALUES (1, 'Alice', 1000.00);").value("success", false), "INSERT Alice (1000.00)");
    check(sendSql("INSERT INTO accounts VALUES (2, 'Bob', 2500.00);").value("success", false), "INSERT Bob (2500.00)");
    check(sendSql("INSERT INTO accounts VALUES (3, 'Charlie', 500.00);").value("success", false), "INSERT Charlie (500.00)");

    {
        auto j = sendSql("SELECT * FROM accounts;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT * FROM accounts -> 3 rows");
    }

    // =========================================================================
    // Phase 3: Business Operations
    // 作者：NAPH130
    // 模拟转账、聚合查询、条件过滤、删除等典型业务操作
    // =========================================================================
    check(sendSql("UPDATE accounts SET balance = 900.00 WHERE id = 1;").value("success", false), "UPDATE Alice balance -> 900.00");
    check(sendSql("UPDATE accounts SET balance = 2600.00 WHERE id = 2;").value("success", false), "UPDATE Bob balance -> 2600.00");

    {
        auto j = sendSql("SELECT balance FROM accounts WHERE id = 1;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "900.00";
        check(ok, "Verify Alice balance = 900.00");
    }

    {
        auto j = sendSql("SELECT * FROM accounts ORDER BY balance DESC;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 2
             && rs[0][1].get<std::string>() == "Bob";
        check(ok, "ORDER BY balance DESC -> first row is Bob");
    }

    {
        auto j = sendSql("SELECT COUNT(*) FROM accounts;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "3";
        check(ok, "SELECT COUNT(*) -> 3");
    }

    check(sendSql("SELECT AVG(balance) FROM accounts;").value("success", false), "SELECT AVG(balance) -> success");

    {
        auto j = sendSql("SELECT name FROM accounts WHERE balance > 1000;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "Bob";
        check(ok, "WHERE balance > 1000 -> only Bob");
    }

    check(sendSql("DELETE FROM accounts WHERE balance < 1000;").value("success", false), "DELETE Charlie (balance < 1000)");

    {
        auto j = sendSql("SELECT * FROM accounts;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT * after DELETE -> 2 rows");
    }

    // =========================================================================
    // Phase 4: ALTER Operations
    // 作者：NAPH130
    // 测试 ADD COLUMN、带新列的 INSERT 以及 DROP COLUMN
    // =========================================================================
    check(sendSql("ALTER TABLE accounts ADD COLUMN type VARCHAR(50) DEFAULT 'savings';").value("success", false), "ALTER TABLE ADD COLUMN type");

    check(sendSql("INSERT INTO accounts VALUES (4, 'Diana', 3000.00, 'checking');").value("success", false), "INSERT Diana with type=checking");

    {
        auto j = sendSql("SELECT type FROM accounts WHERE name = 'Diana';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "checking";
        check(ok, "SELECT type WHERE name=Diana -> checking");
    }

    check(sendSql("ALTER TABLE accounts DROP COLUMN type;").value("success", false), "ALTER TABLE DROP COLUMN type");

    // =========================================================================
    // Phase 5: Cleanup
    // 作者：NAPH130
    // 逐级清理资源：TRUNCATE -> DROP TABLE -> DROP DATABASE
    // =========================================================================
    check(sendSql("TRUNCATE TABLE accounts;").value("success", false), "TRUNCATE TABLE accounts");

    {
        auto j = sendSql("SELECT * FROM accounts;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 0;
        check(ok, "SELECT after TRUNCATE -> 0 rows");
    }

    check(sendSql("DROP TABLE accounts;").value("success", false), "DROP TABLE accounts");
    check(sendSql("DROP DATABASE client_test_db;").value("success", false), "DROP DATABASE client_test_db");

    core.stop();

    // =========================================================================
    // Report
    // 作者：NAPH130
    // 汇总输出并追加写入 report.log
    // =========================================================================
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nClientTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;

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
        log << "==========\nClientTest\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
    }

    return failed > 0 ? 1 : 0;
}
