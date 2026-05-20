/**
 * @brief CRUD SQL 测试 — 通过 SqlPipeline 验证基本增删改查 (扩展版)
 * @author NAPH130
 */
#include "Core.h"
#include "core/SqlPipeline.h"
#include "dbLog/DbLogManager.h"
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
#include <thread>
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
    netCtx.setConnectionId("crud-test");
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

    // DbLog recovery should undo UPDATE and DELETE to reach earlier point in time
    {
        const auto logsBeforeRecover = core.getDbLogManager()->getLogsForDatabase("crud_test_db");
        bool ok = logsBeforeRecover.size() >= 6;
        if (ok) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ok = core.getDbLogManager()->dbRecover("crud_test_db", logsBeforeRecover[4].getTimestamp());
        }
        if (ok) {
            auto rows = sendSql("SELECT * FROM students;");
            auto resultSet = rows.value("resultSet", nlohmann::json::array());
            ok = rows.value("success", false) && resultSet.size() == 3;
        }
        if (ok) {
            auto bob = sendSql("SELECT age FROM students WHERE name = 'Bob';");
            auto rs = bob.value("resultSet", nlohmann::json::array());
            ok = bob.value("success", false)
                 && rs.size() == 1
                 && rs[0].is_array()
                 && rs[0].size() == 1
                 && rs[0][0].get<std::string>() == "22";
        }
        check(ok, "DBLOG recover restores students before UPDATE/DELETE");
    }

    // ========== EXTENDED TEST CASES ==========

    // CREATE TABLE with INT/VARCHAR/FLOAT/TEXT types
    check(sendSql("CREATE TABLE items (id INT, name VARCHAR(100), price FLOAT, description TEXT);").value("success", false),
          "CREATE TABLE items (INT/VARCHAR/FLOAT/TEXT)");

    // INSERT with NULL handling (optional columns omitted -> NULL)
    check(sendSql("INSERT INTO items (id, name) VALUES (1, 'Widget');").value("success", false),
          "INSERT with NULLs (optional cols omitted)");

    // SELECT should still return rows whose trailing columns are NULL
    {
        auto j = sendSql("SELECT * FROM items WHERE id = 1;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() == 4
              && rs[0][0].get<std::string>() == "1"
              && rs[0][1].get<std::string>() == "Widget"
              && rs[0][2].get<std::string>().empty()
              && rs[0][3].get<std::string>().empty();
        check(ok, "SELECT row with trailing NULL columns");
    }

    // INSERT multi-row VALUES
    check(sendSql("INSERT INTO items VALUES (2,'a',1.0,'t1'), (3,'b',2.0,'t2');").value("success", false),
          "INSERT multi-row VALUES (2 rows)");

    // DbLog recovery should restore dropped table with its data
    check(sendSql("CREATE TABLE recovery_items (id INT, name VARCHAR(50));").value("success", false),
          "CREATE TABLE recovery_items");
    check(sendSql("INSERT INTO recovery_items VALUES (1, 'keep');").value("success", false),
          "INSERT recovery_items row");
    const auto logsBeforeDrop = core.getDbLogManager()->getLogsForDatabase("crud_test_db");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(sendSql("DROP TABLE recovery_items;").value("success", false), "DROP TABLE recovery_items");
    {
        bool ok = !logsBeforeDrop.empty()
               && core.getDbLogManager()->dbRecover("crud_test_db", logsBeforeDrop.back().getTimestamp());
        if (ok) {
            auto restored = sendSql("SELECT * FROM recovery_items;");
            auto rs = restored.value("resultSet", nlohmann::json::array());
            ok = restored.value("success", false)
                 && rs.size() == 1
                 && rs[0].is_array()
                 && rs[0].size() == 2
                 && rs[0][0].get<std::string>() == "1"
                 && rs[0][1].get<std::string>() == "keep";
        }
        check(ok, "DBLOG recover restores dropped table and rows");
    }

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

    // ========== DELETE with ARITHMETIC EXPRESSIONS ==========
    // Insert fresh data for arithmetic tests
    check(sendSql("INSERT INTO students VALUES (10, 'ArithA', 20);").value("success", false), "INSERT ArithA for arithmetic test");
    check(sendSql("INSERT INTO students VALUES (11, 'ArithB', 30);").value("success", false), "INSERT ArithB for arithmetic test");
    check(sendSql("INSERT INTO students VALUES (12, 'ArithC', 40);").value("success", false), "INSERT ArithC for arithmetic test");

    // Test left-side arithmetic: age + 10 = 50 → ArithC(40+10=50) should be deleted
    check(sendSql("DELETE FROM students WHERE age + 10 = 50;").value("success", false), "DELETE WHERE age+10=50 (left arithmetic)");
    {
        auto rs = sendSql("SELECT name FROM students WHERE name = 'ArithC';").value("resultSet", nlohmann::json::array());
        check(rs.empty(), "Verify ArithC deleted by left arithmetic expression");
    }

    // Test right-side arithmetic: 40 = age + 10 → ArithB(30+10=40) should be deleted
    check(sendSql("DELETE FROM students WHERE 40 = age + 10;").value("success", false), "DELETE WHERE 40=age+10 (right arithmetic)");
    {
        auto rs = sendSql("SELECT name FROM students WHERE name = 'ArithB';").value("resultSet", nlohmann::json::array());
        check(rs.empty(), "Verify ArithB deleted by right arithmetic expression");
    }

    // Test both-sides arithmetic: age + 5 = age + 5 → always true, deletes ArithA(20)
    check(sendSql("DELETE FROM students WHERE age + 5 = age + 5;").value("success", false), "DELETE WHERE age+5=age+5 (both sides arithmetic)");
    {
        auto rs = sendSql("SELECT name FROM students WHERE name = 'ArithA';").value("resultSet", nlohmann::json::array());
        check(rs.empty(), "Verify ArithA deleted by both-sides arithmetic expression");
    }

    // Verify all arithmetic test rows are gone
    {
        auto rs = sendSql("SELECT id FROM students WHERE name LIKE 'Arith%';").value("resultSet", nlohmann::json::array());
        check(rs.empty(), "All arithmetic test rows deleted");
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
    writeReport("CrudSqlTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
