/**
 * @file DbVersionModelTest.cpp
 * @brief 数据库版本号模型层单元测试
 * @details 测试 NetworkTransferData、DatabaseNode 中 dbVersion 字段的序列化/反序列化、
 *          DB_VERSION_REQUEST/RESPONSE 常量、默认值、JSON 往返一致性。
 * @author NAPH130
 */
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
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

void writeReport(const std::vector<TestStepResult> &steps, bool overall) {
    std::ofstream ofs("DbVersionModelTestReport.md", std::ios::trunc);
    if (!ofs.good()) return;
    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    ofs << "# DbVersion Model Test Report\n\n";
    ofs << "- Overall: " << (overall ? "PASS" : "FAIL") << "\n";
    ofs << "- Pass Rate: " << gPassedTests << "/" << gTotalTests << " (" << pct << "%)\n\n";
    ofs << "## Steps\n\n| ID | Step | Result | Detail |\n|---|---|---|---|\n";
    for (const auto &s : steps) {
        ofs << "| " << s.id << " | " << s.name << " | "
            << (s.passed ? "PASS" : "FAIL") << " | " << s.detail << " |\n";
    }
    if (!overall) {
        ofs << "\n## Failed Steps\n\n";
        for (const auto &s : steps) {
            if (!s.passed)
                ofs << "- **#" << s.id << " " << s.name << "**: " << s.detail << "\n";
        }
    }
}

} // namespace

