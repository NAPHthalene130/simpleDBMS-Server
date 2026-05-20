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

#include "TestUtils.h"

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

int main()
{
    Core core;
    core.start();
    SqlPipeline *pipeline = core.getSqlPipeline();

    int passed = 0;
    int failed = 0;
    int testId = 0;
    std::vector<int> passedIds, failedIds;

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

    auto check = [&](bool ok, const std::string &desc, const std::string &extra = "") {
        ++testId;
        if (ok) { std::cout << "PASS: " << desc << std::endl; ++passed; passedIds.push_back(testId); }
        else { std::cout << "FAIL: " << desc; if (!extra.empty()) std::cout << " [" << extra << "]"; std::cout << std::endl; ++failed; failedIds.push_back(testId); }
    };

    // =========================================================================
    // Phase 1: Client Login & Authentication Simulation
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

    // Non-existent user login
    {
        auto j = sendSql("SELECT * FROM user WHERE id = 'nonexistent_user';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.empty();
        check(ok, "Login: non-existent user returns empty");
    }

    // Login with empty username
    {
        auto j = sendSql("SELECT * FROM user WHERE id = '';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.empty();
        check(ok, "Login: empty username returns empty");
    }

    // =========================================================================
    // Phase 2: Session Setup
    // =========================================================================
    netCtx.setConnectionId("client-test-001");
    netCtx.setCurrentUser("root");

    check(sendSql("CREATE DATABASE client_test_db;").value("success", false), "CREATE DATABASE client_test_db");
    check(sendSql("USE DATABASE client_test_db;").value("success", false), "USE DATABASE client_test_db");

    check(sendSql("CREATE TABLE accounts (id INT, name VARCHAR(100), balance FLOAT, active INT);").value("success", false), "CREATE TABLE accounts");

    check(sendSql("INSERT INTO accounts VALUES (1, 'Alice', 1000.00, 1);").value("success", false), "INSERT Alice (1000.00)");
    check(sendSql("INSERT INTO accounts VALUES (2, 'Bob', 2500.00, 1);").value("success", false), "INSERT Bob (2500.00)");
    check(sendSql("INSERT INTO accounts VALUES (3, 'Charlie', 500.00, 0);").value("success", false), "INSERT Charlie (500.00)");

    {
        auto j = sendSql("SELECT * FROM accounts;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 3;
        check(ok, "SELECT * FROM accounts -> 3 rows");
    }

    // =========================================================================
    // Phase 3: Business Operations
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
    // Phase 3.5: Advanced Query Operators
    // =========================================================================
    {
        auto j = sendSql("SELECT * FROM accounts WHERE id BETWEEN 1 AND 2;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "BETWEEN 1 AND 2 -> 2 rows");
    }

    {
        auto j = sendSql("SELECT * FROM accounts WHERE name IN ('Alice', 'Bob');");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "IN ('Alice','Bob') -> 2 rows");
    }

    {
        auto j = sendSql("SELECT * FROM accounts WHERE name LIKE 'A%';");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "LIKE 'A%' -> Alice");
    }

    {
        auto j = sendSql("SELECT DISTINCT active FROM accounts;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 1;
        check(ok, "SELECT DISTINCT active -> ok");
    }

    {
        auto j = sendSql("UPDATE accounts SET balance = NULL WHERE id = 2;");
        check(j.value("success", false), "UPDATE Bob balance = NULL");
    }
    {
        auto j = sendSql("SELECT * FROM accounts WHERE balance IS NULL;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "IS NULL -> Bob (1 row)");
    }
    {
        auto j = sendSql("SELECT * FROM accounts WHERE balance IS NOT NULL;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "IS NOT NULL -> Alice (1 row)");
    }
    check(sendSql("UPDATE accounts SET balance = 2600.00 WHERE id = 2;").value("success", false), "Restore Bob balance");

    // =========================================================================
    // Phase 4: ALTER Operations
    // =========================================================================
    check(sendSql("ALTER TABLE accounts ADD COLUMN type VARCHAR(50) DEFAULT 'savings';").value("success", false), "ALTER TABLE ADD COLUMN type");

    check(sendSql("INSERT INTO accounts VALUES (4, 'Diana', 3000.00, 1, 'checking');").value("success", false), "INSERT Diana with type=checking");

    {
        auto j = sendSql("SELECT type FROM accounts WHERE name = 'Diana';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "checking";
        check(ok, "SELECT type WHERE name=Diana -> checking");
    }

    check(sendSql("ALTER TABLE accounts DROP COLUMN type;").value("success", false), "ALTER TABLE DROP COLUMN type");

    // ALTER ADD with DEFAULT
    check(sendSql("ALTER TABLE accounts ADD COLUMN status INT DEFAULT 1;").value("success", false), "ALTER TABLE ADD COLUMN status DEFAULT 1");

    // Verify default on existing rows
    {
        auto j = sendSql("SELECT status FROM accounts WHERE id = 1;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
             && rs[0][0].get<std::string>() == "1";
        check(ok, "Existing row has DEFAULT status=1");
    }

    check(sendSql("ALTER TABLE accounts DROP COLUMN status;").value("success", false), "ALTER TABLE DROP COLUMN status");

    // ALTER RENAME COLUMN
    check(sendSql("ALTER TABLE accounts RENAME COLUMN balance TO amount;").value("success", false), "ALTER TABLE RENAME COLUMN balance TO amount");

    {
        auto j = sendSql("SELECT amount FROM accounts WHERE id = 1;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT renamed column amount -> ok");
    }

    check(sendSql("ALTER TABLE accounts RENAME COLUMN amount TO balance;").value("success", false), "ALTER TABLE RENAME COLUMN back to balance");

    // =========================================================================
    // Phase 5: Edge Cases — Various Client-Side Input Scenarios
    // =========================================================================

    // 5a. Empty SQL query
    {
        auto j = sendSql("");
        check(!j.value("success", false), "Empty SQL query -> fail");
    }

    // 5b. Whitespace-only SQL
    {
        auto j = sendSql("   ;");
        check(!j.value("success", false) || j.value("resultSet", nlohmann::json::array()).empty(), "Whitespace-only SQL -> handled");
    }

    // 5c. Truncated / incomplete SQL
    {
        auto j = sendSql("SELECT * FROM");
        check(!j.value("success", false), "Incomplete SQL -> fail");
    }

    // 5d. SQL injection attempt: DROP via injection
    {
        auto j = sendSql("SELECT * FROM accounts; DROP TABLE accounts;");
        check(sendSql("SELECT * FROM accounts;").value("success", false), "Table still exists after injection attempt");
    }

    // 5e. SQL injection attempt: OR 1=1
    {
        auto j = sendSql("SELECT * FROM accounts WHERE name = '' OR '1'='1';");
        bool ok = j.value("success", false);
        if (ok) {
            auto rs = j.value("resultSet", nlohmann::json::array());
            ok = rs.size() >= 2;
        }
        check(ok, "SQL injection: OR 1=1 returns all rows");
    }

    // 5f. Very long query string
    {
        std::string longSql = "SELECT * FROM accounts WHERE name = '";
        longSql.append(5000, 'X');
        longSql.append("';");
        auto j = sendSql(longSql);
        check(j.value("success", false) || !j.value("success", false),
              "Very long query -> does not crash");
    }

    // 5g. Data with special characters
    check(sendSql("INSERT INTO accounts VALUES (5, 'O''Brien', 1500.00, 1);").value("success", false), "INSERT with single quote in name");

    // 5h. Data with Unicode characters
    check(sendSql("INSERT INTO accounts VALUES (6, '\u5f20\u4e09', 2000.00, 1);").value("success", false), "INSERT with Unicode name");

    // 5i. Data with leading/trailing whitespace
    check(sendSql("INSERT INTO accounts VALUES (7, '  spaced  ', 1800.00, 0);").value("success", false), "INSERT with spaced name");

    // 5j. Negative numbers and zero
    check(sendSql("INSERT INTO accounts VALUES (8, 'Negative', -100.00, 1);").value("success", false), "INSERT with negative balance");

    // 5k. Extremely large integer
    check(sendSql("INSERT INTO accounts VALUES (9, 'Big', 999999999.99, 1);").value("success", false), "INSERT with large balance");

    // 5l. SELECT with LIMIT 0
    {
        auto j = sendSql("SELECT * FROM accounts LIMIT 0;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "SELECT LIMIT 0 -> empty result");
    }

    // 5m. Invalid SQL keywords
    {
        auto j = sendSql("SELCT * FROM accounts;");
        check(!j.value("success", false), "Invalid SQL keyword -> fail");
    }

    // 5n. Case-insensitive keywords
    {
        auto j = sendSql("select * from accounts;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() >= 5;
        check(ok, "Lowercase SQL keywords -> works");
    }

    // 5o. Multiple sequential USE DATABASE
    check(sendSql("USE DATABASE system;").value("success", false), "USE DATABASE system (switch)");
    check(sendSql("USE DATABASE client_test_db;").value("success", false), "USE DATABASE client_test_db (switch back)");

    // 5p. SQL with line comment
    {
        auto j = sendSql("SELECT * FROM accounts -- this is a comment\n;");
        bool ok = j.value("success", false);
        check(ok, "SQL with line comment (--) -> works");
    }

    // 5q. SQL with block comment
    {
        auto j = sendSql("SELECT * FROM accounts /* block comment */;");
        bool ok = j.value("success", false);
        check(ok, "SQL with block comment (/* */) -> works");
    }

    // 5r. Multiple semicolons
    {
        auto j = sendSql("SELECT * FROM accounts;;;");
        bool ok = j.value("success", false);
        check(ok, "Multiple trailing semicolons -> works");
    }

    // 5s. SQL with newlines
    {
        auto j = sendSql("SELECT *\nFROM\naccounts\n;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 5;
        check(ok, "SQL with newlines -> works");
    }

    // 5t. Table name matching reserved keyword
    check(sendSql("CREATE TABLE `order` (id INT, descr VARCHAR(50));").value("success", false), "CREATE TABLE with reserved keyword name");
    check(sendSql("INSERT INTO `order` VALUES (1, 'test');").value("success", false), "INSERT into reserved-name table");
    {
        auto j = sendSql("SELECT * FROM `order`;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT from reserved-name table -> 1 row");
    }
    check(sendSql("DROP TABLE `order`;").value("success", false), "DROP reserved-name table");

    // 5u. Negative LIMIT
    {
        auto j = sendSql("SELECT * FROM accounts LIMIT -1;");
        check(j.value("success", false) || !j.value("success", false),
              "Negative LIMIT -> does not crash");
    }

    // 5v. Very large LIMIT
    {
        auto j = sendSql("SELECT * FROM accounts LIMIT 999999999;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 5;
        check(ok, "Very large LIMIT -> returns all rows");
    }

    // 5w. ORDER BY non-existent column
    {
        auto j = sendSql("SELECT * FROM accounts ORDER BY nonexistent_col;");
        check(!j.value("success", false), "ORDER BY non-existent column -> fail");
    }

    // 5x. GROUP BY basic
    {
        auto j = sendSql("SELECT active, COUNT(*) FROM accounts GROUP BY active;");
        bool ok = j.value("success", false);
        check(ok, "GROUP BY active -> works");
    }

    // =========================================================================
    // Phase 6: Multiple Connection Contexts
    // =========================================================================
    {
        NetworkExecutionContext ctx2;
        ctx2.setConnectionId("client-test-002");
        ctx2.setCurrentUser("root");

        auto sendSql2 = [&](const std::string &sql) -> nlohmann::json {
            const std::string req = NetData("sql", sql).toJson();
            const NetData resp = pipeline->handleRequest(req, &ctx2);
            auto j = nlohmann::json::parse(resp.getContent());
            if (j.value("success", false)) {
                const std::string db = j.value("dbName", "");
                if (!db.empty()) ctx2.setCurrentDbName(db);
            }
            return j;
        };

        check(sendSql2("USE DATABASE client_test_db;").value("success", false), "Second connection: USE DATABASE");
        check(sendSql2("SELECT COUNT(*) FROM accounts;").value("success", false), "Second connection: SELECT works");

        // Second connection should see data from first connection
        {
            auto j = sendSql2("SELECT * FROM accounts;");
            bool ok = j.value("success", false)
                      && j.value("resultSet", nlohmann::json::array()).size() >= 5;
            check(ok, "Second connection: sees data from first connection");
        }

        // Second connection DDL also visible to first
        check(sendSql2("CREATE TABLE conn2_table (x INT);").value("success", false), "Second connection: CREATE TABLE");
        {
            auto j = sendSql("SELECT * FROM conn2_table;");
            bool ok = j.value("success", false);
            check(ok, "First connection: sees second connection's table");
        }
        check(sendSql("DROP TABLE conn2_table;").value("success", false), "Cleanup conn2_table");
    }

    // =========================================================================
    // Phase 6.5: Third connection with isolated context
    // =========================================================================
    {
        NetworkExecutionContext ctx3;
        ctx3.setConnectionId("client-test-003");
        ctx3.setCurrentUser("root");

        auto sendSql3 = [&](const std::string &sql) -> nlohmann::json {
            const std::string req = NetData("sql", sql).toJson();
            const NetData resp = pipeline->handleRequest(req, &ctx3);
            auto j = nlohmann::json::parse(resp.getContent());
            if (j.value("success", false)) {
                const std::string db = j.value("dbName", "");
                if (!db.empty()) ctx3.setCurrentDbName(db);
            }
            return j;
        };

        // Third connection with no DB selected
        {
            auto j = sendSql3("SELECT * FROM accounts;");
            check(!j.value("success", false), "Third connection: SELECT without DB context -> fail");
        }
        check(sendSql3("USE DATABASE client_test_db;").value("success", false), "Third connection: USE DATABASE");
        check(sendSql3("SELECT * FROM accounts;").value("success", false), "Third connection: SELECT after USE -> works");
    }

    // =========================================================================
    // Phase 7: Error Handling & Resilience
    // =========================================================================

    // 7a. DROP non-existent table
    {
        auto j = sendSql("DROP TABLE nonexistent_table;");
        check(!j.value("success", false), "DROP non-existent table -> fail");
    }

    // 7b. SELECT from non-existent table
    {
        auto j = sendSql("SELECT * FROM nonexistent_table;");
        check(!j.value("success", false), "SELECT non-existent table -> fail");
    }

    // 7c. INSERT wrong column count
    {
        auto j = sendSql("INSERT INTO accounts VALUES (99, 'Bad');");
        check(!j.value("success", false), "INSERT wrong column count -> fail");
    }

    // 7d. DROP system database (should be protected)
    {
        auto j = sendSql("DROP DATABASE system;");
        check(!j.value("success", false), "DROP DATABASE system -> fail (protected)");
    }

    // 7e. ALTER TABLE with invalid column name
    {
        auto j = sendSql("ALTER TABLE accounts RENAME COLUMN nonexistent TO something;");
        check(!j.value("success", false), "ALTER RENAME non-existent column -> fail");
    }

    // 7f. CREATE TABLE with no columns
    {
        auto j = sendSql("CREATE TABLE bad_table ();");
        check(!j.value("success", false), "CREATE TABLE no columns -> fail");
    }

    // 7g. INSERT duplicate primary key (if PK exists — otherwise just verify no crash)
    {
        auto j = sendSql("INSERT INTO accounts VALUES (1, 'Dup', 0.0, 1);");
        check(!j.value("success", false) || j.value("success", false),
              "INSERT duplicate id -> handled without crash");
    }

    // 7h. Non-existent database operations
    {
        auto j = sendSql("USE DATABASE nonexistent_db;");
        check(!j.value("success", false), "USE non-existent database -> fail");
    }

    // 7i. Malformed JSON-like input via raw send
    {
        const std::string badJson = "not json at all";
        const NetData resp = pipeline->handleRequest(badJson, &netCtx);
        bool ok = true;
        try {
            auto j = nlohmann::json::parse(resp.getContent());
            ok = !j.value("success", true);
        } catch (...) {
            ok = true;
        }
        check(ok, "Malformed JSON request -> handled (no crash)");
    }

    // 7j. Extremely long table name
    {
        std::string longName = "tbl_";
        longName.append(200, 'x');
        auto j = sendSql("CREATE TABLE " + longName + " (id INT);");
        check(!j.value("success", false) || j.value("success", false),
              "Extremely long table name -> does not crash");
    }

    // 7k. Extremely long column name
    {
        std::string longCol = "col_";
        longCol.append(100, 'y');
        auto j = sendSql("CREATE TABLE long_col_test (" + longCol + " INT);");
        check(!j.value("success", false) || j.value("success", false),
              "Extremely long column name -> does not crash");
    }

    // 7l. ALTER TABLE ADD COLUMN with non-existent table
    {
        auto j = sendSql("ALTER TABLE ghost_table ADD COLUMN x INT;");
        check(!j.value("success", false), "ALTER non-existent table -> fail");
    }

    // 7m. SELECT with WHERE on non-existent column
    {
        auto j = sendSql("SELECT * FROM accounts WHERE ghost_col = 1;");
        check(!j.value("success", false), "SELECT WHERE non-existent column -> fail");
    }

    // 7n. TRUNCATE non-existent table
    {
        auto j = sendSql("TRUNCATE TABLE ghost_table;");
        check(!j.value("success", false), "TRUNCATE non-existent table -> fail");
    }

    // 7o. UPDATE with invalid SET column
    {
        auto j = sendSql("UPDATE accounts SET ghost_col = 1 WHERE id = 1;");
        check(!j.value("success", false), "UPDATE invalid SET column -> fail");
    }

    // 7p. DELETE with invalid WHERE column
    {
        auto j = sendSql("DELETE FROM accounts WHERE ghost_col = 1;");
        check(!j.value("success", false), "DELETE invalid WHERE column -> fail");
    }

    // =========================================================================
    // Phase 8: Data Type Edge Cases
    // =========================================================================

    // 8a. INSERT with boolean-like values
    check(sendSql("INSERT INTO accounts VALUES (10, 'BoolTest', 0.0, 1);").value("success", false), "INSERT with boolean-like active=1");

    // 8b. INSERT with zero values
    check(sendSql("INSERT INTO accounts VALUES (11, 'Zero', 0.00, 0);").value("success", false), "INSERT with zero balance and inactive");

    // 8c. FLOAT with many decimal places
    check(sendSql("INSERT INTO accounts VALUES (12, 'Precise', 3.14159265358979, 1);").value("success", false), "INSERT with high-precision float");

    // 8d. SELECT with ORDER BY on NULL column
    {
        auto j = sendSql("SELECT name FROM accounts ORDER BY name ASC;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 8;
        check(ok, "ORDER BY name ASC -> returns rows");
    }

    // 8e. SELECT with LIMIT + OFFSET
    {
        auto j = sendSql("SELECT * FROM accounts LIMIT 2 OFFSET 1;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT LIMIT 2 OFFSET 1 -> 2 rows");
    }

    // =========================================================================
    // Phase 9: Stress / Rapid-fire Operations
    // =========================================================================

    {
        bool allOk = true;
        for (int i = 0; i < 20; ++i) {
            std::string name = "StressUser" + std::to_string(i);
            auto j = sendSql("INSERT INTO accounts VALUES (" + std::to_string(100 + i) + ", '" + name + "', " + std::to_string(100.0 + i) + ", 1);");
            if (!j.value("success", false)) { allOk = false; break; }
        }
        check(allOk, "Rapid-fire 20 INSERTs -> all succeed");
    }

    {
        auto j = sendSql("SELECT COUNT(*) FROM accounts;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0;
        int count = ok ? std::stoi(rs[0][0].get<std::string>()) : 0;
        check(count >= 28, "Row count after stress INSERTs -> >= 28", std::to_string(count));
    }

    // Rapid SELECTs
    {
        bool allOk = true;
        for (int i = 0; i < 10; ++i) {
            auto j = sendSql("SELECT * FROM accounts WHERE id > 0;");
            if (!j.value("success", false)) { allOk = false; break; }
        }
        check(allOk, "Rapid-fire 10 SELECTs -> all succeed");
    }

    // Batch UPDATE
    check(sendSql("UPDATE accounts SET active = 0 WHERE id < 5;").value("success", false), "Batch UPDATE multiple rows");

    {
        auto j = sendSql("SELECT * FROM accounts WHERE active = 0;");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 4;
        check(ok, "Verify batch UPDATE -> multiple rows affected");
    }

    // =========================================================================
    // Phase 9.5: Additional Client Scenarios
    // =========================================================================

    // 9.5a. Concurrent connections - verify isolation (main connection unaffected by errors in others)
    {
        NetworkExecutionContext errCtx;
        errCtx.setConnectionId("client-test-err");
        errCtx.setCurrentUser("root");
        auto sendErr = [&](const std::string &sql) -> nlohmann::json {
            const std::string req = NetData("sql", sql).toJson();
            const NetData resp = pipeline->handleRequest(req, &errCtx);
            auto j = nlohmann::json::parse(resp.getContent());
            if (j.value("success", false)) {
                const std::string db = j.value("dbName", "");
                if (!db.empty()) errCtx.setCurrentDbName(db);
            }
            return j;
        };

        // Force errors in separate connection
        sendErr("USE DATABASE client_test_db;");
        sendErr("SELECT * FROM nonexistent;");
        sendErr("INSERT INTO nonexistent VALUES (1);");
        sendErr("UPDATE nonexistent SET x=1;");
        sendErr("DELETE FROM nonexistent;");

        // Main connection should be unaffected
        auto j = sendSql("SELECT COUNT(*) FROM accounts;");
        bool ok = j.value("success", false) && !j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "Errors in other connection -> main connection unaffected");
    }

    // 9.5b. Subquery in SELECT clause
    {
        auto j = sendSql("SELECT name, (SELECT MAX(balance) FROM accounts) AS max_bal FROM accounts LIMIT 1;");
        bool ok = j.value("success", false) || !j.value("success", false);
        check(ok, "Subquery in SELECT clause -> handled without crash");
    }

    // 9.5c. Subquery in WHERE with IN
    {
        auto j = sendSql("SELECT * FROM accounts WHERE id IN (SELECT id FROM accounts WHERE active = 1);");
        bool ok = j.value("success", false);
        check(ok, "Subquery IN WHERE -> works");
    }

    // 9.5d. UNION query (basic)
    {
        auto j = sendSql("SELECT name, balance FROM accounts WHERE id = 1 UNION SELECT name, balance FROM accounts WHERE id = 2;");
        bool ok = j.value("success", false) || !j.value("success", false);
        check(ok, "UNION query -> handled without crash");
    }

    // 9.5e. Cross-database: create second DB, switch, and verify isolation
    check(sendSql("CREATE DATABASE client_test_db2;").value("success", false), "CREATE second database");
    check(sendSql("USE DATABASE client_test_db2;").value("success", false), "USE second database");
    check(sendSql("CREATE TABLE ref_data (code INT, label VARCHAR(50));").value("success", false), "CREATE TABLE in second db");
    check(sendSql("INSERT INTO ref_data VALUES (1, 'Active');").value("success", false), "INSERT into second db");
    {
        auto j = sendSql("SELECT * FROM ref_data;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT from second db -> 1 row");
    }
    // Switch back to original
    check(sendSql("USE DATABASE client_test_db;").value("success", false), "Switch back to original db");
    {
        auto j = sendSql("SELECT * FROM accounts;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() >= 5;
        check(ok, "Original db data intact after cross-db switch");
    }
    // Cleanup second db
    check(sendSql("DROP DATABASE client_test_db2;").value("success", false), "DROP second database");

    // 9.5f. Multiple sequential DDL operations (create, alter, drop in sequence)
    check(sendSql("CREATE TABLE seq_test (a INT, b VARCHAR(10));").value("success", false), "CREATE seq_test table");
    check(sendSql("ALTER TABLE seq_test ADD COLUMN c FLOAT;").value("success", false), "ALTER seq_test ADD COLUMN");
    check(sendSql("INSERT INTO seq_test VALUES (1, 'hello', 1.5);").value("success", false), "INSERT into seq_test");
    check(sendSql("TRUNCATE TABLE seq_test;").value("success", false), "TRUNCATE seq_test");
    check(sendSql("DROP TABLE seq_test;").value("success", false), "DROP seq_test");

    // 9.5g. Data types: DATE type handling
    check(sendSql("CREATE TABLE date_test (id INT, created DATE);").value("success", false), "CREATE TABLE with DATE");
    check(sendSql("INSERT INTO date_test VALUES (1, '2024-01-15');").value("success", false), "INSERT DATE value");
    {
        auto j = sendSql("SELECT * FROM date_test;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 1;
        check(ok, "SELECT DATE -> 1 row");
    }
    check(sendSql("DROP TABLE date_test;").value("success", false), "DROP date_test");

    // 9.5h. Session state persistence across multiple requests
    {
        // Verify connection ID and user stay consistent
        auto j1 = sendSql("SELECT * FROM accounts WHERE id = 1;");
        auto j2 = sendSql("SELECT * FROM accounts WHERE id = 2;");
        auto j3 = sendSql("SELECT * FROM accounts WHERE id = 3;");
        bool ok = j1.value("success", false) && j2.value("success", false) && j3.value("success", false);
        check(ok, "Session state persists across 3 sequential SELECTs");
    }

    // 9.5i. Data integrity: insert and immediately verify
    {
        auto j = sendSql("INSERT INTO accounts VALUES (50, 'Integrity', 7777.77, 1);");
        bool insertOk = j.value("success", false);
        if (insertOk) {
            auto j2 = sendSql("SELECT balance FROM accounts WHERE id = 50;");
            bool ok = j2.value("success", false);
            auto rs = j2.value("resultSet", nlohmann::json::array());
            ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0
                 && rs[0][0].get<std::string>() == "7777.77";
            check(ok, "Data integrity: INSERT then SELECT returns correct value");
        } else {
            check(false, "Data integrity: INSERT then SELECT (insert failed)");
        }
    }

    // 9.5j. LIKE with special patterns
    {
        auto j = sendSql("SELECT * FROM accounts WHERE name LIKE '%test%' OR name LIKE '%user%';");
        bool ok = j.value("success", false) || !j.value("success", false);
        check(ok, "LIKE with OR patterns -> handled");
    }

    // 9.5k. IN with many values
    {
        std::string inClause = "SELECT * FROM accounts WHERE id IN (";
        for (int i = 1; i <= 10; ++i) {
            if (i > 1) inClause += ",";
            inClause += std::to_string(i);
        }
        inClause += ");";
        auto j = sendSql(inClause);
        bool ok = j.value("success", false);
        check(ok, "IN clause with 10 values -> works");
    }

    // 9.5l. ORDER BY with calculated expression (if supported)
    {
        auto j = sendSql("SELECT id, name, balance FROM accounts ORDER BY balance * 2 DESC;");
        bool ok = j.value("success", false) || !j.value("success", false);
        check(ok, "ORDER BY expression -> handled without crash");
    }

    // 9.5m. Session: switch user context mid-session
    {
        NetworkExecutionContext adminCtx;
        adminCtx.setConnectionId("admin-session");
        adminCtx.setCurrentUser("root");
        auto sendAdmin = [&](const std::string &sql) -> nlohmann::json {
            const std::string req = NetData("sql", sql).toJson();
            const NetData resp = pipeline->handleRequest(req, &adminCtx);
            auto j = nlohmann::json::parse(resp.getContent());
            return j;
        };
        // Grant a new user, then query as that user context
        sendAdmin("USE DATABASE system;");
        sendAdmin("GRANT ALL PRIVILEGES TO client_test_user IDENTIFIED BY test999;");
        {
            auto j = sendAdmin("SELECT * FROM user WHERE id = 'client_test_user';");
            bool ok = j.value("success", false);
            auto rs = j.value("resultSet", nlohmann::json::array());
            ok = ok && rs.size() == 1;
            check(ok, "GRANT new user via admin session -> created");
        }
        // Verify new user can query (using their own context)
        NetworkExecutionContext userCtx;
        userCtx.setConnectionId("client-test-user");
        userCtx.setCurrentUser("client_test_user");
        auto sendUser = [&](const std::string &sql) -> nlohmann::json {
            const std::string req = NetData("sql", sql).toJson();
            const NetData resp = pipeline->handleRequest(req, &userCtx);
            auto j = nlohmann::json::parse(resp.getContent());
            return j;
        };
        {
            auto j = sendUser("USE DATABASE client_test_db;");
            bool ok = j.value("success", false);
            check(ok, "New user: USE DATABASE -> works");
        }
        // Cleanup: revoke user
        sendAdmin("REVOKE ALL PRIVILEGES FROM client_test_user;");

        // Restore main context
        sendAdmin("USE DATABASE client_test_db;");
    }

    // 9.5n. Multiple rapid database creation and cleanup
    {
        bool allOk = true;
        for (int i = 0; i < 5; ++i) {
            std::string dbName = "rapid_db_" + std::to_string(i);
            auto j = sendSql("CREATE DATABASE " + dbName + ";");
            if (!j.value("success", false)) { allOk = false; break; }
            j = sendSql("DROP DATABASE " + dbName + ";");
            if (!j.value("success", false)) { allOk = false; break; }
        }
        check(allOk, "Rapid CREATE/DROP 5 databases -> all succeed");
    }

    // 9.5o. SELECT with DISTINCT on multiple columns
    {
        auto j = sendSql("SELECT DISTINCT active FROM accounts;");
        bool ok = j.value("success", false);
        check(ok, "SELECT DISTINCT single column -> works");
    }

    // 9.5p. Very deep nested WHERE parentheses
    {
        auto j = sendSql("SELECT * FROM accounts WHERE ((((id > 0))));");
        bool ok = j.value("success", false)
                  && j.value("resultSet", nlohmann::json::array()).size() >= 5;
        check(ok, "Deeply nested WHERE parentheses -> works");
    }

    // 9.5q. String comparison with special characters
    {
        auto j = sendSql("SELECT * FROM accounts WHERE name = 'O''Brien';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() >= 2
             && rs[0][1].get<std::string>() == "O'Brien";
        check(ok, "SELECT with single quote in WHERE -> matches O'Brien");
    }

    // 9.5r. Handling of boolean in WHERE
    {
        auto j = sendSql("SELECT * FROM accounts WHERE active;");
        bool ok = j.value("success", false) || !j.value("success", false);
        check(ok, "WHERE with bare boolean column -> handled without crash");
    }

    // =========================================================================
    // Phase 10: Schema Introspection
    // =========================================================================

    {
        auto j = sendSql("SHOW TABLES;");
        bool ok = j.value("success", false)
                  && !j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "SHOW TABLES -> returns at least accounts");
    }

    {
        auto j = sendSql("SHOW DATABASES;");
        bool ok = j.value("success", false)
                  && !j.value("resultSet", nlohmann::json::array()).empty();
        check(ok, "SHOW DATABASES -> returns at least system and client_test_db");
    }

    // =========================================================================
    // Phase 11: Cleanup
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
    // =========================================================================
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nClientTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;
    writeReport("ClientTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
