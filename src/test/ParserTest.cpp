/**
 * @file ParserTest.cpp
 * @brief SQL语法分析器测试
 * @details 测试Parser对各SQL语句类型的AST构建，包括CREATE/INSERT/SELECT/DELETE/UPDATE/
 *          DROP/SHOW/USE/DATABASE等，以及错误SQL的拒绝处理。
 * @author NAPH130
 */
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Core.h"
#include "tokenizer/Tokenizer.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "models/parser/CreateDbStmt.h"
#include "models/parser/CreateTableStmt.h"
#include "models/parser/DeleteStmt.h"
#include "models/parser/DropStmt.h"
#include "models/parser/InsertStmt.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/ShowStmt.h"
#include "models/parser/UpdateStmt.h"
#include "models/parser/UseDbStmt.h"

namespace {

struct TestStepResult { int id; std::string name; bool passed; std::string detail; };
int gTotal = 0, gPassed = 0;

void appendStep(std::vector<TestStepResult> &s, int id, const std::string &name, bool p, const std::string &d = "") {
    ++gTotal; if (p) ++gPassed; s.push_back({id, name, p, d});
}

std::string nowStr() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{}; localtime_s(&tm, &t);
    std::ostringstream oss; oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S"); return oss.str();
}

void writeReportLog(const std::string &suite, const std::vector<TestStepResult> &steps) {
    std::filesystem::create_directories("test");
    std::ofstream ofs("test/report.log", std::ios::app);
    if (!ofs.good()) return;
    ofs << "====================\n" << suite << "\n" << nowStr() << "\n" << gPassed << "/" << gTotal << "\n";
    for (auto &s : steps) ofs << "[" << (s.passed ? "YES" : "NO") << "]" << s.name << "\n";
}

} // namespace