int main() {
    std::vector<TestStepResult> steps;
    bool overall = true;

    std::cout << "\n========== DbVersion Model Test ==========\n";

    // ====================== 1. dbVersion 默认值测试 ======================
    {
        // 1-1: NetworkTransferData 默认构造 dbVersion 为 0
        NetworkTransferData data;
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, 1, "NetworkTransferData default dbVersion=0", p,
                   p ? "ok" : "got " + std::to_string(data.getDbVersion()));
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-1\n";
    }
    {
        // 1-2: DatabaseNode 默认构造 dbVersion 为 0
        DatabaseNode node;
        bool p = (node.getDbVersion() == 0);
        appendStep(steps, 2, "DatabaseNode default dbVersion=0", p,
                   p ? "ok" : "got " + std::to_string(node.getDbVersion()));
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-2\n";
    }

    // ====================== 2. dbVersion setter/getter 测试 ======================
    {
        // 2-1: 设置普通值
        NetworkTransferData data;
        data.setDbVersion(42);
        bool p = (data.getDbVersion() == 42);
        appendStep(steps, 3, "setDbVersion/getDbVersion normal value", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-1\n";
    }
    {
        // 2-2: 设置 0
        NetworkTransferData data;
        data.setDbVersion(100);
        data.setDbVersion(0);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, 4, "setDbVersion 0 after non-zero", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-2\n";
    }
    {
        // 2-3: 设置 UINT64_MAX
        NetworkTransferData data;
        data.setDbVersion(UINT64_MAX);
        bool p = (data.getDbVersion() == UINT64_MAX);
        appendStep(steps, 5, "setDbVersion UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-3\n";
    }
    {
        // 2-4: DatabaseNode setDbVersion
        DatabaseNode node;
        node.setDbVersion(999);
        bool p = (node.getDbVersion() == 999);
        appendStep(steps, 6, "DatabaseNode setDbVersion/getDbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-4\n";
    }

    // ====================== 3. dbVersion JSON 序列化/反序列化测试 ======================
    {
        // 3-1: NetworkTransferData JSON 往返 (dbVersion=0)
        NetworkTransferData data1;
        data1.setDbVersion(0);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 0);
        appendStep(steps, 7, "toJson/fromJson roundtrip dbVersion=0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-1\n";
    }
    {
        // 3-2: NetworkTransferData JSON 往返 (dbVersion=12345)
        NetworkTransferData data1;
        data1.setDbVersion(12345);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == 12345);
        appendStep(steps, 8, "toJson/fromJson roundtrip dbVersion=12345", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-2\n";
    }
    {
        // 3-3: NetworkTransferData JSON 往返 (dbVersion=UINT64_MAX)
        NetworkTransferData data1;
        data1.setDbVersion(UINT64_MAX);
        std::string json = data1.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDbVersion() == UINT64_MAX);
        appendStep(steps, 9, "toJson/fromJson roundtrip dbVersion=UINT64_MAX", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-3\n";
    }
    {
        // 3-4: JSON 中包含 dbVersion 字段
        NetworkTransferData data1;
        data1.setDbVersion(777);
        std::string json = data1.toJson();
        bool p = (json.find("\"dbVersion\"") != std::string::npos);
        appendStep(steps, 10, "JSON contains dbVersion field", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-4\n";
    }
    {
        // 3-5: 旧格式 JSON (无 dbVersion 字段) 反序列化后为默认值 0
        std::string oldJson = R"({"type":"TEST","id":"test","password":"","dbName":"","sql":"","success":false,"message":"","affectedRows":0,"columns":[],"rows":[],"databases":[]})";
        NetworkTransferData data = NetworkTransferData::fromJson(oldJson);
        bool p = (data.getDbVersion() == 0);
        appendStep(steps, 11, "Old JSON without dbVersion defaults to 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-5\n";
    }

    // ====================== 4. DatabaseNode dbVersion JSON 序列化测试 ======================
    {
        // 4-1: DatabaseNode JSON 往返 dbVersion
        DatabaseNode node1("test_db", {});
        node1.setDbVersion(555);
        std::string json = node1.toJson();
        DatabaseNode node2 = DatabaseNode::fromJson(json);
        bool p = (node2.getDbVersion() == 555) && (node2.getName() == "test_db");
        appendStep(steps, 12, "DatabaseNode toJson/fromJson dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-1\n";
    }
    {
        // 4-2: DatabaseNode 旧 JSON (无 dbVersion) 默认值为 0
        std::string oldJson = R"({"name":"old_db","tables":[]})";
        DatabaseNode node = DatabaseNode::fromJson(oldJson);
        bool p = (node.getDbVersion() == 0);
        appendStep(steps, 13, "DatabaseNode old JSON dbVersion defaults to 0", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-2\n";
    }
    {
        // 4-3: DatabaseNode JSON 包含 dbVersion 字段
        DatabaseNode node("vdb", {});
        node.setDbVersion(100);
        std::string json = node.toJson();
        bool p = (json.find("\"dbVersion\"") != std::string::npos);
        appendStep(steps, 14, "DatabaseNode JSON contains dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-3\n";
    }

    // ====================== 5. DB_VERSION 常量测试 ======================
    {
        // 5-1: DB_VERSION_REQUEST 常量
        bool p = (NetworkTransferData::DB_VERSION_REQUEST == "DB_VERSION_REQUEST");
        appendStep(steps, 15, "DB_VERSION_REQUEST constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-1\n";
    }
    {
        // 5-2: DB_VERSION_RESPONSE 常量
        bool p = (NetworkTransferData::DB_VERSION_RESPONSE == "DB_VERSION_RESPONSE");
        appendStep(steps, 16, "DB_VERSION_RESPONSE constant", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-2\n";
    }
    {
        // 5-3: DB_VERSION_REQUEST 构造的消息 type 正确
        NetworkTransferData data(NetworkTransferData::DB_VERSION_REQUEST, "user1");
        bool p = (data.getType() == "DB_VERSION_REQUEST" && data.getId() == "user1");
        appendStep(steps, 17, "DB_VERSION_REQUEST message construction", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-3\n";
    }
    {
        // 5-4: 已有类型常量未被破坏
        bool p = (NetworkTransferData::SQL_EXEC_REQUEST == "SQL_EXEC_REQUEST"
               && NetworkTransferData::SQL_EXEC_RESPONSE == "SQL_EXEC_RESPONSE"
               && NetworkTransferData::SQL_QUERY_RESPONSE == "SQL_QUERY_RESPONSE"
               && NetworkTransferData::DIRECTORY_REQUEST == "DIRECTORY_REQUEST"
               && NetworkTransferData::DIRECTORY_RESPONSE == "DIRECTORY_RESPONSE"
               && NetworkTransferData::LOGIN_REQUEST == "LOGIN_REQUEST"
               && NetworkTransferData::LOGIN_RESPONSE == "LOGIN_RESPONSE"
               && NetworkTransferData::VERIFY_REQUEST == "VERIFY_REQUEST"
               && NetworkTransferData::VERIFY_RESPONSE == "VERIFY_RESPONSE"
               && NetworkTransferData::ERROR_RESPONSE == "ERROR_RESPONSE");
        appendStep(steps, 18, "All existing type constants intact", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-4\n";
    }

    // ====================== 6. 完整消息 JSON 往返测试 ======================
    {
        // 6-1: SQL_EXEC_REQUEST 带 dbVersion 完整往返
        NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, "user1");
        req.setDbName("test_db");
        req.setSql("SELECT * FROM t;");
        req.setDbVersion(42);
        std::string json = req.toJson();
        NetworkTransferData req2 = NetworkTransferData::fromJson(json);
        bool p = (req2.getType() == NetworkTransferData::SQL_EXEC_REQUEST
               && req2.getId() == "user1"
               && req2.getDbName() == "test_db"
               && req2.getSql() == "SELECT * FROM t;"
               && req2.getDbVersion() == 42);
        appendStep(steps, 19, "SQL_EXEC_REQUEST full roundtrip with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-1\n";
    }
    {
        // 6-2: SQL_EXEC_RESPONSE 带 dbVersion 完整往返
        NetworkTransferData resp(NetworkTransferData::SQL_EXEC_RESPONSE, "user1");
        resp.setSuccess(true);
        resp.setMessage("Query OK.");
        resp.setAffectedRows(0);
        resp.setDbName("test_db");
        resp.setDbVersion(43);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
               && resp2.getSuccess() == true
               && resp2.getDbVersion() == 43);
        appendStep(steps, 20, "SQL_EXEC_RESPONSE full roundtrip with dbVersion", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-2\n";
    }
    {
        // 6-3: DB_VERSION_RESPONSE 带 databases 列表
        NetworkTransferData resp(NetworkTransferData::DB_VERSION_RESPONSE, "");
        resp.setSuccess(true);
        resp.setMessage("OK");
        std::vector<DatabaseNode> dbs;
        DatabaseNode db1;
        db1.setName("db1");
        db1.setDbVersion(10);
        dbs.push_back(db1);
        DatabaseNode db2;
        db2.setName("db2");
        db2.setDbVersion(20);
        dbs.push_back(db2);
        resp.setDatabases(dbs);
        std::string json = resp.toJson();
        NetworkTransferData resp2 = NetworkTransferData::fromJson(json);
        bool p = (resp2.getType() == NetworkTransferData::DB_VERSION_RESPONSE
               && resp2.getDatabases().size() == 2
               && resp2.getDatabases()[0].getDbVersion() == 10
               && resp2.getDatabases()[1].getDbVersion() == 20);
        appendStep(steps, 21, "DB_VERSION_RESPONSE with databases list", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-3\n";
    }
    {
        // 6-4: DIRECTORY_RESPONSE 带数据库版本号
        NetworkTransferData dirResp(NetworkTransferData::DIRECTORY_RESPONSE, "");
        dirResp.setSuccess(true);
        std::vector<DatabaseNode> dbs;
        DatabaseNode db;
        db.setName("school");
        db.setDbVersion(5);
        std::vector<TableNode> tables;
        tables.emplace_back("student", std::vector<std::string>{"id", "name"});
        db.setTables(tables);
        dbs.push_back(db);
        dirResp.setDatabases(dbs);
        std::string json = dirResp.toJson();
        NetworkTransferData dirResp2 = NetworkTransferData::fromJson(json);
        auto &dbs2 = dirResp2.getDatabases();
        bool p = (dbs2.size() == 1
               && dbs2[0].getName() == "school"
               && dbs2[0].getDbVersion() == 5
               && dbs2[0].getTables().size() == 1
               && dbs2[0].getTables()[0].getName() == "student");
        appendStep(steps, 22, "DIRECTORY_RESPONSE with dbVersion in DatabaseNode", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-4\n";
    }

    // ====================== 7. 版本不一致错误消息格式测试 ======================
    {
        // 7-1: 版本不匹配错误响应
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
        appendStep(steps, 23, "Version mismatch error response format", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 7-1\n";
    }

    // ====================== 8. 边界值测试 ======================
    {
        // 8-1: dbVersion 连续设置多次
        NetworkTransferData data;
        data.setDbVersion(1);
        data.setDbVersion(2);
        data.setDbVersion(3);
        bool p = (data.getDbVersion() == 3);
        appendStep(steps, 24, "Multiple sequential setDbVersion calls", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 8-1\n";
    }
    {
        // 8-2: DatabaseNode 带 TableNode 再设 version 不影响其他字段
        std::vector<TableNode> tables;
        tables.emplace_back("t1", std::vector<std::string>{"a", "b"});
        DatabaseNode node("db", tables);
        node.setDbVersion(88);
        bool p = (node.getName() == "db" && node.getTables().size() == 1
               && node.getDbVersion() == 88
               && node.getTables()[0].getFields().size() == 2);
        appendStep(steps, 25, "DatabaseNode setDbVersion preserves other fields", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 8-2\n";
    }
    {
        // 8-3: 空 databases 向量的 JSON 往返
        NetworkTransferData data(NetworkTransferData::DB_VERSION_RESPONSE, "");
        std::string json = data.toJson();
        NetworkTransferData data2 = NetworkTransferData::fromJson(json);
        bool p = (data2.getDatabases().empty() && data2.getDbVersion() == 0);
        appendStep(steps, 26, "Empty databases vector roundtrip", p);
        std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 8-3\n";
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

    writeReport(steps, overall);
    return overall ? 0 : 1;
}
