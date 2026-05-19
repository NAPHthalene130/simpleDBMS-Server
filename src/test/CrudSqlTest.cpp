/**
 * @brief CRUD SQL 测试 — 通过 SqlPipeline 验证基本增删改查 (扩展版)
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

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

int main()
{
    Core core;
    SqlPipeline *pipeline = core.getSqlPipeline();

    NetworkExecutionContext netCtx;
    netCtx.setConnectionId("crud-test");
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

    // ========== SETUP ==========
    check(sendSql("CREATE DATABASE crud_test_db;").value("success", false), "CREATE DATABASE crud_test_db");
    check(sendSql("USE DATABASE crud_test_db;").value("success", false), "USE DATABASE crud_test_db");

    // ========== ORIGINAL TEST CASES ==========
    check(sendSql("CREATE TABLE students (id INT, name VARCHAR(50), age INT);").value("success", false), "CREATE TABLE students");
    check(sendSql("INSERT INTO students VALUES (1, 'Alice', 20);").value("success", false), "INSERT Alice");
    check(sendSql("INSERT INTO students VALUES (2, 'Bob', 22);").value("success", false), "INSERT Bob");
    check(sendSql("INSERT INTO students VALUES (3, 'Charlie', 21);").value("success", false), "INSERT Charlie");

    // SELECT * -> 3 rows
    {
        auto j = sendSql("SELECT * FROM students;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT * -> 3 rows");
    }

    // SELECT with WHERE
    {
        auto j = sendSql("SELECT * FROM students WHERE age > 20;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT WHERE age > 20 -> 2 rows");
    }

    // UPDATE
    check(sendSql("UPDATE students SET age = 23 WHERE name = 'Bob';").value("success", false), "UPDATE Bob age=23");

    // SELECT after UPDATE
    {
        auto j = sendSql("SELECT age FROM students WHERE name = 'Bob';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "23";
        check(ok, "SELECT after UPDATE -> age=23");
    }

    // DELETE
    check(sendSql("DELETE FROM students WHERE name = 'Alice';").value("success", false), "DELETE Alice");

    // SELECT after DELETE
    {
        auto j = sendSql("SELECT * FROM students;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT after DELETE -> 2 rows");
    }

    // ========== EXTENDED TEST CASES ==========

    // CREATE TABLE with INT/VARCHAR/FLOAT/TEXT types
    check(sendSql("CREATE TABLE items (id INT, name VARCHAR(100), price FLOAT, description TEXT);").value("success", false),
          "CREATE TABLE items (INT/VARCHAR/FLOAT/TEXT)");

    // INSERT with NULL handling (optional columns omitted -> NULL)
    check(sendSql("INSERT INTO items (id, name) VALUES (1, 'Widget');").value("success", false),
          "INSERT with NULLs (optional cols omitted)");

    // INSERT multi-row VALUES
    check(sendSql("INSERT INTO items VALUES (2,'a',1.0,'t1'), (3,'b',2.0,'t2');").value("success", false),
          "INSERT multi-row VALUES (2 rows)");

    // SELECT with LIMIT
    {
        auto j = sendSql("SELECT * FROM items LIMIT 1;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT with LIMIT 1");
    }

    // SELECT with ORDER BY ASC
    {
        auto j = sendSql("SELECT * FROM items ORDER BY id ASC;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT with ORDER BY ASC");
    }

    // SELECT with specific columns
    {
        auto j = sendSql("SELECT id, name FROM items;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 3 && rs[0].is_array() && rs[0].size() == 2;
        check(ok, "SELECT specific columns (id, name)");
    }

    // UPDATE with multiple SET conditions
    check(sendSql("UPDATE items SET name = 'Updated', price = 99.99 WHERE id = 1;").value("success", false),
          "UPDATE multiple SET (name, price)");

    // DELETE with single condition
    check(sendSql("DELETE FROM items WHERE id = 3;").value("success", false),
          "DELETE with single condition (id=3)");

    // SELECT after UPDATE to verify changes persist
    {
        auto j = sendSql("SELECT name, price FROM items WHERE id = 1;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() >= 2
              && rs[0][0].get<std::string>() == "Updated";
        check(ok, "SELECT after UPDATE verify persist");
    }

    // CREATE TABLE with more columns (boundary case)
    check(sendSql("CREATE TABLE wide_table (a INT, b VARCHAR(10), c INT, d VARCHAR(10), e INT, f VARCHAR(10), g INT, h VARCHAR(10));").value("success", false),
          "CREATE TABLE with 8 columns (boundary)");

    // ========== ERROR CASES ==========

    // ERROR: CREATE TABLE with no columns should fail
    {
        auto j = sendSql("CREATE TABLE bad_table ();");
        check(!j.value("success", false), "ERROR: CREATE TABLE no columns fails");
    }

    // ERROR: DROP non-existent table should fail
    {
        auto j = sendSql("DROP TABLE no_such_table;");
        check(!j.value("success", false), "ERROR: DROP non-existent table fails");
    }

    // ERROR: INSERT into non-existent table should fail
    {
        auto j = sendSql("INSERT INTO ghost VALUES (1);");
        check(!j.value("success", false), "ERROR: INSERT non-existent table fails");
    }

    // ========== DROP TABLE then DROP DATABASE CLEANUP ==========
    check(sendSql("DROP TABLE wide_table;").value("success", false), "DROP TABLE wide_table");
    check(sendSql("DROP TABLE items;").value("success", false), "DROP TABLE items");
    check(sendSql("DROP TABLE students;").value("success", false), "DROP TABLE students");
    check(sendSql("DROP DATABASE crud_test_db;").value("success", false), "DROP DATABASE crud_test_db");

    // ========== REPORT ==========
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nCrudSqlTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;

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
        log << "==========\nCrudSqlTest\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
    }

    return failed > 0 ? 1 : 0;
}
