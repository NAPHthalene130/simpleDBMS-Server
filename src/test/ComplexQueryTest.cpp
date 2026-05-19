/**
 * @brief ComplexQuery SQL 测试 — 验证复杂查询通过 SqlPipeline
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
    netCtx.setConnectionId("complex-query-test");
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
    check(sendSql("CREATE DATABASE complex_test_db;").value("success", false), "CREATE DATABASE complex_test_db");
    check(sendSql("USE DATABASE complex_test_db;").value("success", false), "USE DATABASE complex_test_db");
    check(sendSql("CREATE TABLE employees (id INT, name VARCHAR(50), dept VARCHAR(30), salary FLOAT);").value("success", false), "CREATE TABLE employees");
    check(sendSql("INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 75000);").value("success", false), "INSERT 1");
    check(sendSql("INSERT INTO employees VALUES (2, 'Bob', 'Engineering', 82000);").value("success", false), "INSERT 2");
    check(sendSql("INSERT INTO employees VALUES (3, 'Charlie', 'Sales', 55000);").value("success", false), "INSERT 3");
    check(sendSql("INSERT INTO employees VALUES (4, 'Diana', 'Sales', 60000);").value("success", false), "INSERT 4");
    check(sendSql("INSERT INTO employees VALUES (5, 'Eve', 'HR', 50000);").value("success", false), "INSERT 5");

    // SELECT with BETWEEN
    {
        auto j = sendSql("SELECT * FROM employees WHERE salary BETWEEN 55000 AND 80000;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT salary BETWEEN -> 3 rows");
    }

    // SELECT with IN
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept IN ('Engineering', 'HR');");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT dept IN -> 3 rows");
    }

    // SELECT with LIKE
    {
        auto j = sendSql("SELECT * FROM employees WHERE name LIKE 'A%';");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT name LIKE -> 1 row");
    }

    // SELECT ORDER BY DESC
    {
        auto j = sendSql("SELECT * FROM employees ORDER BY salary DESC;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 2 && rs[0][1].get<std::string>() == "Bob";
        check(ok, "SELECT ORDER BY DESC -> Bob first");
    }

    // SELECT ORDER BY ASC LIMIT
    {
        auto j = sendSql("SELECT salary FROM employees ORDER BY salary ASC LIMIT 3;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 3 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "50000";
        check(ok, "SELECT ORDER BY ASC LIMIT 3 -> first=50000");
    }

    // SELECT GROUP BY COUNT
    {
        auto j = sendSql("SELECT dept, COUNT(*) FROM employees GROUP BY dept;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT GROUP BY COUNT -> 3 groups");
    }

    // SELECT GROUP BY HAVING
    {
        auto j = sendSql("SELECT dept, AVG(salary) FROM employees GROUP BY dept HAVING AVG(salary) > 50000;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool hasEng = false;
        for (auto &row : rs) {
            if (row.is_array() && !row.empty() && row[0].get<std::string>() == "Engineering") hasEng = true;
        }
        check(ok && hasEng, "SELECT GROUP BY HAVING -> has Engineering");
    }

    // UPDATE + SELECT
    check(sendSql("UPDATE employees SET salary = 76000 WHERE name = 'Alice';").value("success", false), "UPDATE Alice=76000");
    {
        auto j = sendSql("SELECT salary FROM employees WHERE name = 'Alice';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "76000";
        check(ok, "SELECT after UPDATE -> 76000");
    }

    // DELETE + SELECT COUNT
    check(sendSql("DELETE FROM employees WHERE id = 5;").value("success", false), "DELETE id=5");
    {
        auto j = sendSql("SELECT COUNT(*) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "4";
        check(ok, "SELECT COUNT after DELETE -> 4");
    }

    // Cleanup
    check(sendSql("DROP DATABASE complex_test_db;").value("success", false), "DROP DATABASE teardown");

    // Report
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nComplexQueryTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;

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
        log << "==========\nComplexQueryTest\n" << ts.str() << "\n"
            << passed << "/" << total << " " << pct << "%\n";
    }

    return failed > 0 ? 1 : 0;
}
