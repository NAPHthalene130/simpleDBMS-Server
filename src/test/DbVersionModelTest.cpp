/**
 * @file DbVersionModelTest.cpp
 * @brief Database version model layer unit test
 * @details Tests NetworkTransferData, DatabaseNode dbVersion field serialization/deserialization,
 *          DB_VERSION_REQUEST/RESPONSE constants, defaults, JSON round-trip consistency.
 * @author NAPH130
 */
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "models/network/NetworkTransferData.h"

namespace {

struct TestStepResult {
    int id;
    std::string name;
    bool passed;
    std::string detail;
};

int gTotalTests = 0;
int gPassedTests = 0;

void appendStep(std::vector<TestStepResult> &steps, int id, const std::string &name,
                bool passed, const std::string &detail = "") {
    ++gTotalTests;
    if (passed) ++gPassedTests;
    steps.push_back({id, name, passed, detail});
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
    ofs << "====================\n" << suite << "\n" << nowStr() << "\n"
        << gPassedTests << "/" << gTotalTests << "\n";
    for (auto &s : steps) {
        ofs << "[" << (s.passed ? "YES" : "NO") << "]" << s.name << "\n";
    }
}

} // namespace

int main() {
    std::vector<TestStepResult> steps;
    bool overall = true;
    int seq = 1;

    std::cout << "\n========== DbVersion Model Test ==========\n";

    // ====================== NetworkTransferData dbVersion (30+) ======================
    {
        NetworkTransferData data;
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, seq++, "NetworkTransferData default dbVersion=0", p,
                   p ? "ok" : "got " + std::to_string(data.getDbVersion()));
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(42);
        bool p = (data.getDbVersion() == 42);
        appendStep(steps, seq++, "setDbVersion/getDbVersion normal value", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(100);
        data.setDbVersion(0);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, seq++, "setDbVersion 0 after non-zero", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(UINT64_MAX);
        bool p = (data.getDbVersion() == UINT64_MAX);
        appendStep(steps, seq++, "setDbVersion UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1);
        bool p = (data.getDbVersion() == 1);
        appendStep(steps, seq++, "setDbVersion minimum non-zero", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1234567890123ULL);
        bool p = (data.getDbVersion() == 1234567890123ULL);
        appendStep(steps, seq++, "setDbVersion large value", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(UINT64_MAX - 1);
        bool p = (data.getDbVersion() == UINT64_MAX - 1);
        appendStep(steps, seq++, "setDbVersion UINT64_MAX-1", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(0);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, seq++, "setDbVersion explicit 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(999999);
        data.setDbVersion(1);
        bool p = (data.getDbVersion() == 1);
        appendStep(steps, seq++, "setDbVersion overwrite smaller", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1);
        data.setDbVersion(999999);
        bool p = (data.getDbVersion() == 999999);
        appendStep(steps, seq++, "setDbVersion overwrite larger", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        data1.setDbVersion(0);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 0);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersion=0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        data1.setDbVersion(12345);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 12345);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersion=12345", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        data1.setDbVersion(UINT64_MAX);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == UINT64_MAX);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersion=UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        data1.setDbVersion(777);
        std::string json = data1.toJson();
        bool p = (json.find("\"dbVersion\"") != std::string::npos);
        appendStep(steps, seq++, "JSON contains dbVersion field", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::string oldJson = R"({"type":"TEST","id":"test","password":"","dbName":"","sql":"","success":false,"message":"","affectedRows":0,"columns":[],"rows":[],"databases":[]})";
        NetworkTransferData data = NetworkTransferData::fromJson(oldJson);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, seq++, "Old JSON without dbVersion defaults to 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(555);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersion\":555") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersion value serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(UINT64_MAX);
        std::string json = data.toJson();
        bool p = (json.find("18446744073709551615") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersion UINT64_MAX serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, "user1");
        req.setDbVersion(10);
        std::string json = req.toJson();
        NetworkTransferData req2 = NetworkTransferData::fromJson(json);
        bool p = (req2.getDbVersion() == 10 && req2.getType() == NetworkTransferData::DB_VERSION_REQUEST);
        appendStep(steps, seq++, "DB_VERSION_REQUEST with dbVersion roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::DB_VERSION_RESPONSE, "user1");
        resp.setSuccess(true);
        resp.setDbVersion(20);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersion() == 20 && resp2.getSuccess());
        appendStep(steps, seq++, "DB_VERSION_RESPONSE with dbVersion roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, "user1");
        req.setDbName("test_db");
        req.setSql("SELECT * FROM t;");
        req.setDbVersion(42);
        std::string json = req.toJson();
        NetworkTransferData req2 = NetworkTransferData::fromJson(json);
        bool p = (req2.getDbVersion() == 42 && req2.getDbName() == "test_db");
        appendStep(steps, seq++, "SQL_EXEC_REQUEST full roundtrip with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        resp.setSuccess(true);
        resp.setMessage("Query OK.");
        resp.setAffectedRows(0);
        resp.setDbName("test_db");
        resp.setDbVersion(43);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersion() == 43 && resp2.getSuccess());
        appendStep(steps, seq++, "SQL_EXEC_RESPONSE full roundtrip with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::SQL_QUERY_RESPONSE, "user1");
        resp.setSuccess(true);
        resp.setDbVersion(50);
        std::vector<std::vector<std::string>> rows = {{"1", "a"}, {"2", "b"}};
        resp.setRows(rows);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersion() == 50 && resp2.getRows().size() == 2);
        appendStep(steps, seq++, "SQL_QUERY_RESPONSE with dbVersion and rows roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::DIRECTORY_RESPONSE, "");
        resp.setSuccess(true);
        resp.setDbVersion(0);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersion() == 0 && resp2.getType() == NetworkTransferData::DIRECTORY_RESPONSE);
        appendStep(steps, seq++, "DIRECTORY_RESPONSE with dbVersion=0 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1);
        data.setDbVersion(2);
        data.setDbVersion(3);
        bool p = (data.getDbVersion() == 3);
        appendStep(steps, seq++, "Multiple sequential setDbVersion calls", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(100);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersion\":100") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersion 100 serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(0);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersion\":0") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersion 0 serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(999);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 999);
        appendStep(steps, seq++, "dbVersion 999 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1);
        data.setDbVersion(UINT64_MAX);
        bool p = (data.getDbVersion() == UINT64_MAX);
        appendStep(steps, seq++, "setDbVersion to max after min", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(UINT64_MAX);
        data.setDbVersion(0);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, seq++, "setDbVersion to 0 after max", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(500);
        data.setDbVersion(500);
        bool p = (data.getDbVersion() == 500);
        appendStep(steps, seq++, "setDbVersion same value twice", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(123);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersion\"") != std::string::npos);
        appendStep(steps, seq++, "dbVersion field present in JSON for all values", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }

    // ====================== DatabaseNode dbVersion (20+) ======================
    {
        DatabaseNode node;
        bool p = (node.getDbVersion() == 0);
        appendStep(steps, seq++, "DatabaseNode default dbVersion=0", p,
                   p ? "ok" : "got " + std::to_string(node.getDbVersion()));
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(999);
        bool p = (node.getDbVersion() == 999);
        appendStep(steps, seq++, "DatabaseNode setDbVersion/getDbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node1("test_db", {});
        node1.setDbVersion(555);
        std::string json = node1.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 555) && (node2.getName() == "test_db");
        appendStep(steps, seq++, "DatabaseNode toJson/fromJson dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::string oldJson = R"({"name":"old_db","tables":[]})";
        DatabaseNode node = DatabaseNode::fromJson(oldJson);
        bool p = (node.getDbVersion() == 0);
        appendStep(steps, seq++, "DatabaseNode old JSON dbVersion defaults to 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node("vdb", {});
        node.setDbVersion(100);
        std::string json = node.toJson();
        bool p = (json.find("\"dbVersion\"") != std::string::npos);
        appendStep(steps, seq++, "DatabaseNode JSON contains dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(UINT64_MAX);
        bool p = (node.getDbVersion() == UINT64_MAX);
        appendStep(steps, seq++, "DatabaseNode setDbVersion UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(0);
        bool p = (node.getDbVersion() == 0);
        appendStep(steps, seq++, "DatabaseNode setDbVersion explicit 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(1);
        node.setDbVersion(2);
        bool p = (node.getDbVersion() == 2);
        appendStep(steps, seq++, "DatabaseNode sequential setDbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::vector<TableNode> tables;
        tables.emplace_back("t1", std::vector<std::string>{"a", "b"});
        DatabaseNode node("db", tables);
        node.setDbVersion(88);
        bool p = (node.getName() == "db" && node.getTables().size() == 1
               && node.getDbVersion() == 88
               && node.getTables()[0].getFields().size() == 2);
        appendStep(steps, seq++, "DatabaseNode setDbVersion preserves other fields", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node("empty_db", {});
        node.setDbVersion(77);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 77 && node2.getTables().empty());
        appendStep(steps, seq++, "DatabaseNode with empty tables roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setName("mydb");
        node.setDbVersion(33);
        std::string json = node.toJson();
        bool p = (json.find("\"name\":\"mydb\"") != std::string::npos && json.find("\"dbVersion\":33") != std::string::npos);
        appendStep(steps, seq++, "DatabaseNode JSON name and dbVersion both present", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(123456789);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 123456789);
        appendStep(steps, seq++, "DatabaseNode large dbVersion roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(UINT64_MAX - 1);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == UINT64_MAX - 1);
        appendStep(steps, seq++, "DatabaseNode UINT64_MAX-1 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::string json = R"({"name":"compat","tables":[],"dbVersion":42})";
        DatabaseNode node = DatabaseNode::fromJson(json);
        bool p = (node.getDbVersion() == 42 && node.getName() == "compat");
        appendStep(steps, seq++, "DatabaseNode fromJson with dbVersion field", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(0);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 0);
        appendStep(steps, seq++, "DatabaseNode dbVersion=0 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(1);
        node.setDbVersion(0);
        node.setDbVersion(999);
        bool p = (node.getDbVersion() == 999);
        appendStep(steps, seq++, "DatabaseNode multiple setDbVersion calls", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::vector<TableNode> tables;
        tables.emplace_back("t1", std::vector<std::string>{"id"});
        tables.emplace_back("t2", std::vector<std::string>{"x", "y"});
        DatabaseNode node("multi", tables);
        node.setDbVersion(66);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 66 && node2.getTables().size() == 2);
        appendStep(steps, seq++, "DatabaseNode with multiple tables roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setDbVersion(55);
        node.setDbVersion(55);
        bool p = (node.getDbVersion() == 55);
        appendStep(steps, seq++, "DatabaseNode setDbVersion same value twice", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }

    // ====================== JSON serialization (30+) ======================
    {
        NetworkTransferData data;
        data.setType("TEST");
        data.setId("user");
        data.setDbVersion(10);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getType() == "TEST" && data2.getId() == "user" && data2.getDbVersion() == 10);
        appendStep(steps, seq++, "All fields preserved in JSON roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("hello world");
        data.setDbVersion(1);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "hello world" && data2.getDbVersion() == 1);
        appendStep(steps, seq++, "Message field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("special \"chars\" and \\ backslash");
        data.setDbVersion(2);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "special \"chars\" and \\ backslash" && data2.getDbVersion() == 2);
        appendStep(steps, seq++, "Special characters in message preserved", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("");
        data.setDbVersion(3);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage().empty() && data2.getDbVersion() == 3);
        appendStep(steps, seq++, "Empty message preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbName("my_database");
        data.setDbVersion(4);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbName() == "my_database" && data2.getDbVersion() == 4);
        appendStep(steps, seq++, "DbName field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSql("SELECT * FROM users WHERE id = 1;");
        data.setDbVersion(5);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSql() == "SELECT * FROM users WHERE id = 1;" && data2.getDbVersion() == 5);
        appendStep(steps, seq++, "SQL field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSuccess(true);
        data.setDbVersion(6);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSuccess() == true && data2.getDbVersion() == 6);
        appendStep(steps, seq++, "Success field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setAffectedRows(100);
        data.setDbVersion(7);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getAffectedRows() == 100 && data2.getDbVersion() == 7);
        appendStep(steps, seq++, "AffectedRows field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setColumns({"id", "name", "age"});
        data.setDbVersion(8);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getColumns().size() == 3 && data2.getDbVersion() == 8);
        appendStep(steps, seq++, "Columns field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setRows({{"1", "alice"}, {"2", "bob"}});
        data.setDbVersion(9);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getRows().size() == 2 && data2.getDbVersion() == 9);
        appendStep(steps, seq++, "Rows field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<DatabaseNode> dbs;
        DatabaseNode db1; db1.setName("db1"); db1.setDbVersion(10);
        dbs.push_back(db1);
        DatabaseNode db2; db2.setName("db2"); db2.setDbVersion(20);
        dbs.push_back(db2);
        data.setDatabases(dbs);
        data.setDbVersion(99);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDatabases().size() == 2
               && data2.getDatabases()[0].getDbVersion() == 10
               && data2.getDatabases()[1].getDbVersion() == 20
               && data2.getDbVersion() == 99);
        appendStep(steps, seq++, "Databases list preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setPassword("secret123");
        data.setDbVersion(11);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getPassword() == "secret123" && data2.getDbVersion() == 11);
        appendStep(steps, seq++, "Password field preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setId("user_with_unicode_\u4e2d\u6587");
        data.setDbVersion(12);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getId() == "user_with_unicode_\u4e2d\u6587" && data2.getDbVersion() == 12);
        appendStep(steps, seq++, "Unicode in id preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("line1\nline2\nline3");
        data.setDbVersion(13);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "line1\nline2\nline3" && data2.getDbVersion() == 13);
        appendStep(steps, seq++, "Newlines in message preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbName("db_with_\u00e9\u00e8\u00ea");
        data.setDbVersion(14);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbName() == "db_with_\u00e9\u00e8\u00ea" && data2.getDbVersion() == 14);
        appendStep(steps, seq++, "Unicode in dbName preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<std::string> cols;
        for (int i = 0; i < 100; ++i) cols.push_back("col" + std::to_string(i));
        data.setColumns(cols);
        data.setDbVersion(15);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getColumns().size() == 100 && data2.getDbVersion() == 15);
        appendStep(steps, seq++, "Large columns array preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 50; ++i) rows.push_back({std::to_string(i), "val" + std::to_string(i)});
        data.setRows(rows);
        data.setDbVersion(16);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getRows().size() == 50 && data2.getDbVersion() == 16);
        appendStep(steps, seq++, "Large rows array preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<DatabaseNode> dbs;
        for (int i = 0; i < 20; ++i) {
            DatabaseNode db;
            db.setName("db" + std::to_string(i));
            db.setDbVersion((uint64_t)i);
            dbs.push_back(db);
        }
        data.setDatabases(dbs);
        data.setDbVersion(17);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDatabases().size() == 20 && data2.getDbVersion() == 17);
        appendStep(steps, seq++, "Large databases array preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setType("");
        data.setId("");
        data.setDbName("");
        data.setSql("");
        data.setMessage("");
        data.setDbVersion(18);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getType().empty() && data2.getDbVersion() == 18);
        appendStep(steps, seq++, "Empty strings preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSuccess(false);
        data.setDbVersion(19);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSuccess() == false && data2.getDbVersion() == 19);
        appendStep(steps, seq++, "Success=false preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setAffectedRows(0);
        data.setDbVersion(20);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getAffectedRows() == 0 && data2.getDbVersion() == 20);
        appendStep(steps, seq++, "AffectedRows=0 preserved with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(21);
        std::string json = data.toJson();
        bool p = (json.find("dbVersion") != std::string::npos);
        appendStep(steps, seq++, "dbVersion key present in JSON", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(22);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 22);
        appendStep(steps, seq++, "dbVersion 22 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(1000000);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 1000000);
        appendStep(steps, seq++, "dbVersion 1000000 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        DatabaseNode node;
        node.setName("nested_test");
        node.setDbVersion(77);
        std::vector<TableNode> tables;
        tables.emplace_back("t1", std::vector<std::string>{"a", "b", "c"});
        tables.emplace_back("t2", std::vector<std::string>{"x"});
        node.setTables(tables);
        std::string json = node.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getName() == "nested_test" && node2.getDbVersion() == 77 && node2.getTables().size() == 2);
        appendStep(steps, seq++, "Nested DatabaseNode with tables roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbVersion(23);
        data.setDbVersion(24);
        data.setDbVersion(25);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 25);
        appendStep(steps, seq++, "Multiple setDbVersion before toJson", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }

    // ====================== Type constants (10+) ======================
    {
        bool p = (NetworkTransferData::DB_VERSION_REQUEST == "DB_VERSION_REQUEST");
        appendStep(steps, seq++, "DB_VERSION_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::DB_VERSION_RESPONSE == "DB_VERSION_RESPONSE");
        appendStep(steps, seq++, "DB_VERSION_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::SQL_EXEC_REQUEST == "SQL_EXEC_REQUEST");
        appendStep(steps, seq++, "SQL_EXEC_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::SQL_EXEC_RESPONSE == "SQL_EXEC_RESPONSE");
        appendStep(steps, seq++, "SQL_EXEC_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::SQL_QUERY_RESPONSE == "SQL_QUERY_RESPONSE");
        appendStep(steps, seq++, "SQL_QUERY_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::DIRECTORY_REQUEST == "DIRECTORY_REQUEST");
        appendStep(steps, seq++, "DIRECTORY_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::DIRECTORY_RESPONSE == "DIRECTORY_RESPONSE");
        appendStep(steps, seq++, "DIRECTORY_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::LOGIN_REQUEST == "LOGIN_REQUEST");
        appendStep(steps, seq++, "LOGIN_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::LOGIN_RESPONSE == "LOGIN_RESPONSE");
        appendStep(steps, seq++, "LOGIN_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::VERIFY_REQUEST == "VERIFY_REQUEST");
        appendStep(steps, seq++, "VERIFY_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::VERIFY_RESPONSE == "VERIFY_RESPONSE");
        appendStep(steps, seq++, "VERIFY_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::ERROR_RESPONSE == "ERROR_RESPONSE");
        appendStep(steps, seq++, "ERROR_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::USE_DATABASE_REQUEST == "USE_DATABASE_REQUEST");
        appendStep(steps, seq++, "USE_DATABASE_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        bool p = (NetworkTransferData::USE_DATABASE_RESPONSE == "USE_DATABASE_RESPONSE");
        appendStep(steps, seq++, "USE_DATABASE_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }

    // ====================== Error message formats (10+) ======================
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setDbName("test_db");
        errResp.setDbVersion(10);
        errResp.setMessage("Database version mismatch: client=5, server=10. Please refresh the directory.");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess()
               && err2.getDbName() == "test_db"
               && err2.getDbVersion() == 10
               && err2.getMessage().find("version mismatch") != std::string::npos);
        appendStep(steps, seq++, "Version mismatch error response format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setDbVersion(99);
        errResp.setMessage("version mismatch");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getDbVersion() == 99);
        appendStep(steps, seq++, "Short version mismatch message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Table not found");
        errResp.setDbVersion(0);
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Table not found");
        appendStep(steps, seq++, "Table not found error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Duplicate primary key");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Duplicate primary key");
        appendStep(steps, seq++, "Duplicate PK error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Syntax error");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Syntax error");
        appendStep(steps, seq++, "Syntax error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::ERROR_RESPONSE, "");
        errResp.setSuccess(false);
        errResp.setMessage("Unknown error");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (err2.getType() == "ERROR_RESPONSE" && !err2.getSuccess());
        appendStep(steps, seq++, "ERROR_RESPONSE type preserved", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Database does not exist");
        errResp.setDbName("missing_db");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getDbName() == "missing_db");
        appendStep(steps, seq++, "DB not found error with dbName", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Permission denied");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Permission denied");
        appendStep(steps, seq++, "Permission denied error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Connection timeout");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Connection timeout");
        appendStep(steps, seq++, "Connection timeout error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Invalid column type");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Invalid column type");
        appendStep(steps, seq++, "Invalid column type error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Foreign key constraint failed");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Foreign key constraint failed");
        appendStep(steps, seq++, "FK constraint error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Transaction rolled back");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Transaction rolled back");
        appendStep(steps, seq++, "Transaction rollback error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Lock wait timeout");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getMessage() == "Lock wait timeout");
        appendStep(steps, seq++, "Lock wait timeout error message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }

    overall = std::all_of(steps.begin(), steps.end(),
                          [](const auto &s) { return s.passed; });

    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    std::cout << "\n========================================\n";
    std::cout << "Results: " << gPassedTests << " / " << gTotalTests
              << " passed (" << pct << "%)\n";
    std::cout << "Overall: " << (overall ? "PASS" : "FAIL") << "\n";
    std::cout << "========================================\n";

    if (!overall) {
        std::cout << "\nFailed tests:\n";
        for (const auto &s : steps) {
            if (!s.passed)
                std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";
        }
    }

    writeReportLog("DbVersionModelTest", steps);
    return overall ? 0 : 1;
}
