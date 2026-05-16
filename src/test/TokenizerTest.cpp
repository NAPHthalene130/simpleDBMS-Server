/**
 * @file TokenizerTest.cpp
 * @brief SQL词法分析器测试（扩展版）
 */
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>

#include "Core.h"
#include "tokenizer/Tokenizer.h"
#include "models/tokenizer/Token.h"

namespace {

struct TestStepResult { int id; std::string name; bool passed; std::string detail; };
int gTotal = 0, gPassed = 0;

void appendStep(std::vector<TestStepResult> &s, int id, const std::string &name, bool p, const std::string &d = "") {
    ++gTotal; if (p) ++gPassed; s.push_back({id, name, p, d});
}

std::string tokenTypeName(SqlTokenType t) {
    switch (t) {
        case SqlTokenType::Keyword: return "Keyword";
        case SqlTokenType::Identifier: return "Identifier";
        case SqlTokenType::Number: return "Number";
        case SqlTokenType::Operator: return "Operator";
        case SqlTokenType::String: return "String";
        case SqlTokenType::Symbol: return "Symbol";
        case SqlTokenType::EndOfFile: return "EndOfFile";
        case SqlTokenType::Unknown: return "Unknown";
        default: return "?";
    }
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
    std::cout << "\n========== Tokenizer Test ==========\n";

    auto check = [&](int id, const std::string &sql, const std::vector<std::pair<SqlTokenType, std::string>> &expected) {
        Tokenizer tk(&core, sql);
        auto tokens = tk.tokenize();
        bool p = tokens.size() >= expected.size();
        for (size_t i = 0; p && i < expected.size(); ++i) {
            if (i >= tokens.size()) { p = false; break; }
            if (tokens[i].getType() != expected[i].first) p = false;
            if (tokens[i].getValue() != expected[i].second) p = false;
        }
        std::string detail = p ? "ok" : "got " + std::to_string(tokens.size()) + " tokens";
        appendStep(steps, id, "TK-" + std::to_string(id) + " " + sql.substr(0, 40), p, detail);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " TK-" << id << "\n";
    };

    check(1, "SELECT", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::EndOfFile, ""}});
    check(2, "FROM", {{SqlTokenType::Keyword, "FROM"}, {SqlTokenType::EndOfFile, ""}});
    check(3, "WHERE", {{SqlTokenType::Keyword, "WHERE"}, {SqlTokenType::EndOfFile, ""}});
    check(4, "INSERT", {{SqlTokenType::Keyword, "INSERT"}, {SqlTokenType::EndOfFile, ""}});
    check(5, "INTO", {{SqlTokenType::Keyword, "INTO"}, {SqlTokenType::EndOfFile, ""}});
    check(6, "VALUES", {{SqlTokenType::Keyword, "VALUES"}, {SqlTokenType::EndOfFile, ""}});
    check(7, "DELETE", {{SqlTokenType::Keyword, "DELETE"}, {SqlTokenType::EndOfFile, ""}});
    check(8, "UPDATE", {{SqlTokenType::Keyword, "UPDATE"}, {SqlTokenType::EndOfFile, ""}});
    check(9, "SET", {{SqlTokenType::Keyword, "SET"}, {SqlTokenType::EndOfFile, ""}});
    check(10, "CREATE", {{SqlTokenType::Keyword, "CREATE"}, {SqlTokenType::EndOfFile, ""}});
    check(11, "TABLE", {{SqlTokenType::Keyword, "TABLE"}, {SqlTokenType::EndOfFile, ""}});
    check(12, "DATABASE", {{SqlTokenType::Keyword, "DATABASE"}, {SqlTokenType::EndOfFile, ""}});
    check(13, "DROP", {{SqlTokenType::Keyword, "DROP"}, {SqlTokenType::EndOfFile, ""}});
    check(14, "SHOW", {{SqlTokenType::Keyword, "SHOW"}, {SqlTokenType::EndOfFile, ""}});
    check(15, "USE", {{SqlTokenType::Keyword, "USE"}, {SqlTokenType::EndOfFile, ""}});
    check(16, "INT", {{SqlTokenType::Keyword, "INT"}, {SqlTokenType::EndOfFile, ""}});
    check(17, "VARCHAR", {{SqlTokenType::Keyword, "VARCHAR"}, {SqlTokenType::EndOfFile, ""}});
    check(18, "PRIMARY", {{SqlTokenType::Keyword, "PRIMARY"}, {SqlTokenType::EndOfFile, ""}});
    check(19, "KEY", {{SqlTokenType::Keyword, "KEY"}, {SqlTokenType::EndOfFile, ""}});
    check(20, "NOT", {{SqlTokenType::Keyword, "NOT"}, {SqlTokenType::EndOfFile, ""}});
    check(21, "NULL", {{SqlTokenType::Keyword, "NULL"}, {SqlTokenType::EndOfFile, ""}});
    check(22, "DEFAULT", {{SqlTokenType::Keyword, "DEFAULT"}, {SqlTokenType::EndOfFile, ""}});
    check(23, "ORDER", {{SqlTokenType::Keyword, "ORDER"}, {SqlTokenType::EndOfFile, ""}});
    check(24, "BY", {{SqlTokenType::Keyword, "BY"}, {SqlTokenType::EndOfFile, ""}});
    check(25, "GROUP", {{SqlTokenType::Keyword, "GROUP"}, {SqlTokenType::EndOfFile, ""}});
    check(26, "HAVING", {{SqlTokenType::Keyword, "HAVING"}, {SqlTokenType::EndOfFile, ""}});
    check(27, "JOIN", {{SqlTokenType::Keyword, "JOIN"}, {SqlTokenType::EndOfFile, ""}});
    check(28, "INNER", {{SqlTokenType::Keyword, "INNER"}, {SqlTokenType::EndOfFile, ""}});
    check(29, "LEFT", {{SqlTokenType::Keyword, "LEFT"}, {SqlTokenType::EndOfFile, ""}});
    check(30, "RIGHT", {{SqlTokenType::Keyword, "RIGHT"}, {SqlTokenType::EndOfFile, ""}});
    check(31, "ON", {{SqlTokenType::Keyword, "ON"}, {SqlTokenType::EndOfFile, ""}});
    check(32, "AS", {{SqlTokenType::Keyword, "AS"}, {SqlTokenType::EndOfFile, ""}});
    check(33, "AND", {{SqlTokenType::Keyword, "AND"}, {SqlTokenType::EndOfFile, ""}});
    check(34, "OR", {{SqlTokenType::Keyword, "OR"}, {SqlTokenType::EndOfFile, ""}});
    check(35, "IN", {{SqlTokenType::Keyword, "IN"}, {SqlTokenType::EndOfFile, ""}});
    check(36, "EXISTS", {{SqlTokenType::Keyword, "EXISTS"}, {SqlTokenType::EndOfFile, ""}});
    check(37, "UNION", {{SqlTokenType::Keyword, "UNION"}, {SqlTokenType::EndOfFile, ""}});
    check(38, "ALL", {{SqlTokenType::Keyword, "ALL"}, {SqlTokenType::EndOfFile, ""}});
    check(39, "ALTER", {{SqlTokenType::Keyword, "ALTER"}, {SqlTokenType::EndOfFile, ""}});
    check(40, "TRUNCATE", {{SqlTokenType::Keyword, "TRUNCATE"}, {SqlTokenType::EndOfFile, ""}});
    check(41, "LIKE", {{SqlTokenType::Keyword, "LIKE"}, {SqlTokenType::EndOfFile, ""}});
    check(42, "BETWEEN", {{SqlTokenType::Keyword, "BETWEEN"}, {SqlTokenType::EndOfFile, ""}});
    check(43, "COUNT", {{SqlTokenType::Keyword, "COUNT"}, {SqlTokenType::EndOfFile, ""}});
    check(44, "SUM", {{SqlTokenType::Keyword, "SUM"}, {SqlTokenType::EndOfFile, ""}});
    check(45, "AVG", {{SqlTokenType::Keyword, "AVG"}, {SqlTokenType::EndOfFile, ""}});
    check(46, "MIN", {{SqlTokenType::Keyword, "MIN"}, {SqlTokenType::EndOfFile, ""}});
    check(47, "MAX", {{SqlTokenType::Keyword, "MAX"}, {SqlTokenType::EndOfFile, ""}});
    check(48, "LIMIT", {{SqlTokenType::Keyword, "LIMIT"}, {SqlTokenType::EndOfFile, ""}});
    check(49, "DISTINCT", {{SqlTokenType::Keyword, "DISTINCT"}, {SqlTokenType::EndOfFile, ""}});
    check(50, "ASC", {{SqlTokenType::Keyword, "ASC"}, {SqlTokenType::EndOfFile, ""}});
    check(51, "DESC", {{SqlTokenType::Keyword, "DESC"}, {SqlTokenType::EndOfFile, ""}});
    check(52, "UNIQUE", {{SqlTokenType::Keyword, "UNIQUE"}, {SqlTokenType::EndOfFile, ""}});
    check(53, "AUTO_INCREMENT", {{SqlTokenType::Keyword, "AUTO_INCREMENT"}, {SqlTokenType::EndOfFile, ""}});
    check(54, "BOOLEAN", {{SqlTokenType::Keyword, "BOOLEAN"}, {SqlTokenType::EndOfFile, ""}});
    check(55, "FALSE", {{SqlTokenType::Keyword, "FALSE"}, {SqlTokenType::EndOfFile, ""}});
    check(56, "TRUE", {{SqlTokenType::Keyword, "TRUE"}, {SqlTokenType::EndOfFile, ""}});
    check(57, "SELECT", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::EndOfFile, ""}});
    check(58, "SELECT", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::EndOfFile, ""}});
    check(59, "CREATE TABLE", {{SqlTokenType::Keyword, "CREATE"}, {SqlTokenType::Keyword, "TABLE"}, {SqlTokenType::EndOfFile, ""}});
    check(60, "myTable", {{SqlTokenType::Identifier, "myTable"}, {SqlTokenType::EndOfFile, ""}});
    check(61, "user_name", {{SqlTokenType::Identifier, "user_name"}, {SqlTokenType::EndOfFile, ""}});
    check(62, "_private", {{SqlTokenType::Identifier, "_private"}, {SqlTokenType::EndOfFile, ""}});
    check(63, "id123", {{SqlTokenType::Identifier, "id123"}, {SqlTokenType::EndOfFile, ""}});
    check(64, "a", {{SqlTokenType::Identifier, "a"}, {SqlTokenType::EndOfFile, ""}});
    check(65, "col_name_1", {{SqlTokenType::Identifier, "col_name_1"}, {SqlTokenType::EndOfFile, ""}});
    check(66, "t1", {{SqlTokenType::Identifier, "t1"}, {SqlTokenType::EndOfFile, ""}});
    check(67, "__double_underscore", {{SqlTokenType::Identifier, "__double_underscore"}, {SqlTokenType::EndOfFile, ""}});
    check(68, "t1234567890", {{SqlTokenType::Identifier, "t1234567890"}, {SqlTokenType::EndOfFile, ""}});
    check(69, "123", {{SqlTokenType::Number, "123"}, {SqlTokenType::EndOfFile, ""}});
    check(70, "0", {{SqlTokenType::Number, "0"}, {SqlTokenType::EndOfFile, ""}});
    check(71, "123.456", {{SqlTokenType::Number, "123.456"}, {SqlTokenType::EndOfFile, ""}});
    check(72, "0.5", {{SqlTokenType::Number, "0.5"}, {SqlTokenType::EndOfFile, ""}});
    check(73, "1000000", {{SqlTokenType::Number, "1000000"}, {SqlTokenType::EndOfFile, ""}});
    check(74, "00", {{SqlTokenType::Number, "00"}, {SqlTokenType::EndOfFile, ""}});
    check(75, "3.14159", {{SqlTokenType::Number, "3.14159"}, {SqlTokenType::EndOfFile, ""}});
    check(76, "999", {{SqlTokenType::Number, "999"}, {SqlTokenType::EndOfFile, ""}});
    check(77, "'hello'", {{SqlTokenType::String, "hello"}, {SqlTokenType::EndOfFile, ""}});
    check(78, "\"world\"", {{SqlTokenType::String, "world"}, {SqlTokenType::EndOfFile, ""}});
    check(79, "''", {{SqlTokenType::String, ""}, {SqlTokenType::EndOfFile, ""}});
    check(80, "'it''s'", {{SqlTokenType::String, "it's"}, {SqlTokenType::EndOfFile, ""}});
    check(81, "'a'", {{SqlTokenType::String, "a"}, {SqlTokenType::EndOfFile, ""}});
    check(82, "\"\"", {{SqlTokenType::String, ""}, {SqlTokenType::EndOfFile, ""}});
    check(83, "'hello world'", {{SqlTokenType::String, "hello world"}, {SqlTokenType::EndOfFile, ""}});
    check(84, "'123'", {{SqlTokenType::String, "123"}, {SqlTokenType::EndOfFile, ""}});
    check(85, "'_underscore_string_'", {{SqlTokenType::String, "_underscore_string_"}, {SqlTokenType::EndOfFile, ""}});
    check(86, "=", {{SqlTokenType::Operator, "="}, {SqlTokenType::EndOfFile, ""}});
    check(87, "<>", {{SqlTokenType::Operator, "<>"}, {SqlTokenType::EndOfFile, ""}});
    check(88, "<=", {{SqlTokenType::Operator, "<="}, {SqlTokenType::EndOfFile, ""}});
    check(89, ">=", {{SqlTokenType::Operator, ">="}, {SqlTokenType::EndOfFile, ""}});
    check(90, "!=", {{SqlTokenType::Operator, "!="}, {SqlTokenType::EndOfFile, ""}});
    check(91, "||", {{SqlTokenType::Operator, "||"}, {SqlTokenType::EndOfFile, ""}});
    check(92, "&&", {{SqlTokenType::Operator, "&&"}, {SqlTokenType::EndOfFile, ""}});
    check(93, "+", {{SqlTokenType::Operator, "+"}, {SqlTokenType::EndOfFile, ""}});
    check(94, "-", {{SqlTokenType::Operator, "-"}, {SqlTokenType::EndOfFile, ""}});
    check(95, "*", {{SqlTokenType::Operator, "*"}, {SqlTokenType::EndOfFile, ""}});
    check(96, "/", {{SqlTokenType::Operator, "/"}, {SqlTokenType::EndOfFile, ""}});
    check(97, "<", {{SqlTokenType::Operator, "<"}, {SqlTokenType::EndOfFile, ""}});
    check(98, ">", {{SqlTokenType::Operator, ">"}, {SqlTokenType::EndOfFile, ""}});
    check(99, "%", {{SqlTokenType::Operator, "%"}, {SqlTokenType::EndOfFile, ""}});
    check(100, "~", {{SqlTokenType::Operator, "~"}, {SqlTokenType::EndOfFile, ""}});
    check(101, "&", {{SqlTokenType::Operator, "&"}, {SqlTokenType::EndOfFile, ""}});
    check(102, "|", {{SqlTokenType::Operator, "|"}, {SqlTokenType::EndOfFile, ""}});
    check(103, "^", {{SqlTokenType::Operator, "^"}, {SqlTokenType::EndOfFile, ""}});
    check(104, "<<", {{SqlTokenType::Operator, "<<"}, {SqlTokenType::EndOfFile, ""}});
    check(105, ">>", {{SqlTokenType::Operator, ">>"}, {SqlTokenType::EndOfFile, ""}});
    check(106, ",", {{SqlTokenType::Symbol, ","}, {SqlTokenType::EndOfFile, ""}});
    check(107, ";", {{SqlTokenType::Symbol, ";"}, {SqlTokenType::EndOfFile, ""}});
    check(108, "(", {{SqlTokenType::Symbol, "("}, {SqlTokenType::EndOfFile, ""}});
    check(109, ")", {{SqlTokenType::Symbol, ")"}, {SqlTokenType::EndOfFile, ""}});
    check(110, ".", {{SqlTokenType::Symbol, "."}, {SqlTokenType::EndOfFile, ""}});
    check(111, ":", {{SqlTokenType::Symbol, ":"}, {SqlTokenType::EndOfFile, ""}});
    check(112, "?", {{SqlTokenType::Symbol, "?"}, {SqlTokenType::EndOfFile, ""}});
    check(113, "-- comment", {{SqlTokenType::EndOfFile, ""}});
    check(114, "/* comment */", {{SqlTokenType::EndOfFile, ""}});
    check(115, "SELECT /* inline */ 1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(116, "SELECT -- inline\n1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(117, "/* multi\nline */SELECT", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::EndOfFile, ""}});
    check(118, "--", {{SqlTokenType::EndOfFile, ""}});
    check(119, "/**/", {{SqlTokenType::EndOfFile, ""}});
    check(120, "SELECT/**/1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(121, "SELECT--comment\n1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(122, "/*comment*/SELECT/*c2*/1/*c3*/", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(123, "", {{SqlTokenType::EndOfFile, ""}});
    check(124, "   ", {{SqlTokenType::EndOfFile, ""}});
    check(125, "\t\n\r", {{SqlTokenType::EndOfFile, ""}});
    check(126, " \t \n \r ", {{SqlTokenType::EndOfFile, ""}});
    check(127, "\n\n\n", {{SqlTokenType::EndOfFile, ""}});
    check(128, "\t\t", {{SqlTokenType::EndOfFile, ""}});
    check(129, "a.b", {{SqlTokenType::Identifier,"a"},{SqlTokenType::Symbol,"."},{SqlTokenType::Identifier,"b"},{SqlTokenType::EndOfFile,""}});
    check(130, "a.b.c", {{SqlTokenType::Identifier,"a"},{SqlTokenType::Symbol,"."},{SqlTokenType::Identifier,"b"},{SqlTokenType::Symbol,"."},{SqlTokenType::Identifier,"c"},{SqlTokenType::EndOfFile,""}});
    check(131, "1+2", {{SqlTokenType::Number,"1"},{SqlTokenType::Operator,"+"},{SqlTokenType::Number,"2"},{SqlTokenType::EndOfFile,""}});
    check(132, "a>=b", {{SqlTokenType::Identifier,"a"},{SqlTokenType::Operator,">="},{SqlTokenType::Identifier,"b"},{SqlTokenType::EndOfFile,""}});
    check(133, "a<>b", {{SqlTokenType::Identifier,"a"},{SqlTokenType::Operator,"<>"},{SqlTokenType::Identifier,"b"},{SqlTokenType::EndOfFile,""}});
    check(134, "1-2*3", {{SqlTokenType::Number,"1"},{SqlTokenType::Operator,"-"},{SqlTokenType::Number,"2"},{SqlTokenType::Operator,"*"},{SqlTokenType::Number,"3"},{SqlTokenType::EndOfFile,""}});
    check(135, "SELECT * FROM t;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(136, "CREATE TABLE t (id INT);",
          {{SqlTokenType::Keyword,"CREATE"},{SqlTokenType::Keyword,"TABLE"},{SqlTokenType::Identifier,"t"},
           {SqlTokenType::Symbol,"("},{SqlTokenType::Identifier,"id"},{SqlTokenType::Keyword,"INT"},
           {SqlTokenType::Symbol,")"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(137, "INSERT INTO t VALUES (1, 'a');",
          {{SqlTokenType::Keyword,"INSERT"},{SqlTokenType::Keyword,"INTO"},{SqlTokenType::Identifier,"t"},
           {SqlTokenType::Keyword,"VALUES"},{SqlTokenType::Symbol,"("},{SqlTokenType::Number,"1"},
           {SqlTokenType::Symbol,","},{SqlTokenType::String,"a"},{SqlTokenType::Symbol,")"},
           {SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(138, "SELECT * FROM t WHERE id >= 1;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"id"},
           {SqlTokenType::Operator,">="},{SqlTokenType::Number,"1"},{SqlTokenType::Symbol,";"},
           {SqlTokenType::EndOfFile,""}});
    check(139, "SELECT COUNT(*) FROM t GROUP BY name HAVING COUNT(*) > 1;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Keyword,"COUNT"},{SqlTokenType::Symbol,"("},
           {SqlTokenType::Operator,"*"},{SqlTokenType::Symbol,")"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"GROUP"},{SqlTokenType::Keyword,"BY"},
           {SqlTokenType::Identifier,"name"},{SqlTokenType::Keyword,"HAVING"},{SqlTokenType::Keyword,"COUNT"},
           {SqlTokenType::Symbol,"("},{SqlTokenType::Operator,"*"},{SqlTokenType::Symbol,")"},
           {SqlTokenType::Operator,">"},{SqlTokenType::Number,"1"},{SqlTokenType::Symbol,";"},
           {SqlTokenType::EndOfFile,""}});
    check(140, "USE DATABASE mydb;",
          {{SqlTokenType::Keyword,"USE"},{SqlTokenType::Keyword,"DATABASE"},
           {SqlTokenType::Identifier,"mydb"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(141, "SELECT DISTINCT name FROM users;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Keyword,"DISTINCT"},{SqlTokenType::Identifier,"name"},
           {SqlTokenType::Keyword,"FROM"},{SqlTokenType::Identifier,"users"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(142, "UPDATE users SET age = 30 WHERE id = 1;",
          {{SqlTokenType::Keyword,"UPDATE"},{SqlTokenType::Identifier,"users"},{SqlTokenType::Keyword,"SET"},
           {SqlTokenType::Identifier,"age"},{SqlTokenType::Operator,"="},{SqlTokenType::Number,"30"},
           {SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"id"},{SqlTokenType::Operator,"="},
           {SqlTokenType::Number,"1"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(143, "DELETE FROM users WHERE id = 5;",
          {{SqlTokenType::Keyword,"DELETE"},{SqlTokenType::Keyword,"FROM"},{SqlTokenType::Identifier,"users"},
           {SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"id"},{SqlTokenType::Operator,"="},
           {SqlTokenType::Number,"5"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(144, "SELECT * FROM t WHERE name LIKE 'A%';",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"name"},
           {SqlTokenType::Keyword,"LIKE"},{SqlTokenType::String,"A%"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(145, "SELECT * FROM t WHERE id IN (1, 2, 3);",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"id"},
           {SqlTokenType::Keyword,"IN"},{SqlTokenType::Symbol,"("},{SqlTokenType::Number,"1"},
           {SqlTokenType::Symbol,","},{SqlTokenType::Number,"2"},{SqlTokenType::Symbol,","},
           {SqlTokenType::Number,"3"},{SqlTokenType::Symbol,")"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(146, "SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t1"},{SqlTokenType::Keyword,"INNER"},{SqlTokenType::Keyword,"JOIN"},
           {SqlTokenType::Identifier,"t2"},{SqlTokenType::Keyword,"ON"},{SqlTokenType::Identifier,"t1"},
           {SqlTokenType::Symbol,"."},{SqlTokenType::Identifier,"id"},{SqlTokenType::Operator,"="},
           {SqlTokenType::Identifier,"t2"},{SqlTokenType::Symbol,"."},{SqlTokenType::Identifier,"id"},
           {SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(147, "SELECT id FROM t ORDER BY id DESC LIMIT 10;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Identifier,"id"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"ORDER"},{SqlTokenType::Keyword,"BY"},
           {SqlTokenType::Identifier,"id"},{SqlTokenType::Keyword,"DESC"},{SqlTokenType::Keyword,"LIMIT"},
           {SqlTokenType::Number,"10"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(148, "SELECT a FROM t WHERE a BETWEEN 1 AND 10;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Identifier,"a"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"a"},
           {SqlTokenType::Keyword,"BETWEEN"},{SqlTokenType::Number,"1"},{SqlTokenType::Keyword,"AND"},
           {SqlTokenType::Number,"10"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(149, "SELECT a AS alias FROM t;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Identifier,"a"},{SqlTokenType::Keyword,"AS"},
           {SqlTokenType::Identifier,"alias"},{SqlTokenType::Keyword,"FROM"},{SqlTokenType::Identifier,"t"},
           {SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(150, "SHOW TABLES;",
          {{SqlTokenType::Keyword,"SHOW"},{SqlTokenType::Keyword,"TABLES"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(151, "SHOW DATABASES;",
          {{SqlTokenType::Keyword,"SHOW"},{SqlTokenType::Keyword,"DATABASES"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(152, "DROP TABLE t;",
          {{SqlTokenType::Keyword,"DROP"},{SqlTokenType::Keyword,"TABLE"},{SqlTokenType::Identifier,"t"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(153, "DROP DATABASE db;",
          {{SqlTokenType::Keyword,"DROP"},{SqlTokenType::Keyword,"DATABASE"},{SqlTokenType::Identifier,"db"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(154, "TRUNCATE TABLE t;",
          {{SqlTokenType::Keyword,"TRUNCATE"},{SqlTokenType::Keyword,"TABLE"},{SqlTokenType::Identifier,"t"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(155, "SELECT COUNT(*) FROM t1 UNION SELECT COUNT(*) FROM t2;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Keyword,"COUNT"},{SqlTokenType::Symbol,"("},
           {SqlTokenType::Operator,"*"},{SqlTokenType::Symbol,")"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t1"},{SqlTokenType::Keyword,"UNION"},{SqlTokenType::Keyword,"SELECT"},
           {SqlTokenType::Keyword,"COUNT"},{SqlTokenType::Symbol,"("},{SqlTokenType::Operator,"*"},
           {SqlTokenType::Symbol,")"},{SqlTokenType::Keyword,"FROM"},{SqlTokenType::Identifier,"t2"},
           {SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(156, "@", {{SqlTokenType::Unknown, "@"}, {SqlTokenType::EndOfFile, ""}});
    check(157, "#", {{SqlTokenType::Unknown, "#"}, {SqlTokenType::EndOfFile, ""}});
    check(158, "`backtick`", {{SqlTokenType::Unknown, "`"}, {SqlTokenType::Identifier,"backtick"}, {SqlTokenType::Unknown, "`"}, {SqlTokenType::EndOfFile, ""}});
    check(159, "SELECT @var", {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Unknown,"@"},{SqlTokenType::Identifier,"var"},{SqlTokenType::EndOfFile,""}});

    {
        Tokenizer tk(&core, "SELECT 1");
        bool p = tk.hasMoreTokens() && tk.peekToken().getType() == SqlTokenType::Keyword && tk.peekToken().getValue() == "SELECT" && tk.getCurrentPosition() == 0;
        tk.nextToken(); p = p && tk.getCurrentPosition() > 0;
        appendStep(steps, 160, "TK-160 peekToken/hasMoreTokens/position", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " TK-160\n";
    }
    {
        Tokenizer tk(&core, "");
        bool p = !tk.hasMoreTokens();
        appendStep(steps, 161, "TK-161 hasMoreTokens false on empty input", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " TK-161\n";
    }
    {
        Tokenizer tk(&core, "   ");
        bool p = tk.tokenize().size() == 1 && tk.tokenize()[0].getType() == SqlTokenType::EndOfFile;
        appendStep(steps, 162, "TK-162 only whitespace returns EOF", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " TK-162\n";
    }
    {
        Tokenizer tk(&core, "SELECT");
        tk.reset("FROM");
        auto tokens = tk.tokenize();
        bool p = tokens.size() >= 1 && tokens[0].getType() == SqlTokenType::Keyword && tokens[0].getValue() == "FROM";
        appendStep(steps, 163, "TK-163 reset() changes input", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " TK-163\n";
    }

    bool overall = std::all_of(steps.begin(), steps.end(), [](auto &s){ return s.passed; });
    double pct = gTotal > 0 ? 100.0*gPassed/gTotal : 0;
    std::cout << "\nResults: " << gPassed << "/" << gTotal << " (" << pct << "%)\nOverall: " << (overall?"PASS":"FAIL") << "\n";
    if (!overall) for (auto &s : steps) if (!s.passed) std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";
    writeReportLog("TokenizerTest", steps);
    return overall ? 0 : 1;
}
