/**
 * @brief DDL SQL 测试 — 验证 CREATE/DROP/ALTER TABLE/TRUNCATE
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
    SqlPipeline *pipeline = core.getSqlPipeline();

    NetworkExecutionContext netCtx;
    netCtx.setConnectionId("ddl-test");
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

    // a) CREATE DATABASE
    check(sendSql("CREATE DATABASE ddl_test_db;").value("success", false), "CREATE DATABASE ddl_test_db");

    // b) USE DATABASE
    check(sendSql("USE DATABASE ddl_test_db;").value("success", false), "USE DATABASE ddl_test_db");

    // c) CREATE TABLE with TEXT
    check(sendSql("CREATE TABLE items (id INT, name VARCHAR(100), price FLOAT, description TEXT);").value("success", false), "CREATE TABLE items");

    // d) INSERT
    check(sendSql("INSERT INTO items VALUES (1, 'Widget', 9.99, 'A useful widget');").value("success", false), "INSERT Widget");

    // e) SELECT -> 1 row, 4 columns
    {
        auto j = sendSql("SELECT * FROM items;");
        bool ok = j.value("success", false);
        auto cols = j.value("columns", nlohmann::json::array());
        auto rows = j.value("resultSet", nlohmann::json::array());
        check(ok && rows.size() == 1 && cols.size() == 4, "SELECT * -> 1 row, 4 cols");
    }

    // f) ALTER TABLE ADD COLUMN
    check(sendSql("ALTER TABLE items ADD COLUMN category VARCHAR(50);").value("success", false), "ALTER TABLE ADD COLUMN category");

    // g) INSERT with new column
    check(sendSql("INSERT INTO items VALUES (2, 'Gadget', 19.99, 'A cool gadget', 'Electronics');").value("success", false), "INSERT Gadget with category");

    // h) SELECT new column
    {
        auto j = sendSql("SELECT category FROM items WHERE id = 2;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "Electronics";
        check(ok, "SELECT category -> Electronics");
    }

    // i) ALTER TABLE RENAME COLUMN
    check(sendSql("ALTER TABLE items RENAME COLUMN category TO item_type;").value("success", false), "ALTER TABLE RENAME COLUMN");

    // j) INSERT with renamed column
    check(sendSql("INSERT INTO items VALUES (3, 'Thing', 4.99, 'A simple thing', 'Misc');").value("success", false), "INSERT Thing with item_type");

    // k) SELECT renamed column
    {
        auto j = sendSql("SELECT item_type FROM items WHERE id = 3;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "Misc";
        check(ok, "SELECT item_type -> Misc");
    }

    // l) ALTER TABLE DROP COLUMN
    check(sendSql("ALTER TABLE items DROP COLUMN item_type;").value("success", false), "ALTER TABLE DROP COLUMN");

    // m) SELECT after drop column -> 4 columns
    {
        auto j = sendSql("SELECT * FROM items WHERE id = 3;");
        bool ok = j.value("success", false);
        auto cols = j.value("columns", nlohmann::json::array());
        check(ok && cols.size() == 4, "SELECT after DROP COLUMN -> 4 cols");
    }

    // n) TRUNCATE
    check(sendSql("TRUNCATE TABLE items;").value("success", false), "TRUNCATE TABLE items");

    // o) SELECT after TRUNCATE -> 0 rows
    {
        auto j = sendSql("SELECT * FROM items;");
        check(j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 0, "SELECT after TRUNCATE -> 0 rows");
    }

    // p) DROP TABLE
    check(sendSql("DROP TABLE items;").value("success", false), "DROP TABLE items");

    // q) DROP DATABASE
    check(sendSql("DROP DATABASE ddl_test_db;").value("success", false), "DROP DATABASE ddl_test_db");

    // Report
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nDdlSqlTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;

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
        log << "==========\nDdlSqlTest\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
    }

    return failed > 0 ? 1 : 0;
}
