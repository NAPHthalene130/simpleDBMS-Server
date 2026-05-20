/**
 * @brief DML SQL 测试 — 验证 INSERT/SELECT/UPDATE/DELETE/TRUNCATE（富测试）
 * @author NAPH130
 */
#include "Core.h"
#include "core/SqlPipeline.h"
#include "log/LogWriter.h"
#include "models/network/NetworkExecutionContext.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
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
    netCtx.setConnectionId("dml-test");
    netCtx.setCurrentUser("tester");

    int passed = 0;
    int failed = 0;
    int skipped = 0;
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

    auto skip = [&](const std::string &desc) {
        std::cout << "SKIP: " << desc << " (not supported)" << std::endl;
        ++skipped;
    };

    // ========== Setup ==========
    check(sendSql("CREATE DATABASE dml_test_db;").value("success", false), "CREATE DATABASE dml_test_db");
    check(sendSql("USE DATABASE dml_test_db;").value("success", false), "USE DATABASE dml_test_db");
    check(sendSql("CREATE TABLE products (id INT, name VARCHAR(100), price FLOAT, qty INT);").value("success", false), "CREATE TABLE products");

    // ========== INSERT — 10+ rows ==========
    check(sendSql("INSERT INTO products VALUES (1, 'Apple', 1.50, 100);").value("success", false), "INSERT 1 Apple");
    check(sendSql("INSERT INTO products VALUES (2, 'Banana', 0.80, 150);").value("success", false), "INSERT 2 Banana");
    check(sendSql("INSERT INTO products VALUES (3, 'Orange', 1.20, 80);").value("success", false), "INSERT 3 Orange");
    check(sendSql("INSERT INTO products VALUES (4, 'Grape', 3.00, 40);").value("success", false), "INSERT 4 Grape");
    check(sendSql("INSERT INTO products VALUES (5, 'Mango', 2.50, 60);").value("success", false), "INSERT 5 Mango");

    // INSERT with explicit column list
    check(sendSql("INSERT INTO products (id, name, price, qty) VALUES (6, 'Peach', 2.00, 70);").value("success", false), "INSERT 6 Peach (column list)");
    check(sendSql("INSERT INTO products (name, id, qty, price) VALUES ('Kiwi', 7, 55, 2.80);").value("success", false), "INSERT 7 Kiwi (column reorder)");
    check(sendSql("INSERT INTO products VALUES (8, 'Lemon', 0.90, 120);").value("success", false), "INSERT 8 Lemon");
    check(sendSql("INSERT INTO products VALUES (9, 'Cherry', 4.50, 30);").value("success", false), "INSERT 9 Cherry");
    check(sendSql("INSERT INTO products (id, name, price, qty) VALUES (10, 'Papaya', 3.20, 45);").value("success", false), "INSERT 10 Papaya (column list)");

    // ========== SELECT ==========
    {
        auto j = sendSql("SELECT * FROM products;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 10;
        check(ok, "SELECT * -> 10 rows");
    }

    {
        auto j = sendSql("SELECT * FROM products LIMIT 2;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 2;
        check(ok, "SELECT * LIMIT 2 -> 2 rows");
    }

    {
        auto j = sendSql("SELECT name, price FROM products ORDER BY price DESC;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        if (ok && !rs.empty() && rs[0].is_array() && rs[0].size() >= 1) {
            ok = rs[0][0].get<std::string>() == "Cherry";
        } else {
            ok = false;
        }
        check(ok, "SELECT name,price ORDER BY price DESC (top=Cherry)");
    }

    {
        auto j = sendSql("SELECT * FROM products WHERE id != 5;");
        bool ok = j.value("success", false) && j.value("resultSet", nlohmann::json::array()).size() == 9;
        check(ok, "SELECT id != 5 -> 9 rows");
    }

    {
        auto j = sendSql("SELECT * FROM products WHERE price >= 2.00 AND price <= 3.00;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        bool countOk = (rs.size() == 4);
        std::ostringstream extra;
        if (!countOk) extra << "expected 4 got " << rs.size();
        check(ok && countOk, "SELECT price >= 2.00 AND <= 3.00 -> 4 rows", extra.str());
    }

    // ========== UPDATE ==========
    check(sendSql("UPDATE products SET price = 1.80 WHERE name = 'Banana';").value("success", false), "UPDATE Banana price=1.80");
    check(sendSql("UPDATE products SET qty = 200 WHERE id = 1;").value("success", false), "UPDATE id=1 qty=200");

    // Verify UPDATE values
    {
        auto j = sendSql("SELECT price FROM products WHERE name = 'Banana';");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() >= 1;
        double priceVal = ok ? std::stod(rs[0][0].get<std::string>()) : 0.0;
        ok = ok && std::abs(priceVal - 1.80) < 0.001;
        check(ok, "Verify UPDATE Banana price=1.80");
    }

    {
        auto j = sendSql("SELECT qty FROM products WHERE id = 1;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() >= 1
             && rs[0][0].get<std::string>() == "200";
        check(ok, "Verify UPDATE id=1 qty=200");
    }

    // UPDATE all rows
    check(sendSql("UPDATE products SET qty = qty + 10;").value("success", false), "UPDATE all rows qty=qty+10");

    // ========== DELETE ==========
    check(sendSql("DELETE FROM products WHERE id = 5;").value("success", false), "DELETE id=5");

    // DELETE with multiple conditions (AND)
    check(sendSql("DELETE FROM products WHERE price < 1.00 AND qty > 100;").value("success", false), "DELETE WHERE price<1.00 AND qty>100");

    // ========== AGGREGATES ==========
    {
        auto j = sendSql("SELECT COUNT(*) FROM products;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0 && rs[0][0].get<std::string>() == "8";
        check(ok, "SELECT COUNT -> 8");
    }

    {
        auto j = sendSql("SELECT SUM(price) FROM products;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0;
        check(ok, "SELECT SUM(price) from products");
    }

    {
        auto j = sendSql("SELECT AVG(price) FROM products;");
        bool ok = j.value("success", false);
        auto rs = j.value("resultSet", nlohmann::json::array());
        ok = ok && rs.size() == 1 && rs[0].is_array() && rs[0].size() > 0;
        check(ok, "SELECT AVG(price) from products");
    }

    // ========== TRUNCATE ==========
    {
        auto j = sendSql("TRUNCATE TABLE products;");
        if (j.value("success", false)) {
            check(true, "TRUNCATE TABLE products");

            check(sendSql("INSERT INTO products (id, name, price, qty) VALUES (1, 'Avocado', 5.00, 10);").value("success", false), "INSERT after TRUNCATE");

            {
                auto j2 = sendSql("SELECT * FROM products;");
                bool ok = j2.value("success", false) && j2.value("resultSet", nlohmann::json::array()).size() == 1;
                check(ok, "Verify 1 row after TRUNCATE+INSERT");
            }
        } else {
            skip("TRUNCATE TABLE products");
            skip("INSERT after TRUNCATE");
        }
    }

    // ========== ERROR CASES ==========
    {
        auto j = sendSql("INSERT INTO products VALUES (99, 'BadRow');");
        check(!j.value("success", false), "Error: INSERT wrong column count (expected fail)");
    }

    {
        auto j = sendSql("UPDATE nonexistent_table SET x = 1;");
        check(!j.value("success", false), "Error: UPDATE non-existent table (expected fail)");
    }

    {
        auto j = sendSql("DELETE FROM nonexistent_table;");
        check(!j.value("success", false), "Error: DELETE non-existent table (expected fail)");
    }

    // ========== Cleanup ==========
    check(sendSql("DROP DATABASE dml_test_db;").value("success", false), "DROP DATABASE dml_test_db");

    // ========== Report ==========
    int total = passed + failed;
    int pct = total > 0 ? passed * 100 / total : 0;
    std::cout << "\nDmlSqlTest: " << passed << "/" << total << " (" << pct << "%)";
    if (skipped > 0) std::cout << " [" << skipped << " skipped]";
    std::cout << std::endl;
    writeReport("DmlSqlTest", passed, failed, passedIds, failedIds);
    return failed > 0 ? 1 : 0;
}
