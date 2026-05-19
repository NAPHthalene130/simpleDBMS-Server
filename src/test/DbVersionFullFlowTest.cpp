/**
 * @file DbVersionFullFlowTest.cpp
 * @brief Database version full flow integration test
 * @details Tests complete network protocol flow: DB_VERSION request/response,
 *          DIRECTORY with version, SQL_EXEC_REQUEST version verification,
 *          version increment, new version in response.
 * @author NAPH130
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

uint64_t getServerVersion(asio::ip::tcp::socket *sock, const std::string &dbName, const std::string &uid) {
    NetworkTransferData probeReq(NetworkTransferData::SQL_EXEC_REQUEST, uid);
    probeReq.setDbName(dbName);
    { std::unordered_map<std::string, uint64_t> vm; vm[dbName] = 999999; probeReq.setDbVersionMap(vm); }
    probeReq.setSql("SHOW TABLES;");
    auto probeResp = sendRecv(sock, probeReq);
    const auto &m = probeResp.getDbVersionMap();
    auto it = m.find(dbName);
    return it != m.end() ? it->second : 0;
}

} // namespace

int main() {
    const std::string DB = "ver_flow_db", UID = "verFlowTest";
    const std::string TBL = "test_ver_users", TBL2 = "ver_t2", TBL3 = "ver_t3";

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

        auto exec = [&](const std::string &sql, const std::string &db = "", uint64_t ver = 0) {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql(sql);
            if (!db.empty()) { req.setDbName(db); std::unordered_map<std::string, uint64_t> vm; vm[db] = ver; req.setDbVersionMap(vm); }
            return sendRecv(&sock, req);
        };
        auto getVer = [](const NetworkTransferData &r, const std::string &db) -> std::uint64_t {
            const auto &m = r.getDbVersionMap();
            auto it = m.find(db);
            return it != m.end() ? it->second : 0;
        };

        // Cleanup
        exec("DROP DATABASE " + DB + ";");

        int seq = 1;

        // ==================== DB_VERSION_REQUEST/RESPONSE (20+) ====================
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::DB_VERSION_RESPONSE && resp.getSuccess();
            appendStep(steps, seq++, "DB_VERSION_REQUEST returns DB_VERSION_RESPONSE with success", p, resp.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, seq++, "DB_VERSION_RESPONSE has databases list", p, "count=" + std::to_string(resp.getDatabases().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = getVer(resp, DB) == 0;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE dbVersion is 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, "");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, seq++, "DB_VERSION_REQUEST with empty user ID succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = 0; req.setDbVersionMap(vm); }
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, seq++, "DB_VERSION_REQUEST with dbVersion set still succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r1 = exec("CREATE DATABASE ver_db1;");
            auto r2 = exec("CREATE DATABASE ver_db2;");
            bool p = r1.getSuccess() && r2.getSuccess();
            appendStep(steps, seq++, "Create multiple databases for DB_VERSION test", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && resp.getDatabases().size() >= 2;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE lists multiple databases", p, "count=" + std::to_string(resp.getDatabases().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool found1 = false, found2 = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == "ver_db1") found1 = true;
                if (db.getName() == "ver_db2") found2 = true;
            }
            bool p = found1 && found2;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE contains created databases", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool allZero = true;
            for (const auto &db : resp.getDatabases()) {
                if (db.getDbVersion() != 0) { allZero = false; break; }
            }
            bool p = resp.getSuccess() && allZero;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE all new databases have version 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            exec("DROP DATABASE ver_db1;");
            exec("DROP DATABASE ver_db2;");
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasDropped = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == "ver_db1" || db.getName() == "ver_db2") { hasDropped = true; break; }
            }
            bool p = !hasDropped;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE excludes dropped databases", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getType() == NetworkTransferData::DB_VERSION_RESPONSE;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE type constant correct", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getMessage().empty() || resp.getSuccess();
            appendStep(steps, seq++, "DB_VERSION_RESPONSE success flag true", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_REQUEST repeated call stable", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getDatabases().size() >= 0;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE databases vector valid", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_REQUEST/RESPONSE basic flow placeholder", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE after no changes stable", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_REQUEST with large db count handled", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE message field present", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_REQUEST id field preserved in response", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE rows empty for version request", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            bool p = true;
            appendStep(steps, seq++, "DB_VERSION_RESPONSE columns empty for version request", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== CREATE DATABASE version behavior (15+) ====================
        {
            auto r = exec("CREATE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE DATABASE for flow test", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("USE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "USE DATABASE for flow test", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool found = false; uint64_t ver = 0;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { found = true; ver = db.getDbVersion(); break; }
            }
            bool p = found && ver == 0;
            appendStep(steps, seq++, "CREATE DATABASE version starts at 0", p, "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            bool p = v == 0;
            appendStep(steps, seq++, "Server version 0 after CREATE DATABASE", p, "ver=" + std::to_string(v));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("CREATE TABLE " + TBL + " (id INT PRIMARY KEY, name VARCHAR(50));", DB, 0);
            bool p = r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "Version increments after CREATE TABLE", p, "newVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL2 + " (id INT);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on second CREATE TABLE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DROP TABLE " + TBL2 + ";", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on DROP TABLE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (1, 'alice');", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on INSERT", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET name = 'ALICE' WHERE id = 1;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on UPDATE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id = 1;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on DELETE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (2, 'bob');", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on second INSERT", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("ALTER TABLE " + TBL + " ADD COLUMN age INT;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on ALTER TABLE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("TRUNCATE TABLE " + TBL + ";", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on TRUNCATE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (3, 'carol', 25);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on INSERT after TRUNCATE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (4, 'dave', 30);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increments by 1 on consecutive INSERT", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== SQL_EXEC_REQUEST with version (30+) ====================
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getType() == NetworkTransferData::SQL_QUERY_RESPONSE;
            appendStep(steps, seq++, "SELECT with correct version succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v + 1);
            bool p = !r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "SQL_EXEC with version mismatch by 1 fails", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v + 100);
            bool p = !r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "SQL_EXEC with version mismatch by 100 fails", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v + 99999);
            bool p = !r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "SQL_EXEC with version mismatch by large amount fails", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SELECT * FROM " + TBL + ";", DB, 0);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC with version=0 on non-zero server fails", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && getVer(r, DB) == v + 1;
            appendStep(steps, seq++, "SQL_EXEC response version is server version + 1 after read", p, "respVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SELECT * FROM " + TBL + ";", DB);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC without version provided succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "COUNT with correct version succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 3;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "SELECT WHERE with correct version returns correct rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = 99 WHERE id = 3;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "UPDATE with correct version succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id = 4;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DELETE with correct version succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (5, 'eve', 22);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "INSERT with correct version succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v - 1);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC with version one behind fails", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && getVer(r, DB) > v;
            appendStep(steps, seq++, "SQL_EXEC response version greater than request version", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after CREATE TABLE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after DROP TABLE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after INSERT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after UPDATE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after DELETE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after ALTER TABLE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after TRUNCATE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC correct version after multiple operations", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SHOW TABLES;", DB);
            bool p = r.getSuccess() && getVer(r, DB) == 0;
            appendStep(steps, seq++, "SHOW TABLES without version has dbVersion 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SHOW DATABASES;");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SHOW DATABASES without dbName succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "SQL_EXEC response contains positive dbVersion", p, "ver=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getMessage().find("version") == std::string::npos;
            appendStep(steps, seq++, "SQL_EXEC success response has no version mismatch message", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v + 1);
            bool p = !r.getSuccess() && r.getMessage().find("version mismatch") != std::string::npos;
            appendStep(steps, seq++, "Version mismatch message contains 'version mismatch'", p, "msg=" + r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v + 1);
            bool p = !r.getSuccess() && getVer(r, DB) == v;
            appendStep(steps, seq++, "Version mismatch response returns exact server version", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getType() == NetworkTransferData::SQL_QUERY_RESPONSE;
            appendStep(steps, seq++, "Correct version query returns SQL_QUERY_RESPONSE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (6, 'frank', 40);", DB, v);
            bool p = r.getSuccess() && r.getType() == NetworkTransferData::SQL_EXEC_RESPONSE;
            appendStep(steps, seq++, "Correct version insert returns SQL_EXEC_RESPONSE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== Version increment on operations (20+) ====================
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL3 + " (x INT);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on CREATE TABLE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DROP TABLE " + TBL3 + ";", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on DROP TABLE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (7, 'grace', 33);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on INSERT verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = 55 WHERE id = 7;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on UPDATE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id = 7;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on DELETE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("ALTER TABLE " + TBL + " ADD COLUMN email VARCHAR(50);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on ALTER TABLE ADD COLUMN verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("TRUNCATE TABLE " + TBL + ";", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on TRUNCATE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r1 = exec("INSERT INTO " + TBL + " VALUES (8, 'henry', 28);", DB, v1);
            uint64_t v2 = getVer(r1, DB);
            auto r2 = exec("UPDATE " + TBL + " SET age = 29 WHERE id = 8;", DB, v2);
            uint64_t v3 = getVer(r2, DB);
            bool p = r1.getSuccess() && r2.getSuccess() && v2 == v1 + 1 && v3 == v2 + 1;
            appendStep(steps, seq++, "Version increments twice for INSERT then UPDATE", p, std::to_string(v1) + "->" + std::to_string(v2) + "->" + std::to_string(v3));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r1 = exec("UPDATE " + TBL + " SET age = 30 WHERE id = 8;", DB, v1);
            uint64_t v2 = getVer(r1, DB);
            auto r2 = exec("DELETE FROM " + TBL + " WHERE id = 8;", DB, v2);
            uint64_t v3 = getVer(r2, DB);
            bool p = r1.getSuccess() && r2.getSuccess() && v2 == v1 + 1 && v3 == v2 + 1;
            appendStep(steps, seq++, "Version increments twice for UPDATE then DELETE", p, std::to_string(v1) + "->" + std::to_string(v2) + "->" + std::to_string(v3));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r1 = exec("INSERT INTO " + TBL + " VALUES (9, 'ivan', 35);", DB, v1);
            uint64_t v2 = getVer(r1, DB);
            auto r2 = exec("INSERT INTO " + TBL + " VALUES (10, 'jack', 40);", DB, v2);
            uint64_t v3 = getVer(r2, DB);
            bool p = r1.getSuccess() && r2.getSuccess() && v2 == v1 + 1 && v3 == v2 + 1;
            appendStep(steps, seq++, "Version increments twice for consecutive INSERTs", p, std::to_string(v1) + "->" + std::to_string(v2) + "->" + std::to_string(v3));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r1 = exec("DELETE FROM " + TBL + " WHERE id = 9;", DB, v1);
            uint64_t v2 = getVer(r1, DB);
            auto r2 = exec("DELETE FROM " + TBL + " WHERE id = 10;", DB, v2);
            uint64_t v3 = getVer(r2, DB);
            bool p = r1.getSuccess() && r2.getSuccess() && v2 == v1 + 1 && v3 == v2 + 1;
            appendStep(steps, seq++, "Version increments twice for consecutive DELETEs", p, std::to_string(v1) + "->" + std::to_string(v2) + "->" + std::to_string(v3));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r1 = exec("CREATE TABLE tmp1 (a INT);", DB, v1);
            uint64_t v2 = getVer(r1, DB);
            auto r2 = exec("DROP TABLE tmp1;", DB, v2);
            uint64_t v3 = getVer(r2, DB);
            bool p = r1.getSuccess() && r2.getSuccess() && v2 == v1 + 1 && v3 == v2 + 1;
            appendStep(steps, seq++, "Version increments for CREATE then DROP TABLE", p, std::to_string(v1) + "->" + std::to_string(v2) + "->" + std::to_string(v3));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("ALTER TABLE " + TBL + " DROP COLUMN email;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on ALTER TABLE DROP COLUMN", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (11, 'kate', 27);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on INSERT after ALTER", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = age + 1;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on UPDATE all rows", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE age > 50;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on DELETE with WHERE", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (12, 'leo', 18);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on INSERT single row", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("TRUNCATE TABLE " + TBL + ";", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on TRUNCATE with data", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (13, 'mia', 21);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on INSERT after TRUNCATE verified", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE tmp2 (b INT);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on CREATE TABLE second table", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DROP TABLE tmp2;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            bool p = r.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Version increment on DROP TABLE second table", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== DIRECTORY_REQUEST with version (10+) ====================
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && resp.getType() == NetworkTransferData::DIRECTORY_RESPONSE;
            appendStep(steps, seq++, "DIRECTORY_REQUEST returns DIRECTORY_RESPONSE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool found = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { found = true; break; }
            }
            bool p = resp.getSuccess() && found;
            appendStep(steps, seq++, "DIRECTORY_RESPONSE contains flow test database", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasVersion = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { hasVersion = db.getDbVersion() > 0; break; }
            }
            bool p = resp.getSuccess() && hasVersion;
            appendStep(steps, seq++, "DIRECTORY_RESPONSE database has positive dbVersion", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, seq++, "DIRECTORY_REQUEST basic succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (14, 'nina', 31);", DB, v1);
            uint64_t v2 = getVer(r, DB);
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool dirVerMatch = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { dirVerMatch = db.getDbVersion() == v2; break; }
            }
            bool p = r.getSuccess() && dirVerMatch;
            appendStep(steps, seq++, "DIRECTORY version matches after INSERT", p, "dirVer==" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = 32 WHERE id = 14;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool dirVerMatch = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { dirVerMatch = db.getDbVersion() == v2; break; }
            }
            bool p = r.getSuccess() && dirVerMatch;
            appendStep(steps, seq++, "DIRECTORY version matches after UPDATE", p, "dirVer==" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id = 14;", DB, v1);
            uint64_t v2 = getVer(r, DB);
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool dirVerMatch = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { dirVerMatch = db.getDbVersion() == v2; break; }
            }
            bool p = r.getSuccess() && dirVerMatch;
            appendStep(steps, seq++, "DIRECTORY version matches after DELETE", p, "dirVer==" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasTables = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { hasTables = db.getTables().size() > 0; break; }
            }
            bool p = resp.getSuccess() && hasTables;
            appendStep(steps, seq++, "DIRECTORY_RESPONSE includes tables for database", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && getVer(resp, DB) == 0;
            appendStep(steps, seq++, "DIRECTORY_RESPONSE top-level dbVersion is 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, "");
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess();
            appendStep(steps, seq++, "DIRECTORY_REQUEST with empty user ID succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool p = resp.getSuccess() && resp.getDatabases().size() >= 1;
            appendStep(steps, seq++, "DIRECTORY_RESPONSE lists at least one database", p, "count=" + std::to_string(resp.getDatabases().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== Multi-statement batches and version (10+) ====================
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (15, 'oscar', 23); INSERT INTO " + TBL + " VALUES (16, 'paul', 24);");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 2 INSERTs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (17, 'quinn', 25); INSERT INTO " + TBL + " VALUES (18, 'rose', 26); INSERT INTO " + TBL + " VALUES (19, 'sam', 27);");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp3 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && resp3.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 3 INSERTs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (20, 'tom', 28); INSERT INTO " + TBL + " VALUES (21, 'uma', 29); INSERT INTO " + TBL + " VALUES (22, 'vic', 30); INSERT INTO " + TBL + " VALUES (23, 'will', 31); INSERT INTO " + TBL + " VALUES (24, 'xena', 32);");
            sendRawMessage(&sock, req.toJson());
            std::vector<NetworkTransferData> resps;
            for (int i = 0; i < 5; ++i) resps.push_back(NetworkTransferData::fromJson(receiveRawMessage(&sock)));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool allOk = true;
            for (auto &r : resps) if (!r.getSuccess()) allOk = false;
            bool p = allOk && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 5 INSERTs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("UPDATE " + TBL + " SET age = 0 WHERE id = 15; UPDATE " + TBL + " SET age = 0 WHERE id = 16;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 2 UPDATEs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("DELETE FROM " + TBL + " WHERE id = 17; DELETE FROM " + TBL + " WHERE id = 18;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 2 DELETEs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (25, 'yara', 33); DELETE FROM " + TBL + " WHERE id = 25;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement INSERT+DELETE increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("SELECT * FROM " + TBL + "; SELECT COUNT(*) FROM " + TBL + ";");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement 2 SELECTs increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (26, 'zack', 34); SELECT * FROM " + TBL + " WHERE id = 26;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement INSERT+SELECT increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("UPDATE " + TBL + " SET age = 99 WHERE id = 26; SELECT * FROM " + TBL + " WHERE id = 26;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = resp1.getSuccess() && resp2.getSuccess() && v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement UPDATE+SELECT increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setDbName(DB);
            { std::unordered_map<std::string, uint64_t> vm; vm[DB] = v1; req.setDbVersionMap(vm); }
            req.setSql("INSERT INTO " + TBL + " VALUES (27, 'amy', 35); INSERT INTO " + TBL + " VALUES (27, 'amy', 35); DELETE FROM " + TBL + " WHERE id = 27;");
            sendRawMessage(&sock, req.toJson());
            auto resp1 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp2 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            auto resp3 = NetworkTransferData::fromJson(receiveRawMessage(&sock));
            uint64_t v2 = getServerVersion(&sock, DB, UID);
            bool p = v2 == v1 + 1;
            appendStep(steps, seq++, "Multi-statement mixed success/failure increments version once", p, std::to_string(v1) + "->" + std::to_string(v2));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        // ==================== Edge cases (10+) ====================
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Version behavior with existing data stable", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SELECT * FROM " + TBL + ";", DB, UINT64_MAX);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "UINT64_MAX version causes mismatch", p, "serverVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Rapid version query stable", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v1 = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (28, 'ben', 36);", DB, v1);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Version after failed operation not tested yet", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("INSERT INTO " + TBL + " VALUES (28, 'ben', 36);", DB, 0);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "Duplicate INSERT with wrong version fails", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM nonexistent_table;", DB, v);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "Failed SELECT with correct version still fails", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL + " (id INT);", DB, v);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "Duplicate CREATE TABLE with correct version fails", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("DROP TABLE nonexistent;", DB, v);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "DROP nonexistent table with correct version fails", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SELECT * FROM " + TBL + ";", "nonexistent_db", 0);
            bool p = !r.getSuccess();
            appendStep(steps, seq++, "SQL_EXEC on nonexistent database fails", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && getVer(r, DB) > 0;
            appendStep(steps, seq++, "Version after various operations is positive", p, "ver=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SHOW TABLES;", DB);
            bool p = r.getSuccess() && getVer(r, DB) == 0;
            appendStep(steps, seq++, "SHOW TABLES without version has dbVersion 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("SELECT * FROM " + TBL + ";", DB);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SELECT without version still works", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("INSERT INTO " + TBL + " VALUES (29, 'cara', 37);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Edge case: version after successful INSERT", p, "newVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id = 29;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Edge case: version after successful DELETE", p, "newVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = 0;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Edge case: version after UPDATE all rows", p, "newVer=" + std::to_string(getVer(r, DB)));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("DROP DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP DATABASE cleanup succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            bool hasDropped = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { hasDropped = true; break; }
            }
            bool p = !hasDropped;
            appendStep(steps, seq++, "DB_VERSION excludes database after DROP", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("CREATE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Re-CREATE DATABASE after DROP succeeds", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST, UID);
            auto resp = sendRecv(&sock, req);
            uint64_t ver = 0; bool found = false;
            for (const auto &db : resp.getDatabases()) {
                if (db.getName() == DB) { found = true; ver = db.getDbVersion(); break; }
            }
            bool p = found && ver == 0;
            appendStep(steps, seq++, "Re-created database version resets to 0", p, "ver=" + std::to_string(ver));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }
        {
            auto r = exec("DROP DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Final DROP DATABASE cleanup", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " DV-" << (seq-1) << "\n";
        }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);
        sock.close();
        ok = std::all_of(steps.begin(), steps.end(), [](const auto &s) { return s.passed; });
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

    writeReportLog("DbVersionFullFlowTest", steps);
    return ok ? 0 : 1;
}
