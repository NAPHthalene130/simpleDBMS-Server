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

#include "TestUtils.h"

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

    // ===== Setup =====
    check(sendSql("CREATE DATABASE complex_test_db;").value("success", false), "CREATE DATABASE complex_test_db");
    check(sendSql("USE DATABASE complex_test_db;").value("success", false), "USE DATABASE complex_test_db");
    check(sendSql("CREATE TABLE employees (id INT, name VARCHAR(50), dept VARCHAR(30), salary FLOAT);").value("success", false), "CREATE TABLE employees");
    check(sendSql("CREATE TABLE bonuses (id INT, employee_id INT, amount FLOAT);").value("success", false), "CREATE TABLE bonuses");

    // ===== Data Insertion (10 rows) =====
    check(sendSql("INSERT INTO employees VALUES (1, 'Alice', 'Engineering', 75000);").value("success", false), "INSERT 1 Alice");
    check(sendSql("INSERT INTO employees VALUES (2, 'Bob', 'Engineering', 82000);").value("success", false), "INSERT 2 Bob");
    check(sendSql("INSERT INTO employees VALUES (3, 'Charlie', 'Sales', 55000);").value("success", false), "INSERT 3 Charlie");
    check(sendSql("INSERT INTO employees VALUES (4, 'Diana', 'Sales', 60000);").value("success", false), "INSERT 4 Diana");
    check(sendSql("INSERT INTO employees VALUES (5, 'Eve', 'HR', 50000);").value("success", false), "INSERT 5 Eve");
    check(sendSql("INSERT INTO employees VALUES (6, 'Frank', 'Engineering', 68000);").value("success", false), "INSERT 6 Frank");
    check(sendSql("INSERT INTO employees VALUES (7, 'Grace', 'HR', 52000);").value("success", false), "INSERT 7 Grace");
    check(sendSql("INSERT INTO employees VALUES (8, 'Henry', 'Marketing', 70000);").value("success", false), "INSERT 8 Henry");
    check(sendSql("INSERT INTO employees VALUES (9, 'Ivy', NULL, 45000);").value("success", false), "INSERT 9 Ivy (NULL dept)");
    check(sendSql("INSERT INTO employees VALUES (10, 'Aaron', 'Sales', 90000);").value("success", false), "INSERT 10 Aaron");
    check(sendSql("INSERT INTO bonuses VALUES (1, 1, 5000);").value("success", false), "INSERT bonus for Alice");
    check(sendSql("INSERT INTO bonuses VALUES (2, 2, 8000);").value("success", false), "INSERT bonus for Bob");
    check(sendSql("INSERT INTO bonuses VALUES (3, 4, 2000);").value("success", false), "INSERT bonus for Diana");

    // ===== WHERE Clause Tests =====

    // SELECT with BETWEEN
    {
        auto j = sendSql("SELECT * FROM employees WHERE salary BETWEEN 55000 AND 80000;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 5;
        check(ok, "SELECT salary BETWEEN -> 5 rows");
    }

    // SELECT with IN
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept IN ('Engineering', 'HR');");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 5;
        check(ok, "SELECT dept IN -> 5 rows");
    }

    // SELECT with LIKE
    {
        auto j = sendSql("SELECT * FROM employees WHERE name LIKE 'A%';");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT name LIKE -> 2 rows (Alice, Aaron)");
    }

    // SELECT with AND
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept = 'Engineering' AND salary > 70000;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT AND -> 2 rows (Alice, Bob)");
    }

    // SELECT with OR
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept = 'Sales' OR salary > 80000;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 4;
        check(ok, "SELECT OR -> 4 rows");
    }

    // SELECT with NOT
    {
        auto j = sendSql("SELECT * FROM employees WHERE NOT dept = 'HR';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() >= 7;
        check(ok, "SELECT NOT -> >=7 rows(excl HR+NULL)");
    }

    // SELECT with IS NULL
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept IS NULL;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT IS NULL -> 1 row (Ivy)");
    }

    // SELECT with NOT LIKE
    {
        auto j = sendSql("SELECT * FROM employees WHERE name NOT LIKE 'A%';");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 8;
        check(ok, "SELECT NOT LIKE -> 8 rows");
    }

    // SELECT with nested AND/OR
    {
        auto j = sendSql("SELECT * FROM employees WHERE (dept = 'Engineering' OR dept = 'Sales') AND salary > 50000;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 6;
        check(ok, "SELECT nested AND/OR -> 6 rows");
    }

    // SELECT with correlated EXISTS
    {
        auto j = sendSql("SELECT * FROM employees WHERE EXISTS (SELECT employee_id FROM bonuses WHERE bonuses.employee_id = employees.id);");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT correlated EXISTS -> 3 rows");
    }

    // SELECT with correlated NOT EXISTS
    {
        auto j = sendSql("SELECT * FROM employees WHERE NOT EXISTS (SELECT employee_id FROM bonuses WHERE bonuses.employee_id = employees.id);");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 7;
        check(ok, "SELECT correlated NOT EXISTS -> 7 rows");
    }

    // ===== ORDER BY Tests =====

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
        ok = ok && rs.size() == 3 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "45000";
        check(ok, "SELECT ORDER BY ASC LIMIT 3 -> first=45000(Ivy)");
    }

    // SELECT with multiple ORDER BY columns
    {
        auto j = sendSql("SELECT * FROM employees ORDER BY dept, salary DESC;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 2 && rs[0][1].get<std::string>() == "Bob";
        check(ok, "SELECT multi ORDER BY -> Bob first(Eng highest)");
    }

    // ===== GROUP BY / HAVING Tests =====

    // SELECT GROUP BY COUNT
    {
        auto j = sendSql("SELECT dept, COUNT(*) FROM employees GROUP BY dept;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 5;
        check(ok, "SELECT GROUP BY COUNT -> 5 groups");
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

    // SELECT GROUP BY + ORDER BY together
    {
        auto j = sendSql("SELECT dept, COUNT(*) FROM employees GROUP BY dept ORDER BY COUNT(*) DESC;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 1 && rs[0][0].get<std::string>() == "Engineering";
        check(ok, "SELECT GROUP BY + ORDER BY -> Engineering first(3)");
    }

    // ===== Aggregate Function Tests =====

    // SELECT MAX, MIN
    {
        auto j = sendSql("SELECT MAX(salary), MIN(salary) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 2
             && rs[0][0].get<std::string>() == "90000"
             && rs[0][1].get<std::string>() == "45000";
        check(ok, "SELECT MAX/MIN -> 90000, 45000");
    }

    // SELECT SUM
    {
        auto j = sendSql("SELECT SUM(salary) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && !rs[0].empty();
        check(ok, "SELECT SUM(salary) -> result present");
    }

    // SELECT COUNT(DISTINCT dept)
    {
        auto j = sendSql("SELECT COUNT(DISTINCT dept) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && !rs.empty() && rs[0].is_array() && !rs[0].empty();
        std::string val = ok ? rs[0][0].get<std::string>() : "";
        ok = ok && (val == "4" || val == "5");
        check(ok, "SELECT COUNT(DISTINCT dept) -> 4 or 5");
    }

    // ===== Edge Case: Empty Result =====

    // SELECT where no matching rows
    {
        auto j = sendSql("SELECT * FROM employees WHERE id = 999;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "SELECT empty result -> success, 0 rows");
    }

    // ===== UPDATE Tests =====

    // UPDATE single row + verify
    check(sendSql("UPDATE employees SET salary = 76000 WHERE name = 'Alice';").value("success", false), "UPDATE Alice=76000");
    {
        auto j = sendSql("SELECT salary FROM employees WHERE name = 'Alice';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "76000";
        check(ok, "SELECT after UPDATE -> 76000");
    }

    // UPDATE multiple rows
    {
        auto j = sendSql("UPDATE employees SET salary = 57000 WHERE dept = 'HR';");
        bool ok = j.value("success", false);
        check(ok, "UPDATE multiple(HR) -> success");
    }

    // UPDATE with correlated EXISTS
    {
        auto j = sendSql("UPDATE employees SET dept = 'Rewarded' WHERE EXISTS (SELECT employee_id FROM bonuses WHERE bonuses.employee_id = employees.id);");
        bool ok = j.value("success", false) && j.value("affectedRows", 0) == 3;
        check(ok, "UPDATE correlated EXISTS -> 3 rows");
    }
    {
        auto j = sendSql("SELECT * FROM employees WHERE dept = 'Rewarded';");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "VERIFY UPDATE EXISTS -> 3 rewarded rows");
    }

    // ===== DELETE Tests =====

    // DELETE single row + verify count
    check(sendSql("DELETE FROM employees WHERE id = 5;").value("success", false), "DELETE id=5(Eve)");

    // DELETE with correlated NOT EXISTS
    {
        auto j = sendSql("DELETE FROM employees WHERE NOT EXISTS (SELECT employee_id FROM bonuses WHERE bonuses.employee_id = employees.id) AND id >= 8;");
        bool ok = j.value("success", false) && j.value("affectedRows", 0) == 3;
        check(ok, "DELETE correlated NOT EXISTS -> 3 rows");
    }
    {
        auto j = sendSql("SELECT * FROM employees WHERE id >= 8;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "VERIFY DELETE EXISTS -> ids >= 8 removed");
    }
    {
        auto j = sendSql("SELECT COUNT(*) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "9";
        check(ok, "SELECT COUNT after DELETE -> 9");
    }

    // DELETE with complex WHERE + verify count
    {
        auto j = sendSql("DELETE FROM employees WHERE dept = 'Marketing' OR salary < 50000;");
        bool ok = j.value("success", false);
        int aff = j.value("affectedRows", 1);
        ok = ok && aff >= 1;
        check(ok, "DELETE complex -> affected >=1");
    }
    {
        auto j = sendSql("SELECT COUNT(*) FROM employees;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "7";
        check(ok, "SELECT COUNT after complex DELETE -> 7");
    }

    // ===== Cleanup =====
    check(sendSql("DROP DATABASE complex_test_db;").value("success", false), "DROP DATABASE teardown");

    // ===== Report =====
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nComplexQueryTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;
    writeReport("ComplexQueryTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
