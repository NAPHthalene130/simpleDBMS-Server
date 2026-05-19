/**
 * @brief CRUD SQL 测试 — 通过 SqlPipeline 验证基本增删改查
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

    // Setup
    check(sendSql("CREATE DATABASE crud_test_db;").value("success", false), "CREATE DATABASE crud_test_db");
    check(sendSql("USE DATABASE crud_test_db;").value("success", false), "USE DATABASE crud_test_db");
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

    // Cleanup
    check(sendSql("DROP TABLE students;").value("success", false), "DROP TABLE students");
    check(sendSql("DROP DATABASE crud_test_db;").value("success", false), "DROP DATABASE crud_test_db");

    // Report
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
