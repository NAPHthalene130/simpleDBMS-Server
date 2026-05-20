/**
 * @brief DDL SQL 测试 — 验证 CREATE/DROP/ALTER TABLE/TRUNCATE (enriched)
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

int main()
{
    Core core;
    SqlPipeline *pipeline = core.getSqlPipeline();

    NetworkExecutionContext netCtx;
    netCtx.setConnectionId("ddl-test");
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

    // ==============================
    //  1. CREATE DATABASE
    // ==============================
    check(sendSql("CREATE DATABASE ddl_test_db;").value("success", false), "CREATE DATABASE ddl_test_db");

    // ==============================
    //  2. CREATE DATABASE IF NOT EXISTS
    // ==============================
    check(sendSql("CREATE DATABASE IF NOT EXISTS ddl_test_db;").value("success", false), "CREATE DATABASE IF NOT EXISTS ddl_test_db");

    // ==============================
    //  3. USE DATABASE
    // ==============================
    check(sendSql("USE DATABASE ddl_test_db;").value("success", false), "USE DATABASE ddl_test_db");

    // ==============================
    //  4. CREATE TABLE with 4 columns (original)
    // ==============================
    check(sendSql("CREATE TABLE items (id INT, name VARCHAR(100), price FLOAT, description TEXT);").value("success", false), "CREATE TABLE items");

    // ==============================
    //  5. CREATE TABLE with 5+ columns of different types
    // ==============================
    check(sendSql("CREATE TABLE wide_table (id INT, name VARCHAR(100), price FLOAT, description TEXT, created DATE);").value("success", false), "CREATE TABLE wide_table (INT/VARCHAR/FLOAT/TEXT/DATE)");

    // ==============================
    //  6. CREATE TABLE with PRIMARY KEY constraint
    // ==============================
    check(sendSql("CREATE TABLE pk_table (id INT PRIMARY KEY, name VARCHAR(100));").value("success", false), "CREATE TABLE pk_table (PRIMARY KEY)");

    // ==============================
    //  7. CREATE TABLE with NOT NULL constraint
    // ==============================
    check(sendSql("CREATE TABLE nn_table (id INT NOT NULL, name VARCHAR(100));").value("success", false), "CREATE TABLE nn_table (NOT NULL)");

    // ==============================
    //  8. CREATE TABLE with UNIQUE constraint
    // ==============================
    check(sendSql("CREATE TABLE uq_table (id INT UNIQUE, name VARCHAR(100));").value("success", false), "CREATE TABLE uq_table (UNIQUE)");

    // ==============================
    //  9. CREATE TABLE with AUTO_INCREMENT column
    // ==============================
    check(sendSql("CREATE TABLE ai_table (id INT AUTO_INCREMENT, name VARCHAR(100));").value("success", false), "CREATE TABLE ai_table (AUTO_INCREMENT)");

    // ==============================
    // 10. CREATE multiple tables in same database
    // ==============================
    check(sendSql("CREATE TABLE tbl_one (id INT);").value("success", false), "CREATE TABLE tbl_one");
    check(sendSql("CREATE TABLE tbl_two (id INT);").value("success", false), "CREATE TABLE tbl_two");

    // ==============================
    // 11. SHOW DATABASES — verify ddl_test_db appears
    // ==============================
    {
        auto j = sendSql("SHOW DATABASES;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool found = false;
        for (auto &row : rs) {
            if (row.is_array() && row.size() > 0 && row[0].get<std::string>() == "ddl_test_db") {
                found = true; break;
            }
        }
        check(ok && found, "SHOW DATABASES contains ddl_test_db");
    }

    // ==============================
    // 12. SHOW TABLES — verify items appears
    // ==============================
    {
        auto j = sendSql("SHOW TABLES;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool found = false;
        for (auto &row : rs) {
            if (row.is_array() && row.size() > 0 && row[0].get<std::string>() == "items") {
                found = true; break;
            }
        }
        check(ok && found, "SHOW TABLES contains items");
    }

    // ==============================
    // 13. INSERT into items (original)
    // ==============================
    check(sendSql("INSERT INTO items VALUES (1, 'Widget', 9.99, 'A useful widget');").value("success", false), "INSERT Widget");

    // ==============================
    // 14. SELECT * -> 1 row, 4 columns (original)
    // ==============================
    {
        auto j = sendSql("SELECT * FROM items;");
        bool ok = j.value("success", false);
        auto cols = j.value("columns", nlohmann::json::array());
        auto rows = j.value("resultSet", nlohmann::json::array());
        check(ok && rows.size() == 1 && cols.size() == 4, "SELECT * -> 1 row, 4 cols");
    }

    // ==============================
    // 15. ALTER TABLE ADD COLUMN (original)
    // ==============================
    check(sendSql("ALTER TABLE items ADD COLUMN category VARCHAR(50);").value("success", false), "ALTER TABLE ADD COLUMN category");

    // ==============================
    // 16. INSERT with new column (original)
    // ==============================
    check(sendSql("INSERT INTO items VALUES (2, 'Gadget', 19.99, 'A cool gadget', 'Electronics');").value("success", false), "INSERT Gadget with category");

    // ==============================
    // 17. SELECT new column -> Electronics (original)
    // ==============================
    {
        auto j = sendSql("SELECT category FROM items WHERE id = 2;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "Electronics";
        check(ok, "SELECT category -> Electronics");
    }

    // ==============================
    // 18. ALTER TABLE RENAME COLUMN (original)
    // ==============================
    check(sendSql("ALTER TABLE items RENAME COLUMN category TO item_type;").value("success", false), "ALTER TABLE RENAME COLUMN");

    // ==============================
    // 19. INSERT with renamed column (original)
    // ==============================
    check(sendSql("INSERT INTO items VALUES (3, 'Thing', 4.99, 'A simple thing', 'Misc');").value("success", false), "INSERT Thing with item_type");

    // ==============================
    // 20. SELECT renamed column -> Misc (original)
    // ==============================
    {
        auto j = sendSql("SELECT item_type FROM items WHERE id = 3;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "Misc";
        check(ok, "SELECT item_type -> Misc");
    }

    // ==============================
    // 21. ALTER TABLE DROP COLUMN (original)
    // ==============================
    check(sendSql("ALTER TABLE items DROP COLUMN item_type;").value("success", false), "ALTER TABLE DROP COLUMN");

    // ==============================
    // 22. SELECT after DROP COLUMN -> 4 cols (original)
    // ==============================
    {
        auto j = sendSql("SELECT * FROM items WHERE id = 3;");
        bool ok = j.value("success", false);
        auto cols = j.value("columns", nlohmann::json::array());
        check(ok && cols.size() == 4, "SELECT after DROP COLUMN -> 4 cols");
    }

    // ==============================
    // 23. TRUNCATE TABLE (original)
    // ==============================
    check(sendSql("TRUNCATE TABLE items;").value("success", false), "TRUNCATE TABLE items");

    // ==============================
    // 24. SELECT after TRUNCATE -> 0 rows (original)
    // ==============================
    {
        auto j = sendSql("SELECT * FROM items;");
        check(j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 0, "SELECT after TRUNCATE -> 0 rows");
    }

    // ==============================
    // 25. ALTER TABLE ADD COLUMN with DEFAULT value
    // ==============================
    check(sendSql("ALTER TABLE items ADD COLUMN status INT DEFAULT 0;").value("success", false), "ALTER TABLE ADD COLUMN status DEFAULT 0");

    // ==============================
    // 26. ALTER TABLE ADD COLUMN with NOT NULL
    // ==============================
    check(sendSql("ALTER TABLE items ADD COLUMN active INT NOT NULL DEFAULT 1;").value("success", false), "ALTER TABLE ADD COLUMN active NOT NULL DEFAULT 1");

    // ==============================
    // 27. INSERT after DEFAULT/NOT NULL column additions
    // ==============================
    check(sendSql("INSERT INTO items VALUES (4, 'Test', 1.00, 'Test item', 5, 1);").value("success", false), "INSERT after column additions with defaults");

    // ==============================
    // 28. DROP TABLE items (original)
    // ==============================
    check(sendSql("DROP TABLE items;").value("success", false), "DROP TABLE items");

    // ==============================
    // 29. SHOW TABLES — verify items removed after DROP
    // ==============================
    {
        auto j = sendSql("SHOW TABLES;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool found = false;
        for (auto &row : rs) {
            if (row.is_array() && row.size() > 0 && row[0].get<std::string>() == "items") {
                found = true; break;
            }
        }
        check(ok && !found, "SHOW TABLES after DROP — items gone");
    }

    // ==============================
    // 30. DROP TABLE on non-existent table (should fail)
    // ==============================
    check(!sendSql("DROP TABLE ghost_table;").value("success", false), "DROP TABLE ghost_table (non-existent) -> should fail");

    // ==============================
    // 31. ALTER TABLE on non-existent table (should fail)
    // ==============================
    check(!sendSql("ALTER TABLE ghost_table ADD COLUMN x INT;").value("success", false), "ALTER TABLE ghost_table (non-existent) -> should fail");

    // ==============================
    // 32. DROP DATABASE ddl_test_db — with tables still inside
    // ==============================
    check(sendSql("DROP DATABASE ddl_test_db;").value("success", false), "DROP DATABASE ddl_test_db (with tables inside)");

    // ==============================
    // 33. DROP DATABASE on non-existent database (should fail)
    // ==============================
    check(!sendSql("DROP DATABASE ghost_db;").value("success", false), "DROP DATABASE ghost_db (non-existent) -> should fail");

    // ==============================
    // Report
    // ==============================
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nDdlSqlTest: " << passed << "/" << total << " (" << pct << "%)" << std::endl;
    writeReport("DdlSqlTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
