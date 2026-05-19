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
#include <map>
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

    // ====================== NetworkTransferData dbVersionMap (30+) ======================
    {
        NetworkTransferData data;
        bool p = (data.getDbVersionMap().empty());
        appendStep(steps, seq++, "NetworkTransferData default dbVersionMap empty", p,
                   p ? "ok" : "map not empty");
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 42; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == 42);
        appendStep(steps, seq++, "setDbVersionMap/getDbVersionMap normal value", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 100; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 0; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == 0);
        appendStep(steps, seq++, "setDbVersionMap 0 after non-zero", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = UINT64_MAX; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == UINT64_MAX);
        appendStep(steps, seq++, "setDbVersionMap UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == 1);
        appendStep(steps, seq++, "setDbVersionMap minimum non-zero", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1234567890123ULL; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == 1234567890123ULL);
        appendStep(steps, seq++, "setDbVersionMap large value", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = UINT64_MAX - 1; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == UINT64_MAX - 1);
        appendStep(steps, seq++, "setDbVersionMap UINT64_MAX-1", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 0; data.setDbVersionMap(vm);
        bool p = (data.getDbVersionMap().find("test_db")->second == 0);
        appendStep(steps, seq++, "setDbVersionMap explicit 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 999999; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 1; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == 1);
        appendStep(steps, seq++, "setDbVersionMap overwrite smaller", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 999999; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == 999999);
        appendStep(steps, seq++, "setDbVersionMap overwrite larger", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 0; data1.setDbVersionMap(vm);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 0);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersionMap=0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 12345; data1.setDbVersionMap(vm);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 12345);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersionMap=12345", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = UINT64_MAX; data1.setDbVersionMap(vm);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == UINT64_MAX);
        appendStep(steps, seq++, "toJson/fromJson roundtrip dbVersionMap=UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data1;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 777; data1.setDbVersionMap(vm);
        std::string json = data1.toJson();
        bool p = (json.find("\"dbVersionMap\"") != std::string::npos);
        appendStep(steps, seq++, "JSON contains dbVersionMap field", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        std::string oldJson = R"({"type":"TEST","id":"test","password":"","dbName":"","sql":"","success":false,"message":"","affectedRows":0,"columns":[],"rows":[],"databases":[]})";
        NetworkTransferData data = NetworkTransferData::fromJson(oldJson);
        bool p = (data.getDbVersionMap().empty());
        appendStep(steps, seq++, "Old JSON without dbVersionMap defaults to empty", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 555; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersionMap\":{\"test_db\":555}") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersionMap value serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = UINT64_MAX; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"test_db\":18446744073709551615") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersionMap UINT64_MAX serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, "user1");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 10; req.setDbVersionMap(vm);
        std::string json = req.toJson();
        NetworkTransferData req2 = NetworkTransferData::fromJson(json);
        bool p = (req2.getDbVersionMap().find("test_db")->second == 10 && req2.getType() == NetworkTransferData::DB_VERSION_REQUEST);
        appendStep(steps, seq++, "DB_VERSION_REQUEST with dbVersionMap roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::DB_VERSION_RESPONSE, "user1");
        resp.setSuccess(true);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 20; resp.setDbVersionMap(vm);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersionMap().find("test_db")->second == 20 && resp2.getSuccess());
        appendStep(steps, seq++, "DB_VERSION_RESPONSE with dbVersionMap roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, "user1");
        req.setDbName("test_db");
        req.setSql("SELECT * FROM t;");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 42; req.setDbVersionMap(vm);
        std::string json = req.toJson();
        NetworkTransferData req2 = NetworkTransferData::fromJson(json);
        bool p = (req2.getDbVersionMap().find("test_db")->second == 42 && req2.getDbName() == "test_db");
        appendStep(steps, seq++, "SQL_EXEC_REQUEST full roundtrip with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        resp.setSuccess(true);
        resp.setMessage("Query OK.");
        resp.setAffectedRows(0);
        resp.setDbName("test_db");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 43; resp.setDbVersionMap(vm);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersionMap().find("test_db")->second == 43 && resp2.getSuccess());
        appendStep(steps, seq++, "SQL_EXEC_RESPONSE full roundtrip with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::SQL_QUERY_RESPONSE, "user1");
        resp.setSuccess(true);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 50; resp.setDbVersionMap(vm);
        std::vector<std::vector<std::string>> rows = {{"1", "a"}, {"2", "b"}};
        resp.setRows(rows);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersionMap().find("test_db")->second == 50 && resp2.getRows().size() == 2);
        appendStep(steps, seq++, "SQL_QUERY_RESPONSE with dbVersionMap and rows roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData resp(NetworkTransferData::DIRECTORY_RESPONSE, "");
        resp.setSuccess(true);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 0; resp.setDbVersionMap(vm);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getDbVersionMap().find("test_db")->second == 0 && resp2.getType() == NetworkTransferData::DIRECTORY_RESPONSE);
        appendStep(steps, seq++, "DIRECTORY_RESPONSE with dbVersionMap=0 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 2; data.setDbVersionMap(vm2);
        std::map<std::string, std::uint64_t> vm3; vm3["test_db"] = 3; data.setDbVersionMap(vm3);
        bool p = (data.getDbVersionMap().find("test_db")->second == 3);
        appendStep(steps, seq++, "Multiple sequential setDbVersionMap calls", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 100; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersionMap\":{\"test_db\":100}") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersionMap 100 serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 0; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersionMap\":{\"test_db\":0}") != std::string::npos);
        appendStep(steps, seq++, "JSON dbVersionMap 0 serialized correctly", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 999; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 999);
        appendStep(steps, seq++, "dbVersionMap 999 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = UINT64_MAX; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == UINT64_MAX);
        appendStep(steps, seq++, "setDbVersionMap to max after min", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = UINT64_MAX; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 0; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == 0);
        appendStep(steps, seq++, "setDbVersionMap to 0 after max", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 500; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 500; data.setDbVersionMap(vm2);
        bool p = (data.getDbVersionMap().find("test_db")->second == 500);
        appendStep(steps, seq++, "setDbVersionMap same value twice", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 123; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersionMap\"") != std::string::npos);
        appendStep(steps, seq++, "dbVersionMap field present in JSON for all values", p);
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
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 10; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getType() == "TEST" && data2.getId() == "user" && data2.getDbVersionMap().find("test_db")->second == 10);
        appendStep(steps, seq++, "All fields preserved in JSON roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("hello world");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "hello world" && data2.getDbVersionMap().find("test_db")->second == 1);
        appendStep(steps, seq++, "Message field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("special \"chars\" and \\ backslash");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 2; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "special \"chars\" and \\ backslash" && data2.getDbVersionMap().find("test_db")->second == 2);
        appendStep(steps, seq++, "Special characters in message preserved", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 3; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage().empty() && data2.getDbVersionMap().find("test_db")->second == 3);
        appendStep(steps, seq++, "Empty message preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbName("my_database");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 4; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbName() == "my_database" && data2.getDbVersionMap().find("test_db")->second == 4);
        appendStep(steps, seq++, "DbName field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSql("SELECT * FROM users WHERE id = 1;");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 5; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSql() == "SELECT * FROM users WHERE id = 1;" && data2.getDbVersionMap().find("test_db")->second == 5);
        appendStep(steps, seq++, "SQL field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSuccess(true);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 6; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSuccess() == true && data2.getDbVersionMap().find("test_db")->second == 6);
        appendStep(steps, seq++, "Success field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setAffectedRows(100);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 7; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getAffectedRows() == 100 && data2.getDbVersionMap().find("test_db")->second == 7);
        appendStep(steps, seq++, "AffectedRows field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setColumns({"id", "name", "age"});
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 8; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getColumns().size() == 3 && data2.getDbVersionMap().find("test_db")->second == 8);
        appendStep(steps, seq++, "Columns field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setRows({{"1", "alice"}, {"2", "bob"}});
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 9; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getRows().size() == 2 && data2.getDbVersionMap().find("test_db")->second == 9);
        appendStep(steps, seq++, "Rows field preserved with dbVersionMap", p);
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
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 99; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDatabases().size() == 2
               && data2.getDatabases()[0].getDbVersion() == 10
               && data2.getDatabases()[1].getDbVersion() == 20
               && data2.getDbVersionMap().find("test_db")->second == 99);
        appendStep(steps, seq++, "Databases list preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setPassword("secret123");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 11; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getPassword() == "secret123" && data2.getDbVersionMap().find("test_db")->second == 11);
        appendStep(steps, seq++, "Password field preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setId("user_with_unicode_\u4e2d\u6587");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 12; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getId() == "user_with_unicode_\u4e2d\u6587" && data2.getDbVersionMap().find("test_db")->second == 12);
        appendStep(steps, seq++, "Unicode in id preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setMessage("line1\nline2\nline3");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 13; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getMessage() == "line1\nline2\nline3" && data2.getDbVersionMap().find("test_db")->second == 13);
        appendStep(steps, seq++, "Newlines in message preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setDbName("db_with_\u00e9\u00e8\u00ea");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 14; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbName() == "db_with_\u00e9\u00e8\u00ea" && data2.getDbVersionMap().find("test_db")->second == 14);
        appendStep(steps, seq++, "Unicode in dbName preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<std::string> cols;
        for (int i = 0; i < 100; ++i) cols.push_back("col" + std::to_string(i));
        data.setColumns(cols);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 15; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getColumns().size() == 100 && data2.getDbVersionMap().find("test_db")->second == 15);
        appendStep(steps, seq++, "Large columns array preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 50; ++i) rows.push_back({std::to_string(i), "val" + std::to_string(i)});
        data.setRows(rows);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 16; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getRows().size() == 50 && data2.getDbVersionMap().find("test_db")->second == 16);
        appendStep(steps, seq++, "Large rows array preserved with dbVersionMap", p);
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
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 17; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDatabases().size() == 20 && data2.getDbVersionMap().find("test_db")->second == 17);
        appendStep(steps, seq++, "Large databases array preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setType("");
        data.setId("");
        data.setDbName("");
        data.setSql("");
        data.setMessage("");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 18; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getType().empty() && data2.getDbVersionMap().find("test_db")->second == 18);
        appendStep(steps, seq++, "Empty strings preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setSuccess(false);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 19; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getSuccess() == false && data2.getDbVersionMap().find("test_db")->second == 19);
        appendStep(steps, seq++, "Success=false preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        data.setAffectedRows(0);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 20; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getAffectedRows() == 0 && data2.getDbVersionMap().find("test_db")->second == 20);
        appendStep(steps, seq++, "AffectedRows=0 preserved with dbVersionMap", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 21; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        bool p = (json.find("\"dbVersionMap\"") != std::string::npos);
        appendStep(steps, seq++, "dbVersionMap key present in JSON", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 22; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 22);
        appendStep(steps, seq++, "dbVersionMap 22 roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData data;
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 1000000; data.setDbVersionMap(vm);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 1000000);
        appendStep(steps, seq++, "dbVersionMap 1000000 roundtrip", p);
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
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 23; data.setDbVersionMap(vm);
        std::map<std::string, std::uint64_t> vm2; vm2["test_db"] = 24; data.setDbVersionMap(vm2);
        std::map<std::string, std::uint64_t> vm3; vm3["test_db"] = 25; data.setDbVersionMap(vm3);
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersionMap().find("test_db")->second == 25);
        appendStep(steps, seq++, "Multiple setDbVersionMap before toJson", p);
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
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 10; errResp.setDbVersionMap(vm);
        errResp.setMessage("Database version mismatch: client=5, server=10. Please refresh the directory.");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess()
               && err2.getDbName() == "test_db"
               && err2.getDbVersionMap().find("test_db")->second == 10
               && err2.getMessage().find("version mismatch") != std::string::npos);
        appendStep(steps, seq++, "Version mismatch error response format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 99; errResp.setDbVersionMap(vm);
        errResp.setMessage("version mismatch");
        std::string json = errResp.toJson();
        NetworkTransferData err2 = NetworkTransferData::fromJson(json);
        bool p = (!err2.getSuccess() && err2.getDbVersionMap().find("test_db")->second == 99);
        appendStep(steps, seq++, "Short version mismatch message format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " M-" << (seq-1) << "\n";
    }
    {
        NetworkTransferData errResp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        errResp.setSuccess(false);
        errResp.setMessage("Table not found");
        std::map<std::string, std::uint64_t> vm; vm["test_db"] = 0; errResp.setDbVersionMap(vm);
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