int main() {
    std::vector<TestStepResult> steps;
    Core core;

    std::cout << "\n========== Parser Test ==========\n";

    auto parse = [&](const std::string &sql) {
        Tokenizer tk(&core, sql);
        return core.getParserManager()->getParser()->parse(tk.tokenize());
    };

    // ====== 1. CREATE DATABASE ======
    {
        auto r = parse("CREATE DATABASE test_db;");
        bool p = r.success && r.statement != nullptr && r.statement->getStmtType() == ExecutionStatementType::CreateDatabase;
        appendStep(steps, 1, "Parse CREATE DATABASE", p, p ? "ok" : r.errorMessage);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-1\n";
    }
    {
        auto r = parse("CREATE DATABASE my123_db;");
        bool p = r.success;
        appendStep(steps, 2, "Parse CREATE DATABASE with numbers in name", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-2\n";
    }
    {
        auto r = parse("CREATE DATABASE"); // missing name
        bool p = !r.success;
        appendStep(steps, 3, "Reject CREATE DATABASE without name", p, r.errorMessage);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-3\n";
    }

    // ====== 2. CREATE TABLE ======
    {
        auto r = parse("CREATE TABLE t (id INT, name VARCHAR(50));");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::CreateTable;
        appendStep(steps, 4, "Parse CREATE TABLE with columns", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-4\n";
    }
    {
        auto r = parse("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100) NOT NULL, age INT DEFAULT 0);");
        bool p = r.success;
        appendStep(steps, 5, "Parse CREATE TABLE with constraints", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-5\n";
    }
    {
        auto r = parse("CREATE TABLE t (id INT, data TEXT);");
        bool p = r.success;
        appendStep(steps, 6, "Parse CREATE TABLE with TEXT type", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-6\n";
    }
    {
        auto r = parse("CREATE TABLE t (price FLOAT);");
        bool p = r.success;
        appendStep(steps, 7, "Parse CREATE TABLE with FLOAT type", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-7\n";
    }
    {
        auto r = parse("CREATE TABLE t (id INT, score DOUBLE);");
        bool p = r.success;
        appendStep(steps, 8, "Parse CREATE TABLE with DOUBLE type", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-8\n";
    }

    // ====== 3. INSERT ======
    {
        auto r = parse("INSERT INTO t VALUES (1, 'hello');");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Insert;
        appendStep(steps, 9, "Parse INSERT INTO VALUES", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-9\n";
    }
    {
        auto r = parse("INSERT INTO t (id, name) VALUES (1, 'alice');");
        bool p = r.success;
        appendStep(steps, 10, "Parse INSERT with column list", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-10\n";
    }
    {
        auto r = parse("INSERT INTO t VALUES (1, 'a', 3.14);");
        bool p = r.success;
        appendStep(steps, 11, "Parse INSERT with mixed types", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-11\n";
    }

    // ====== 4. SELECT ======
    {
        auto r = parse("SELECT * FROM t;");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Select;
        appendStep(steps, 12, "Parse SELECT *", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-12\n";
    }
    {
        auto r = parse("SELECT id, name FROM users;");
        bool p = r.success;
        appendStep(steps, 13, "Parse SELECT specific columns", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-13\n";
    }
    {
        auto r = parse("SELECT * FROM t WHERE id = 1;");
        bool p = r.success;
        appendStep(steps, 14, "Parse SELECT with WHERE", p, p ? "ok" : r.errorMessage);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-14\n";
    }
    {
        auto r = parse("SELECT * FROM t WHERE name = 'alice' AND age > 18;");
        bool p = r.success;
        appendStep(steps, 15, "Parse SELECT with AND condition", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-15\n";
    }
    {
        auto r = parse("SELECT * FROM t WHERE id BETWEEN 1 AND 10;");
        bool p = r.success;
        appendStep(steps, 16, "Parse SELECT with BETWEEN", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-16\n";
    }
    {
        auto r = parse("SELECT * FROM t WHERE name LIKE 'A%';");
        bool p = r.success;
        appendStep(steps, 17, "Parse SELECT with LIKE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-17\n";
    }
    {
        auto r = parse("SELECT * FROM t WHERE id IN (1, 2, 3);");
        bool p = r.success;
        appendStep(steps, 18, "Parse SELECT with IN", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-18\n";
    }
    {
        auto r = parse("SELECT * FROM t ORDER BY name ASC;");
        bool p = r.success;
        appendStep(steps, 19, "Parse SELECT with ORDER BY ASC", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-19\n";
    }
    {
        auto r = parse("SELECT * FROM t ORDER BY id DESC;");
        bool p = r.success;
        appendStep(steps, 20, "Parse SELECT with ORDER BY DESC", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-20\n";
    }
    {
        auto r = parse("SELECT * FROM t LIMIT 10;");
        bool p = r.success;
        appendStep(steps, 21, "Parse SELECT with LIMIT", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-21\n";
    }

    // ====== 5. DROP ======
    {
        auto r = parse("DROP TABLE t;");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::DropTable;
        appendStep(steps, 22, "Parse DROP TABLE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-22\n";
    }
    {
        auto r = parse("DROP DATABASE mydb;");
        bool p = r.success;
        appendStep(steps, 23, "Parse DROP DATABASE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-23\n";
    }

    // ====== 6. SHOW ======
    {
        auto r = parse("SHOW DATABASES;");
        bool p = r.success;
        appendStep(steps, 24, "Parse SHOW DATABASES", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-24\n";
    }
    {
        auto r = parse("SHOW TABLES;");
        bool p = r.success;
        appendStep(steps, 25, "Parse SHOW TABLES", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-25\n";
    }

    // ====== 7. USE ======
    {
        auto r = parse("USE DATABASE mydb;");
        bool p = r.success;
        appendStep(steps, 26, "Parse USE DATABASE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-26\n";
    }

    // ====== 8. DELETE ======
    {
        auto r = parse("DELETE FROM t WHERE id = 1;");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Delete;
        appendStep(steps, 27, "Parse DELETE with WHERE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-27\n";
    }

    // ====== 9. UPDATE ======
    {
        auto r = parse("UPDATE t SET name = 'bob' WHERE id = 1;");
        bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Update;
        appendStep(steps, 28, "Parse UPDATE with SET and WHERE", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-28\n";
    }
    {
        auto r = parse("UPDATE t SET x = 1, y = 2;");
        bool p = r.success;
        appendStep(steps, 29, "Parse UPDATE with multiple SET columns", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-29\n";
    }

    // ====== 10. Error cases ======
    {
        auto r = parse("INVALID SQL STATEMENT");
        bool p = !r.success;
        appendStep(steps, 30, "Reject invalid SQL", p, r.errorMessage);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-30\n";
    }
    {
        auto r = parse("SELECT");
        bool p = !r.success;
        appendStep(steps, 31, "Reject incomplete SELECT", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-31\n";
    }

    bool overall = std::all_of(steps.begin(), steps.end(), [](auto &s){ return s.passed; });
    double pct = gTotal > 0 ? 100.0*gPassed/gTotal : 0;
    std::cout << "\nResults: " << gPassed << "/" << gTotal << " (" << pct << "%)\nOverall: " << (overall?"PASS":"FAIL") << "\n";
    if (!overall) for (auto &s : steps) if (!s.passed) std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";

    writeReportLog("ParserTest", steps);
    return overall ? 0 : 1;
}
