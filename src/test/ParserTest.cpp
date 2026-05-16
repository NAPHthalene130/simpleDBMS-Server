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
#include "models/parser/UseStmt.h"
#include "models/parser/UnionStmt.h"

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
}

int main() {
    std::vector<TestStepResult> steps;
    Core core;
    std::cout << "\n========== Parser Test ==========\n";
    auto parse = [&](const std::string &sql) {
        Tokenizer tk(&core, sql);
        return core.getParserManager()->getParser()->parse(tk.tokenize());
    };

    {auto r = parse("CREATE DATABASE test_db;"); bool p = r.success && r.statement != nullptr && r.statement->getStmtType() == ExecutionStatementType::CreateDatabase; appendStep(steps, 1, "Parse CREATE DATABASE", p, p ? "ok" : r.errorMessage); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-1\n";}
    {auto r = parse("CREATE DATABASE my123_db;"); bool p = r.success; appendStep(steps, 2, "Parse CREATE DATABASE with numbers", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-2\n";}
    {auto r = parse("CREATE DATABASE"); bool p = !r.success; appendStep(steps, 3, "Reject CREATE DATABASE no name", p, r.errorMessage); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-3\n";}
    {auto r = parse("CREATE DATABASE _hidden;"); bool p = r.success; appendStep(steps, 4, "Parse CREATE DATABASE underscore prefix", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-4\n";}
    {auto r = parse("CREATE TABLE t (id INT, name VARCHAR(50));"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::CreateTable; appendStep(steps, 5, "Parse CREATE TABLE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-5\n";}
    {auto r = parse("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100) NOT NULL, age INT DEFAULT 0);"); bool p = r.success; appendStep(steps, 6, "Parse CREATE TABLE constraints", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-6\n";}
    {auto r = parse("CREATE TABLE t (id INT, data TEXT);"); bool p = r.success; appendStep(steps, 7, "Parse CREATE TABLE TEXT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-7\n";}
    {auto r = parse("CREATE TABLE t (price FLOAT);"); bool p = r.success; appendStep(steps, 8, "Parse CREATE TABLE FLOAT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-8\n";}
    {auto r = parse("CREATE TABLE t (id INT, score DOUBLE);"); bool p = r.success; appendStep(steps, 9, "Parse CREATE TABLE DOUBLE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-9\n";}
    {auto r = parse("CREATE TABLE t (id INT AUTO_INCREMENT PRIMARY KEY);"); bool p = r.success; appendStep(steps, 10, "Parse CREATE TABLE AUTO_INCREMENT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-10\n";}
    {auto r = parse("CREATE TABLE t (email VARCHAR(255) UNIQUE);"); bool p = r.success; appendStep(steps, 11, "Parse CREATE TABLE UNIQUE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-11\n";}
    {auto r = parse("CREATE TABLE t (a INT, b INT, c INT);"); bool p = r.success; appendStep(steps, 12, "Parse CREATE TABLE multiple columns", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-12\n";}
    {auto r = parse("CREATE TABLE t (val BOOLEAN);"); bool p = r.success; appendStep(steps, 13, "Parse CREATE TABLE BOOLEAN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-13\n";}
    {auto r = parse("CREATE TABLE t (id INT NOT NULL);"); bool p = r.success; appendStep(steps, 14, "Parse CREATE TABLE NOT NULL", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-14\n";}
    {auto r = parse("CREATE TABLE t (id INT DEFAULT 100);"); bool p = r.success; appendStep(steps, 15, "Parse CREATE TABLE DEFAULT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-15\n";}
    {auto r = parse("CREATE TABLE t (name VARCHAR(10));"); bool p = r.success; appendStep(steps, 16, "Parse CREATE TABLE VARCHAR length", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-16\n";}
    {auto r = parse("INSERT INTO t VALUES (1, 'hello');"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Insert; appendStep(steps, 17, "Parse INSERT INTO VALUES", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-17\n";}
    {auto r = parse("INSERT INTO t (id, name) VALUES (1, 'alice');"); bool p = r.success; appendStep(steps, 18, "Parse INSERT column list", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-18\n";}
    {auto r = parse("INSERT INTO t VALUES (1, 'a', 3.14);"); bool p = r.success; appendStep(steps, 19, "Parse INSERT mixed types", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-19\n";}
    {auto r = parse("INSERT INTO t (id) VALUES (1);"); bool p = r.success; appendStep(steps, 20, "Parse INSERT single column", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-20\n";}
    {auto r = parse("INSERT INTO t VALUES (1, 2, 3, 4, 5);"); bool p = r.success; appendStep(steps, 21, "Parse INSERT many values", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-21\n";}
    {auto r = parse("SELECT * FROM t;"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Select; appendStep(steps, 22, "Parse SELECT *", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-22\n";}
    {auto r = parse("SELECT id, name FROM users;"); bool p = r.success; appendStep(steps, 23, "Parse SELECT specific columns", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-23\n";}
    {auto r = parse("SELECT * FROM t WHERE id = 1;"); bool p = r.success; appendStep(steps, 24, "Parse SELECT WHERE", p, p ? "ok" : r.errorMessage); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-24\n";}
    {auto r = parse("SELECT * FROM t WHERE name = 'alice' AND age > 18;"); bool p = r.success; appendStep(steps, 25, "Parse SELECT AND condition", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-25\n";}
    {auto r = parse("SELECT * FROM t WHERE id BETWEEN 1 AND 10;"); bool p = r.success; appendStep(steps, 26, "Parse SELECT BETWEEN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-26\n";}
    {auto r = parse("SELECT * FROM t WHERE name LIKE 'A%';"); bool p = r.success; appendStep(steps, 27, "Parse SELECT LIKE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-27\n";}
    {auto r = parse("SELECT * FROM t WHERE id IN (1, 2, 3);"); bool p = r.success; appendStep(steps, 28, "Parse SELECT IN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-28\n";}
    {auto r = parse("SELECT * FROM t ORDER BY name ASC;"); bool p = r.success; appendStep(steps, 29, "Parse SELECT ORDER BY ASC", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-29\n";}
    {auto r = parse("SELECT * FROM t ORDER BY id DESC;"); bool p = r.success; appendStep(steps, 30, "Parse SELECT ORDER BY DESC", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-30\n";}
    {auto r = parse("SELECT * FROM t LIMIT 10;"); bool p = r.success; appendStep(steps, 31, "Parse SELECT LIMIT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-31\n";}
    {auto r = parse("SELECT DISTINCT name FROM t;"); bool p = r.success; appendStep(steps, 32, "Parse SELECT DISTINCT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-32\n";}
    {auto r = parse("SELECT * FROM t WHERE id IS NULL;"); bool p = r.success; appendStep(steps, 33, "Parse SELECT IS NULL", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-33\n";}
    {auto r = parse("SELECT * FROM t WHERE id IS NOT NULL;"); bool p = r.success; appendStep(steps, 34, "Parse SELECT IS NOT NULL", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-34\n";}
    {auto r = parse("SELECT * FROM t WHERE NOT id = 1;"); bool p = r.success; appendStep(steps, 35, "Parse SELECT NOT condition", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-35\n";}
    {auto r = parse("SELECT * FROM t WHERE id NOT IN (1, 2);"); bool p = r.success; appendStep(steps, 36, "Parse SELECT NOT IN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-36\n";}
    {auto r = parse("SELECT * FROM t WHERE name NOT LIKE 'A%';"); bool p = r.success; appendStep(steps, 37, "Parse SELECT NOT LIKE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-37\n";}
    {auto r = parse("SELECT * FROM t WHERE id NOT BETWEEN 1 AND 10;"); bool p = r.success; appendStep(steps, 38, "Parse SELECT NOT BETWEEN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-38\n";}
    {auto r = parse("SELECT id AS user_id FROM t;"); bool p = r.success; appendStep(steps, 39, "Parse SELECT column alias", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-39\n";}
    {auto r = parse("SELECT COUNT(*) FROM t;"); bool p = r.success; appendStep(steps, 40, "Parse SELECT COUNT(*)", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-40\n";}
    {auto r = parse("SELECT SUM(amount), AVG(amount) FROM t;"); bool p = r.success; appendStep(steps, 41, "Parse SELECT SUM/AVG", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-41\n";}
    {auto r = parse("SELECT MIN(price), MAX(price) FROM t;"); bool p = r.success; appendStep(steps, 42, "Parse SELECT MIN/MAX", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-42\n";}
    {auto r = parse("SELECT * FROM t WHERE (id = 1 OR id = 2) AND name = 'x';"); bool p = r.success; appendStep(steps, 43, "Parse SELECT parenthesized OR+AND", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-43\n";}
    {auto r = parse("SELECT * FROM t WHERE age > 18 AND age < 65 OR name = 'admin';"); bool p = r.success; appendStep(steps, 44, "Parse SELECT mixed AND/OR", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-44\n";}
    {auto r = parse("SELECT t.id, t.name FROM t;"); bool p = r.success; appendStep(steps, 45, "Parse SELECT table.column", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-45\n";}
    {auto r = parse("SELECT * FROM t WHERE EXISTS (SELECT 1 FROM t2 WHERE t2.id = t.id);"); bool p = r.success; appendStep(steps, 46, "Parse SELECT EXISTS subquery", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-46\n";}
    {auto r = parse("SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;"); bool p = r.success; appendStep(steps, 47, "Parse SELECT INNER JOIN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-47\n";}
    {auto r = parse("SELECT * FROM t1 LEFT JOIN t2 ON t1.id = t2.id;"); bool p = r.success; appendStep(steps, 48, "Parse SELECT LEFT JOIN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-48\n";}
    {auto r = parse("SELECT * FROM t1 RIGHT JOIN t2 ON t1.id = t2.id;"); bool p = r.success; appendStep(steps, 49, "Parse SELECT RIGHT JOIN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-49\n";}
    {auto r = parse("SELECT * FROM t1 JOIN t2 ON t1.id = t2.id;"); bool p = r.success; appendStep(steps, 50, "Parse SELECT JOIN (default INNER)", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-50\n";}
    {auto r = parse("SELECT COUNT(*) FROM t1 JOIN t2 ON t1.id = t2.id GROUP BY t1.name;"); bool p = r.success; appendStep(steps, 51, "Parse SELECT JOIN + GROUP BY", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-51\n";}
    {auto r = parse("SELECT name, COUNT(*) FROM t GROUP BY name;"); bool p = r.success; appendStep(steps, 52, "Parse SELECT GROUP BY", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-52\n";}
    {auto r = parse("SELECT name, COUNT(*) FROM t GROUP BY name HAVING COUNT(*) > 1;"); bool p = r.success; appendStep(steps, 53, "Parse SELECT GROUP BY + HAVING", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-53\n";}
    {auto r = parse("SELECT category, SUM(price) FROM t GROUP BY category;"); bool p = r.success; appendStep(steps, 54, "Parse SELECT GROUP BY with SUM", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-54\n";}
    {auto r = parse("DROP TABLE t;"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::DropTable; appendStep(steps, 55, "Parse DROP TABLE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-55\n";}
    {auto r = parse("DROP DATABASE mydb;"); bool p = r.success; appendStep(steps, 56, "Parse DROP DATABASE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-56\n";}
    {auto r = parse("SHOW DATABASES;"); bool p = r.success; appendStep(steps, 57, "Parse SHOW DATABASES", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-57\n";}
    {auto r = parse("SHOW TABLES;"); bool p = r.success; appendStep(steps, 58, "Parse SHOW TABLES", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-58\n";}
    {auto r = parse("USE DATABASE mydb;"); bool p = r.success; appendStep(steps, 59, "Parse USE DATABASE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-59\n";}
    {auto r = parse("USE mydb;"); bool p = r.success; appendStep(steps, 60, "Parse USE short form", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-60\n";}
    {auto r = parse("DELETE FROM t WHERE id = 1;"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Delete; appendStep(steps, 61, "Parse DELETE WHERE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-61\n";}
    {auto r = parse("DELETE FROM t WHERE name = 'bob' AND age > 20;"); bool p = r.success; appendStep(steps, 62, "Parse DELETE complex WHERE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-62\n";}
    {auto r = parse("UPDATE t SET name = 'bob' WHERE id = 1;"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::Update; appendStep(steps, 63, "Parse UPDATE SET WHERE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-63\n";}
    {auto r = parse("UPDATE t SET x = 1, y = 2;"); bool p = r.success; appendStep(steps, 64, "Parse UPDATE multiple SET", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-64\n";}
    {auto r = parse("UPDATE t SET name = 'alice' WHERE id = 1 AND active = true;"); bool p = r.success; appendStep(steps, 65, "Parse UPDATE AND condition", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-65\n";}
    {auto r = parse("SELECT id FROM t1 UNION SELECT id FROM t2;"); bool p = r.success && r.statement->getStmtType() == ExecutionStatementType::UnionSelect; appendStep(steps, 66, "Parse UNION SELECT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-66\n";}
    {auto r = parse("SELECT id FROM t1 UNION ALL SELECT id FROM t2;"); bool p = r.success; appendStep(steps, 67, "Parse UNION ALL SELECT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-67\n";}
    {auto r = parse("SELECT * FROM t ORDER BY id LIMIT 5;"); bool p = r.success; appendStep(steps, 68, "Parse SELECT ORDER BY + LIMIT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-68\n";}
    {auto r = parse("SELECT * FROM t WHERE id > 10 ORDER BY name ASC LIMIT 100;"); bool p = r.success; appendStep(steps, 69, "Parse SELECT WHERE+ORDER BY+LIMIT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-69\n";}
    {auto r = parse("ALTER TABLE t ADD COLUMN age INT;"); bool p = r.success; appendStep(steps, 70, "Parse ALTER TABLE ADD COLUMN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-70\n";}
    {auto r = parse("TRUNCATE TABLE t;"); bool p = r.success; appendStep(steps, 71, "Parse TRUNCATE TABLE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-71\n";}
    {auto r = parse("INVALID SQL STATEMENT"); bool p = !r.success; appendStep(steps, 72, "Reject invalid SQL", p, r.errorMessage); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-72\n";}
    {auto r = parse("SELECT"); bool p = !r.success; appendStep(steps, 73, "Reject incomplete SELECT", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-73\n";}
    {auto r = parse("CREATE TABLE t;"); bool p = !r.success; appendStep(steps, 74, "Reject CREATE TABLE without columns", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-74\n";}
    {auto r = parse("INSERT INTO t VALUES;"); bool p = !r.success; appendStep(steps, 75, "Reject INSERT without values", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-75\n";}
    {auto r = parse("INSERT INTO t VALUES ();"); bool p = !r.success; appendStep(steps, 76, "Reject INSERT empty values", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-76\n";}
    {auto r = parse("SELECT * FROM t WHERE;"); bool p = !r.success; appendStep(steps, 77, "Reject SELECT WHERE no condition", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-77\n";}
    {auto r = parse("SELECT * FROM t ORDER BY;"); bool p = !r.success; appendStep(steps, 78, "Reject ORDER BY no column", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-78\n";}
    {auto r = parse("DROP TABLE;"); bool p = !r.success; appendStep(steps, 79, "Reject DROP TABLE no name", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-79\n";}
    {auto r = parse("UPDATE t SET;"); bool p = !r.success; appendStep(steps, 80, "Reject UPDATE SET empty", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-80\n";}
    {auto r = parse("DELETE FROM t;"); bool p = !r.success; appendStep(steps, 81, "Reject DELETE without WHERE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-81\n";}
    {auto r = parse("JOIN t1 t2;"); bool p = !r.success; appendStep(steps, 82, "Reject standalone JOIN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-82\n";}
    {auto r = parse("SELECT COUNT FROM t;"); bool p = r.success; appendStep(steps, 83, "Parse COUNT as identifier (not function)", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-83\n";}
    {auto r = parse(""); bool p = !r.success; appendStep(steps, 84, "Reject empty SQL", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-84\n";}
    {auto r = parse("SELECT * WHERE id = 1;"); bool p = !r.success; appendStep(steps, 85, "Reject SELECT without FROM", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-85\n";}
    {auto r = parse("UPDATE t WHERE id = 1;"); bool p = !r.success; appendStep(steps, 86, "Reject UPDATE without SET", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-86\n";}
    {auto r = parse("CREATE DATABASE IF NOT EXISTS test;"); bool p = r.success; appendStep(steps, 87, "Parse CREATE DATABASE IF NOT EXISTS", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-87\n";}
    {auto r = parse("SELECT * FROM t AS myalias;"); bool p = r.success; appendStep(steps, 88, "Parse SELECT table alias", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-88\n";}
    {auto r = parse("SELECT id FROM t WHERE name LIKE '%test%' OR name LIKE '%demo%';"); bool p = r.success; appendStep(steps, 89, "Parse SELECT multiple LIKE OR", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-89\n";}
    {auto r = parse("SELECT a, b, c, d, e FROM t WHERE a=1 AND b=2 AND c=3;"); bool p = r.success; appendStep(steps, 90, "Parse SELECT many cols + conditions", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-90\n";}
    {auto r = parse("SELECT * FROM t WHERE id NOT IN (SELECT id FROM t2);"); bool p = r.success; appendStep(steps, 91, "Parse SELECT NOT IN subquery", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-91\n";}
    {auto r = parse("SELECT * FROM t WHERE (id > 1 AND id < 100) OR (name = 'admin');"); bool p = r.success; appendStep(steps, 92, "Parse SELECT nested parentheses", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-92\n";}
    {auto r = parse("INSERT INTO t (id, name, age, salary) VALUES (1, 'test', 25, 50000.0);"); bool p = r.success; appendStep(steps, 93, "Parse INSERT 4 columns mixed", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-93\n";}
    {auto r = parse("INSERT INTO t VALUES (1, 'a', 2, 'b', 3);"); bool p = r.success; appendStep(steps, 94, "Parse INSERT 5 values", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-94\n";}
    {auto r = parse("UPDATE t SET a = 1, b = 2, c = 3, d = 4 WHERE id = 10;"); bool p = r.success; appendStep(steps, 95, "Parse UPDATE 4 SET columns WHERE", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-95\n";}
    {auto r = parse("UPDATE t SET val = 100 WHERE id IN (1, 2, 3, 4, 5);"); bool p = r.success; appendStep(steps, 96, "Parse UPDATE WHERE IN list", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-96\n";}
    {auto r = parse("DELETE FROM t WHERE id BETWEEN 10 AND 20;"); bool p = r.success; appendStep(steps, 97, "Parse DELETE WHERE BETWEEN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-97\n";}
    {auto r = parse("DELETE FROM t WHERE name IS NULL;"); bool p = r.success; appendStep(steps, 98, "Parse DELETE WHERE IS NULL", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-98\n";}
    {auto r = parse("SELECT MAX(price) FROM t;"); bool p = r.success; appendStep(steps, 99, "Parse SELECT MAX", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-99\n";}
    {auto r = parse("SELECT MIN(price) FROM t;"); bool p = r.success; appendStep(steps, 100, "Parse SELECT MIN", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-100\n";}
    {auto r = parse("SELECT id, name FROM t ORDER BY name ASC, id DESC;"); bool p = r.success; appendStep(steps, 101, "Parse SELECT ORDER BY multiple", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-101\n";}
    {auto r = parse("SELECT * FROM t WHERE age >= 18 AND age <= 60;"); bool p = r.success; appendStep(steps, 102, "Parse SELECT range AND", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-102\n";}
    {auto r = parse("SELECT * FROM t WHERE age > 18 OR age = 18;"); bool p = r.success; appendStep(steps, 103, "Parse SELECT OR + equality", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-103\n";}
    {auto r = parse("SHOW DATABASE mydb;"); bool p = r.success; appendStep(steps, 104, "Parse SHOW DATABASE specific", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-104\n";}
    {auto r = parse("SHOW TABLE mytable;"); bool p = r.success; appendStep(steps, 105, "Parse SHOW TABLE specific", p); std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " PS-105\n";}

    bool overall = std::all_of(steps.begin(), steps.end(), [](auto &s){ return s.passed; });
    double pct = gTotal > 0 ? 100.0*gPassed/gTotal : 0;
    std::cout << "\nResults: " << gPassed << "/" << gTotal << " (" << pct << "%)\nOverall: " << (overall?"PASS":"FAIL") << "\n";
    if (!overall) for (auto &s : steps) if (!s.passed) std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";
    writeReportLog("ParserTest", steps);
    return overall ? 0 : 1;
}
