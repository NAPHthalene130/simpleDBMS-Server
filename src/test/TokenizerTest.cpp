/**
 * @file TokenizerTest.cpp
 * @brief SQL词法分析器测试
 * @details 测试Tokenizer对各种SQL关键字、标识符、数字、字符串、运算符、符号、
 *          注释、空白字符的分词能力，覆盖正常和异常输入。
 * @author NAPH130
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

#include "Core.h"
#include "tokenizer/Tokenizer.h"
#include "models/tokenizer/Token.h"

namespace {

struct TestStepResult {
    int id;
    std::string name;
    bool passed;
    std::string detail;
};

int gTotal = 0, gPassed = 0;

void appendStep(std::vector<TestStepResult> &s, int id, const std::string &name, bool p, const std::string &d = "") {
    ++gTotal; if (p) ++gPassed;
    s.push_back({id, name, p, d});
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
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void writeReportLog(const std::string &suite, const std::vector<TestStepResult> &steps) {
    std::filesystem::create_directories("test");
    std::ofstream ofs("test/report.log", std::ios::app);
    if (!ofs.good()) return;
    ofs << "====================\n";
    ofs << suite << "\n";
    ofs << nowStr() << "\n";
    ofs << gPassed << "/" << gTotal << "\n";
    for (auto &s : steps) {
        ofs << "[" << (s.passed ? "YES" : "NO") << "]" << s.name << "\n";
    }
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

    // ====== 1. Keywords ======
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

    // ====== 2. Identifiers ======
    check(33, "myTable", {{SqlTokenType::Identifier, "myTable"}, {SqlTokenType::EndOfFile, ""}});
    check(34, "user_name", {{SqlTokenType::Identifier, "user_name"}, {SqlTokenType::EndOfFile, ""}});
    check(35, "_private", {{SqlTokenType::Identifier, "_private"}, {SqlTokenType::EndOfFile, ""}});
    check(36, "id123", {{SqlTokenType::Identifier, "id123"}, {SqlTokenType::EndOfFile, ""}});

    // ====== 3. Numbers ======
    check(37, "123", {{SqlTokenType::Number, "123"}, {SqlTokenType::EndOfFile, ""}});
    check(38, "0", {{SqlTokenType::Number, "0"}, {SqlTokenType::EndOfFile, ""}});
    check(39, "123.456", {{SqlTokenType::Number, "123.456"}, {SqlTokenType::EndOfFile, ""}});

    // ====== 4. Strings ======
    check(40, "'hello'", {{SqlTokenType::String, "hello"}, {SqlTokenType::EndOfFile, ""}});
    check(41, "\"world\"", {{SqlTokenType::String, "world"}, {SqlTokenType::EndOfFile, ""}});
    check(42, "''", {{SqlTokenType::String, ""}, {SqlTokenType::EndOfFile, ""}});
    check(43, "'it''s'", {{SqlTokenType::String, "it's"}, {SqlTokenType::EndOfFile, ""}});

    // ====== 5. Operators ======
    check(44, "=", {{SqlTokenType::Operator, "="}, {SqlTokenType::EndOfFile, ""}});
    check(45, "<>", {{SqlTokenType::Operator, "<>"}, {SqlTokenType::EndOfFile, ""}});
    check(46, "<=", {{SqlTokenType::Operator, "<="}, {SqlTokenType::EndOfFile, ""}});
    check(47, ">=", {{SqlTokenType::Operator, ">="}, {SqlTokenType::EndOfFile, ""}});
    check(48, "!=", {{SqlTokenType::Operator, "!="}, {SqlTokenType::EndOfFile, ""}});
    check(49, "||", {{SqlTokenType::Operator, "||"}, {SqlTokenType::EndOfFile, ""}});
    check(50, "&&", {{SqlTokenType::Operator, "&&"}, {SqlTokenType::EndOfFile, ""}});
    check(51, "+", {{SqlTokenType::Operator, "+"}, {SqlTokenType::EndOfFile, ""}});
    check(52, "-", {{SqlTokenType::Operator, "-"}, {SqlTokenType::EndOfFile, ""}});
    check(53, "*", {{SqlTokenType::Operator, "*"}, {SqlTokenType::EndOfFile, ""}});
    check(54, "/", {{SqlTokenType::Operator, "/"}, {SqlTokenType::EndOfFile, ""}});
    check(55, "<", {{SqlTokenType::Operator, "<"}, {SqlTokenType::EndOfFile, ""}});
    check(56, ">", {{SqlTokenType::Operator, ">"}, {SqlTokenType::EndOfFile, ""}});

    // ====== 6. Symbols ======
    check(57, ",", {{SqlTokenType::Symbol, ","}, {SqlTokenType::EndOfFile, ""}});
    check(58, ";", {{SqlTokenType::Symbol, ";"}, {SqlTokenType::EndOfFile, ""}});
    check(59, "(", {{SqlTokenType::Symbol, "("}, {SqlTokenType::EndOfFile, ""}});
    check(60, ")", {{SqlTokenType::Symbol, ")"}, {SqlTokenType::EndOfFile, ""}});
    check(61, ".", {{SqlTokenType::Symbol, "."}, {SqlTokenType::EndOfFile, ""}});

    // ====== 7. Comments ======
    check(62, "-- comment", {{SqlTokenType::EndOfFile, ""}});
    check(63, "/* comment */", {{SqlTokenType::EndOfFile, ""}});
    check(64, "SELECT /* inline */ 1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(65, "SELECT -- inline\n1", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::Number, "1"}, {SqlTokenType::EndOfFile, ""}});
    check(66, "/* multi\nline */SELECT", {{SqlTokenType::Keyword, "SELECT"}, {SqlTokenType::EndOfFile, ""}});

    // ====== 8. Empty/Whitespace ======
    check(67, "", {{SqlTokenType::EndOfFile, ""}});
    check(68, "   ", {{SqlTokenType::EndOfFile, ""}});
    check(69, "\t\n\r", {{SqlTokenType::EndOfFile, ""}});

    // ====== 9. Complete SQL statements ======
    check(70, "SELECT * FROM t;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(71, "CREATE TABLE t (id INT);",
          {{SqlTokenType::Keyword,"CREATE"},{SqlTokenType::Keyword,"TABLE"},{SqlTokenType::Identifier,"t"},
           {SqlTokenType::Symbol,"("},{SqlTokenType::Identifier,"id"},{SqlTokenType::Keyword,"INT"},
           {SqlTokenType::Symbol,")"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(72, "INSERT INTO t VALUES (1, 'a');",
          {{SqlTokenType::Keyword,"INSERT"},{SqlTokenType::Keyword,"INTO"},{SqlTokenType::Identifier,"t"},
           {SqlTokenType::Keyword,"VALUES"},{SqlTokenType::Symbol,"("},{SqlTokenType::Number,"1"},
           {SqlTokenType::Symbol,","},{SqlTokenType::String,"a"},{SqlTokenType::Symbol,")"},
           {SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});
    check(73, "SELECT * FROM t WHERE id >= 1;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Operator,"*"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"WHERE"},{SqlTokenType::Identifier,"id"},
           {SqlTokenType::Operator,">="},{SqlTokenType::Number,"1"},{SqlTokenType::Symbol,";"},
           {SqlTokenType::EndOfFile,""}});
    check(74, "SELECT COUNT(*) FROM t GROUP BY name HAVING COUNT(*) > 1;",
          {{SqlTokenType::Keyword,"SELECT"},{SqlTokenType::Keyword,"COUNT"},{SqlTokenType::Symbol,"("},
           {SqlTokenType::Operator,"*"},{SqlTokenType::Symbol,")"},{SqlTokenType::Keyword,"FROM"},
           {SqlTokenType::Identifier,"t"},{SqlTokenType::Keyword,"GROUP"},{SqlTokenType::Keyword,"BY"},
           {SqlTokenType::Identifier,"name"},{SqlTokenType::Keyword,"HAVING"},{SqlTokenType::Keyword,"COUNT"},
           {SqlTokenType::Symbol,"("},{SqlTokenType::Operator,"*"},{SqlTokenType::Symbol,")"},
           {SqlTokenType::Operator,">"},{SqlTokenType::Number,"1"},{SqlTokenType::Symbol,";"},
           {SqlTokenType::EndOfFile,""}});
    check(75, "USE DATABASE mydb;",
          {{SqlTokenType::Keyword,"USE"},{SqlTokenType::Keyword,"DATABASE"},
           {SqlTokenType::Identifier,"mydb"},{SqlTokenType::Symbol,";"},{SqlTokenType::EndOfFile,""}});

    bool overall = std::all_of(steps.begin(), steps.end(), [](auto &s){ return s.passed; });
    double pct = gTotal > 0 ? 100.0*gPassed/gTotal : 0;
    std::cout << "\nResults: " << gPassed << "/" << gTotal << " (" << pct << "%)\nOverall: " << (overall?"PASS":"FAIL") << "\n";
    if (!overall) for (auto &s : steps) if (!s.passed) std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";

    writeReportLog("TokenizerTest", steps);
    return overall ? 0 : 1;
}
