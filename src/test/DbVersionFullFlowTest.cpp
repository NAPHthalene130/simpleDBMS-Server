/**
 * @file DbVersionFullFlowTest.cpp
 * @brief 数据库版本号全流程集成测试
 * @details 测试完整网络协议流程：DB_VERSION 请求/响应、DIRECTORY 含版本号、
 *          SQL_EXEC_REQUEST 版本核验(匹配/不匹配)、版本递增、响应中返回新版本。
 * @author NAPH130
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include "Core.h"
#include "models/network/NetworkTransferData.h"
#include "network/NetReceiver.h"

namespace {

constexpr unsigned short TEST_PORT = 19090;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);

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

std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength) {
    return {(unsigned char)(messageLength>>24), (unsigned char)(messageLength>>16),
            (unsigned char)(messageLength>>8), (unsigned char)(messageLength)};
}

std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &h) {
    return ((uint32_t)h[0]<<24)|((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|(uint32_t)h[3];
}

void sendRawMessage(asio::ip::tcp::socket *s, const std::string &m) {
    auto h = buildLengthHeader((uint32_t)m.size());
    asio::write(*s, asio::buffer(h));
    asio::write(*s, asio::buffer(m));
}

std::string receiveRawMessage(asio::ip::tcp::socket *s) {
    std::array<unsigned char,4> h{};
    asio::read(*s, asio::buffer(h));
    auto len = parseLengthHeader(h);
    std::string msg(len,'\0');
    asio::read(*s, asio::buffer(msg.data(),msg.size()));
    return msg;
}

NetworkTransferData sendRecv(asio::ip::tcp::socket *s, const NetworkTransferData &r) {
    sendRawMessage(s, r.toJson());
    return NetworkTransferData::fromJson(receiveRawMessage(s));
}

void connectWithRetry(asio::ip::tcp::socket *s, unsigned short port) {
    asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
    for (int i=0;i<CONNECT_RETRY_COUNT;++i) {
        std::error_code ec;
        s->connect(ep,ec);
        if (!ec) return;
        std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
    }
    throw std::runtime_error("connect failed");
}

void writeReport(const std::vector<TestStepResult> &steps, bool ok, const std::string &fatal) {
    std::ofstream ofs("DbVersionFullFlowTestReport.md", std::ios::trunc);
    if (!ofs.good()) return;
    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    ofs << "# DbVersion Full Flow Test Report\n\n";
    ofs << "- Overall: " << (ok ? "PASS" : "FAIL") << "\n";
    ofs << "- Pass Rate: " << gPassedTests << "/" << gTotalTests << " (" << pct << "%)\n\n";
    ofs << "## Steps\n\n| ID | Step | Result | Detail |\n|---|---|---|---|\n";
    for (const auto &s : steps) {
        ofs << "| " << s.id << " | " << s.name << " | "
            << (s.passed ? "PASS" : "FAIL") << " | " << s.detail << " |\n";
    }
    if (!fatal.empty()) ofs << "\n## Fatal\n```\n" << fatal << "\n```\n";
    if (!ok) {
        ofs << "\n## Failed Steps\n\n";
        for (const auto &s : steps) {
            if (!s.passed)
                ofs << "- **#" << s.id << " " << s.name << "**: " << s.detail << "\n";
        }
    }
}

} // namespace

int main() {
    const std::string DB = "ver_flow_db", TBL = "test_ver_users", UID = "verFlowTest";

    std::vector<TestStepResult> steps;
    bool ok = false;
    std::string fatal;

    std::cout << "\n========== DbVersion Full Flow Test ==========\n";

    Core core;
    std::unique_ptr<NetReceiver> recv;

    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT);
        recv->start();

        asio::io_context ctx;
        asio::ip::tcp::socket sock(ctx);
        connectWithRetry(&sock, TEST_PORT);

        // ==================== Phase 0: 清理上一次运行残留数据 ====================
        {
            NetworkTransferData cleanupReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            cleanupReq.setSql("DROP DATABASE " + DB + ";");
            sendRecv(&sock, cleanupReq);
        }

        // ==================== Phase 1: DB_VERSION 请求/响应 ====================
        {
            // 1-1: 发送 DB_VERSION_REQUEST 获取所有数据库版本
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::DB_VERSION_RESPONSE
                  && resp.getSuccess();
            appendStep(steps, 1, "DB_VERSION_REQUEST returns DB_VERSION_RESPONSE with success",
                       p, resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-1\n";
        }
        {
            // 1-2: DB_VERSION_RESPONSE 包含 databases 字段
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 2, "DB_VERSION_RESPONSE has databases list",
                       p, "databases count=" + std::to_string(resp.getDatabases().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 1-2\n";
        }

        // ==================== Phase 2: 创建数据库后版本号 ====================
        {
            // 2-1: CREATE DATABASE
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql("CREATE DATABASE " + DB + ";");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 3, "CREATE DATABASE for flow test", p, resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-1\n";
        }
        {
            // 2-2: USE DATABASE
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql("USE DATABASE " + DB + ";");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 4, "USE DATABASE for flow test", p, resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 2-2\n";
        }

        // ==================== Phase 3: 首次 SQL_EXEC_REQUEST 版本核验 ====================
        {
            // 3-1: 不带版本号发送 CREATE TABLE (dbVersion=0, server version=0, 应匹配)
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(0);
            req.setSql("CREATE TABLE " + TBL + " (id INT PRIMARY KEY, name VARCHAR(50));");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 5, "CREATE TABLE with dbVersion=0 (match server) succeeds", p,
                       p ? resp.getMessage() + " newVersion=" + std::to_string(resp.getDbVersion())
                         : resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-1\n";
        }
        {
            // 3-2: CREATE TABLE 成功后响应包含新版本号 > 0
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(1);
            req.setSql("CREATE TABLE ver_t2 (id INT);");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && resp.getDbVersion() > 0;
            appendStep(steps, 6, "SQL_EXEC_RESPONSE contains new dbVersion after success", p,
                       "version=" + std::to_string(resp.getDbVersion()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 3-2\n";
        }

        // ==================== Phase 4: 版本不匹配测试 ====================
        {
            // 4-1: 发送错误的版本号 (发送999, 服务器实际值远小于999)
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(999);
            req.setSql("INSERT INTO " + TBL + " VALUES (1, 'test');");
            auto resp = sendRecv(&sock, req);
            bool p = !resp.getSuccess()
                  && resp.getDbVersion() > 0
                  && resp.getMessage().find("version mismatch") != std::string::npos;
            appendStep(steps, 7, "SQL_EXEC_REQUEST with wrong version fails with mismatch",
                       p, p ? "serverVersion=" + std::to_string(resp.getDbVersion())
                            : "type=" + resp.getType() + " success=" + std::to_string(resp.getSuccess())
                              + " msg=" + resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-1\n";
        }
        {
            // 4-2: 版本不匹配时服务器返回了正确的服务器版本号
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(8888);
            req.setSql("SHOW TABLES;");
            auto resp = sendRecv(&sock, req);
            bool p = !resp.getSuccess()
                  && resp.getDbVersion() > 0
                  && resp.getDbVersion() != 8888;
            appendStep(steps, 8, "Version mismatch response includes correct server version", p,
                       "serverVersion=" + std::to_string(resp.getDbVersion()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-2\n";
        }
        {
            // 4-3: 版本不匹配时响应类型正确 (SQL_EXEC_RESPONSE)
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(7777);
            req.setSql("SHOW TABLES;");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                  && !resp.getSuccess();
            appendStep(steps, 9, "Version mismatch response type is SQL_EXEC_RESPONSE", p,
                       resp.getType());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 4-3\n";
        }

        // ==================== Phase 5: 使用正确版本号继续执行 ====================
        {
            // 5-1: 先获取当前正确的版本号（发个不匹配请求来获得serverVersion）
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t correctVersion = probeResp.getDbVersion();

            // 5-2: 用正确版本号发送 INSERT
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(correctVersion);
            req.setSql("INSERT INTO " + TBL + " VALUES (1, 'alice');");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 10, "INSERT with correct version succeeds", p,
                       p ? "newVersion=" + std::to_string(resp.getDbVersion())
                         : "correctVersion=" + std::to_string(correctVersion) + " " + resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-1\n";
        }
        {
            // 5-3: 再次获取正确版本并发送 INSERT
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t correctVersion = probeResp.getDbVersion();

            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(correctVersion);
            req.setSql("INSERT INTO " + TBL + " VALUES (2, 'bob');");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 11, "Second INSERT with updated version succeeds", p,
                       p ? "newVersion=" + std::to_string(resp.getDbVersion())
                         : resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 5-2\n";
        }

        // ==================== Phase 6: SELECT 查询测试版本 ====================
        {
            // 6-1: 先获取正确版本号
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t correctVersion = probeResp.getDbVersion();

            // 6-2: 用正确版本号发送 SELECT
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(correctVersion);
            req.setSql("SELECT * FROM " + TBL + ";");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::SQL_QUERY_RESPONSE
                  && resp.getSuccess()
                  && resp.getRows().size() == 2;
            appendStep(steps, 12, "SELECT returns SQL_QUERY_RESPONSE with 2 rows", p,
                       p ? "rows=" + std::to_string(resp.getRows().size())
                            + " version=" + std::to_string(resp.getDbVersion())
                         : resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-1\n";
        }
        {
            // 6-3: SELECT 响应包含版本号
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t correctVersion = probeResp.getDbVersion();

            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(correctVersion);
            req.setSql("SELECT id FROM " + TBL + " WHERE id = 1;");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && resp.getDbVersion() > 0;
            appendStep(steps, 13, "SQL_QUERY_RESPONSE contains dbVersion after SELECT", p,
                       "version=" + std::to_string(resp.getDbVersion()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 6-2\n";
        }

        // ==================== Phase 7: 版本递增验证 ====================
        {
            // 7-1: 获取当前版本
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t versionBefore = probeResp.getDbVersion();

            // 7-2: 执行 UPDATE
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(versionBefore);
            req.setSql("UPDATE " + TBL + " SET name = 'ALICE' WHERE id = 1;");
            auto resp = sendRecv(&sock, req);
            uint64_t versionAfter = resp.getDbVersion();

            bool p = resp.getSuccess() && (versionAfter == versionBefore + 1);
            appendStep(steps, 14, "Version increments by 1 after successful UPDATE", p,
                       p ? std::to_string(versionBefore) + "->" + std::to_string(versionAfter)
                         : "before=" + std::to_string(versionBefore)
                           + " after=" + std::to_string(versionAfter)
                           + " " + resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 7-1\n";
        }
        {
            // 7-3: 连续两次操作版本递增验证
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t v0 = probeResp.getDbVersion();

            NetworkTransferData req1(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req1.setDbName(DB);
            req1.setDbVersion(v0);
            req1.setSql("DELETE FROM " + TBL + " WHERE id = 2;");
            auto resp1 = sendRecv(&sock, req1);
            uint64_t v1 = resp1.getDbVersion();

            NetworkTransferData req2(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req2.setDbName(DB);
            req2.setDbVersion(v1);
            req2.setSql("INSERT INTO " + TBL + " VALUES (3, 'carol');");
            auto resp2 = sendRecv(&sock, req2);
            uint64_t v2 = resp2.getDbVersion();

            bool p = resp1.getSuccess() && resp2.getSuccess()
                  && (v1 == v0 + 1) && (v2 == v1 + 1);
            appendStep(steps, 15, "Version increments twice for consecutive operations", p,
                       p ? std::to_string(v0) + "->" + std::to_string(v1) + "->" + std::to_string(v2)
                         : "v0=" + std::to_string(v0) + " v1=" + std::to_string(v1)
                           + " v2=" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 7-2\n";
        }

        // ==================== Phase 8: DIRECTORY_RESPONSE 包含版本号 ====================
        {
            // 8-1: DIRECTORY_REQUEST 返回的 DatabaseNode 包含版本号
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasVersion = false;
            bool foundDb = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) {
                    foundDb = true;
                    hasVersion = (db.getDbVersion() > 0);
                    break;
                }
            }
            bool p = resp.getSuccess() && foundDb && hasVersion;
            appendStep(steps, 16, "DIRECTORY_RESPONSE includes dbVersion per database", p,
                       p ? "found " + DB + " with version"
                         : "foundDb=" + std::to_string(foundDb)
                           + " hasVersion=" + std::to_string(hasVersion));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 8-1\n";
        }

        // ==================== Phase 9: 无 dbName 的请求不走版本核验 ====================
        {
            // 9-1: SHOW DATABASES 不带 dbName 也能正常执行
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql("SHOW DATABASES;");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::SQL_QUERY_RESPONSE
                  && resp.getSuccess();
            appendStep(steps, 17, "SHOW DATABASES without dbName works (skip version check)", p,
                       p ? "rows=" + std::to_string(resp.getRows().size())
                         : resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 9-1\n";
        }
        {
            // 9-2: SHOW DATABASES 响应版本号为 0 (因为无 dbName)
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql("SHOW DATABASES;");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getDbVersion() == 0;
            appendStep(steps, 18, "SHOW DATABASES response dbVersion=0 (no dbName context)", p,
                       std::to_string(resp.getDbVersion()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 9-2\n";
        }

        // ==================== Phase 10: 多语句批次版本处理 ====================
        {
            // 10-1: 多语句执行，版本仅递增一次（发送2条INSERT，读取2个响应）
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t vBefore = probeResp.getDbVersion();

            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(vBefore);
            req.setSql("INSERT INTO " + TBL + " VALUES (4, 'dave'); "
                       "INSERT INTO " + TBL + " VALUES (5, 'eve');");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));

            // 再次探测版本
            NetworkTransferData probeReq2(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq2.setDbName(DB);
            probeReq2.setDbVersion(99999);
            probeReq2.setSql("SHOW TABLES;");
            auto probeResp2 = sendRecv(&sock, probeReq2);
            uint64_t vAfter = probeResp2.getDbVersion();

            // 多语句批次中每条语句都会执行，但版本仅递增一次（在批次前递增）
            bool p = resp1.getSuccess() && resp2.getSuccess() && (vAfter == vBefore + 1);
            appendStep(steps, 19, "Multi-statement batch increments version once", p,
                       p ? std::to_string(vBefore) + "->" + std::to_string(vAfter)
                         : "vBefore=" + std::to_string(vBefore) + " vAfter=" + std::to_string(vAfter)
                           + " r1=" + resp1.getMessage() + " r2=" + resp2.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 10-1\n";
        }

        // ==================== Phase 11: 清理 ====================
        {
            // 11-1: DROP TABLE - 获取当前版本后发送删除
            NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            probeReq.setDbName(DB);
            probeReq.setDbVersion(99999);
            probeReq.setSql("SHOW TABLES;");
            auto probeResp = sendRecv(&sock, probeReq);
            uint64_t v = probeResp.getDbVersion();

            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            req.setDbVersion(v);
            req.setSql("DROP TABLE ver_t2; DROP TABLE " + TBL + ";");
            sendRawMessage(&sock, req.toJson());
            auto dropResp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto dropResp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            bool p = dropResp1.getSuccess() && dropResp2.getSuccess();
            appendStep(steps, 20, "DROP TABLE cleanup", p,
                       dropResp1.getMessage() + " / " + dropResp2.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 11-1\n";
        }
        {
            // 11-2: DROP DATABASE
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql("DROP DATABASE " + DB + ";");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, 21, "DROP DATABASE cleanup", p, resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 11-2\n";
        }

        // ==================== Phase 12: 删除后 DB_VERSION 不包含该数据库 ====================
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasDroppedDb = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) {
                    hasDroppedDb = true;
                    break;
                }
            }
            bool p = !hasDroppedDb;
            appendStep(steps, 22, "DB_VERSION_RESPONSE excludes dropped database", p,
                       p ? "ok" : DB + " still present");
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " 12-1\n";
        }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);
        sock.close();
        ok = std::all_of(steps.begin(), steps.end(),
                         [](const auto &s) { return s.passed; });
    } catch (const std::exception &e) {
        fatal = e.what();
        ok = false;
    }

    if (recv) recv->stop();

    double pct = gTotalTests > 0 ? (100.0 * gPassedTests / gTotalTests) : 0.0;
    std::cout << "\n========================================\n";
    std::cout << "Results: " << gPassedTests << " / " << gTotalTests
              << " passed (" << pct << "%)\n";
    std::cout << "Overall: " << (ok ? "PASS" : "FAIL") << "\n";
    if (!fatal.empty()) std::cout << "Fatal: " << fatal << "\n";
    std::cout << "========================================\n";

    if (!ok) {
        std::cout << "\nFailed tests:\n";
        for (const auto &s : steps) {
            if (!s.passed)
                std::cout << "  #" << s.id << " " << s.name << " - " << s.detail << "\n";
        }
    }

    writeReport(steps, ok, fatal);
    return ok ? 0 : 1;
}
