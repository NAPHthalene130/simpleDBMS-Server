/**
 * @file PerformanceTest.cpp
 * @brief Performance testing for simpleDBMS-Server
 * @details Tests large dataset operations, complex queries, index performance,
 *          and memory/throughput measurements.
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

constexpr unsigned short TEST_PORT = 19096;
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
    std::map<std::string, std::uint64_t> vm;
    vm[dbName] = 999999;
    probeReq.setDbVersionMap(vm);
    probeReq.setSql("SHOW TABLES;");
    auto probeResp = sendRecv(sock, probeReq);
    const auto &m = probeResp.getDbVersionMap();
    auto it = m.find(dbName);
    return it != m.end() ? it->second : 0;
}

} // namespace

int main() {
    const std::string DB = "perf_test_db", UID = "perfTest";
    const std::string TBL = "perf_users", TBL2 = "perf_orders", TBL3 = "perf_items";

    std::vector<TestStepResult> steps;
    bool ok = false;
    std::string fatal;

    std::cout << "\n========== Performance Test ==========\n";

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
            if (!db.empty()) {
                req.setDbName(db);
                std::map<std::string, std::uint64_t> vm;
                vm[db] = ver;
                req.setDbVersionMap(vm);
            }
            return sendRecv(&sock, req);
        };

        // Cleanup
        exec("DROP DATABASE " + DB + ";");

        int seq = 1;

        // Setup database and tables
        {
            auto r = exec("CREATE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE DATABASE perf_test_db", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto r = exec("USE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "USE DATABASE perf_test_db", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL + " (id INT PRIMARY KEY, name VARCHAR(50), age INT, salary DECIMAL(10,2));", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE perf_users", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL2 + " (order_id INT PRIMARY KEY, user_id INT, amount DECIMAL(10,2), order_date VARCHAR(20));", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE perf_orders", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL3 + " (item_id INT PRIMARY KEY, name VARCHAR(50), price DECIMAL(10,2), stock INT);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE perf_items", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }

        // ==================== Large dataset operations (40+) ====================
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            bool allOk = true;
            for (int i = 1; i <= 100; ++i) {
                auto r = exec("INSERT INTO " + TBL + " VALUES (" + std::to_string(i) + ", 'user" + std::to_string(i) + "', " + std::to_string(20 + (i % 50)) + ", " + std::to_string(30000 + i * 100) + ".00);", DB, v);
                if (!r.getSuccess()) allOk = false;
                const auto &m = r.getDbVersionMap();
                auto it = m.find(DB);
                v = it != m.end() ? it->second : 0;
            }
            appendStep(steps, seq++, "INSERT 100 rows into perf_users", allOk);
            std::cout << "  " << (allOk ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 100;
            appendStep(steps, seq++, "SELECT * returns 100 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "COUNT(*) on 100 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 50;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "SELECT WHERE id > 50 returns 50 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET salary = salary + 1000 WHERE id <= 50;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "UPDATE 50 rows salary + 1000", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id > 90;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DELETE 10 rows", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 90;
            appendStep(steps, seq++, "Verify 90 rows after DELETE", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            bool allOk = true;
            for (int i = 101; i <= 200; ++i) {
                auto r = exec("INSERT INTO " + TBL + " VALUES (" + std::to_string(i) + ", 'user" + std::to_string(i) + "', " + std::to_string(20 + (i % 50)) + ", " + std::to_string(30000 + i * 100) + ".00);", DB, v);
                if (!r.getSuccess()) allOk = false;
                const auto &m = r.getDbVersionMap();
                auto it = m.find(DB);
                v = it != m.end() ? it->second : 0;
            }
            appendStep(steps, seq++, "INSERT another 100 rows (total 200)", allOk);
            std::cout << "  " << (allOk ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 190;
            appendStep(steps, seq++, "SELECT * returns 190 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT MIN(salary), MAX(salary), AVG(salary) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "MIN/MAX/AVG on 190 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT SUM(salary) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "SUM on 190 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary DESC;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 190;
            appendStep(steps, seq++, "ORDER BY DESC on 190 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY age ASC, salary DESC;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 190;
            appendStep(steps, seq++, "ORDER BY multiple columns on 190 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT DISTINCT age FROM " + TBL + " ORDER BY age;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DISTINCT on 190 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " LIMIT 50;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "LIMIT 50 on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " LIMIT 10;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 10;
            appendStep(steps, seq++, "LIMIT 10 on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age BETWEEN 25 AND 35;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "BETWEEN on 190 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user1%';", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "LIKE prefix on 190 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary > 50000;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "WHERE salary > 50000", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("UPDATE " + TBL + " SET age = age + 1;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "UPDATE all 190 rows", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("DELETE FROM " + TBL + " WHERE id <= 50;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DELETE 50 rows", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 140;
            appendStep(steps, seq++, "Verify 140 rows after DELETE 50", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + " WHERE age > 30;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "COUNT with WHERE on 140 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id IN (60, 70, 80, 90, 100);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "IN clause on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id DESC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "ORDER BY DESC LIMIT 20", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT age, COUNT(*) FROM " + TBL + " GROUP BY age;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "GROUP BY on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 25 AND salary < 80000;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Combined WHERE on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 25 OR salary > 100000;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "OR condition on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name = 'user100';", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Exact match on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 100 AND id <= 150;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Range scan on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY name LIMIT 30;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 30;
            appendStep(steps, seq++, "ORDER BY name LIMIT 30", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 999;", DB, v);
            bool p = r.getSuccess() && r.getRows().empty();
            appendStep(steps, seq++, "Non-existent row on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age < 0;", DB, v);
            bool p = r.getSuccess() && r.getRows().empty();
            appendStep(steps, seq++, "Impossible condition on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary IS NOT NULL;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 140;
            appendStep(steps, seq++, "IS NOT NULL on 140 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE '%user%';", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 140;
            appendStep(steps, seq++, "LIKE contains on 140 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id % 2 = 0;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Modulo condition on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id ASC LIMIT 100;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 100;
            appendStep(steps, seq++, "ORDER BY ASC LIMIT 100", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary DESC LIMIT 5;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 5;
            appendStep(steps, seq++, "ORDER BY salary DESC LIMIT 5", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*), AVG(salary), MIN(age), MAX(age) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple aggregates on 140 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 100 OR id = 120 OR id = 140;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple OR conditions", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age >= 30 AND age <= 40 AND salary > 40000;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Complex WHERE with AND", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY age DESC, salary ASC LIMIT 25;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 25;
            appendStep(steps, seq++, "Multi-column ORDER BY LIMIT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user12_' OR name LIKE 'user13_';", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple LIKE patterns", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id NOT IN (60, 70, 80);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "NOT IN clause", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary BETWEEN 40000 AND 60000;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "BETWEEN on salary", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 35 AND salary < 90000 AND name LIKE 'user%';", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Complex combined WHERE", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id DESC LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 DESC on large table", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(DISTINCT age) FROM " + TBL + ";", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "COUNT DISTINCT on 140 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 140;
            appendStep(steps, seq++, "SELECT all via id > 0", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age = 30 OR age = 35 OR age = 40;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple OR age values", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary LIMIT 50 OFFSET 50;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "LIMIT OFFSET on 140 rows", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary >= 30000 AND salary <= 100000;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 140;
            appendStep(steps, seq++, "Wide salary range returns all", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 200;", DB, v);
            bool p = r.getSuccess() && r.getRows().empty();
            appendStep(steps, seq++, "Deleted row not found", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 51;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "Single row lookup by PK", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }

        // ==================== Complex queries (40+) ====================
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 25 AND salary > 50000 ORDER BY salary DESC LIMIT 10;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Combined WHERE+ORDER BY+LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user1%' AND age >= 30 ORDER BY age;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "LIKE + WHERE + ORDER BY", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT age, COUNT(*), AVG(salary) FROM " + TBL + " GROUP BY age ORDER BY age;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "GROUP BY + ORDER BY aggregates", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id BETWEEN 60 AND 100 AND age > 25 ORDER BY id DESC LIMIT 15;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "BETWEEN + WHERE + ORDER BY DESC + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT DISTINCT age FROM " + TBL + " WHERE salary > 40000 ORDER BY age DESC;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DISTINCT + WHERE + ORDER BY DESC", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE (age > 30 AND salary < 80000) OR (age < 25 AND salary > 60000) ORDER BY id LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Complex OR + AND + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id IN (55, 65, 75, 85, 95, 105, 115, 125, 135) ORDER BY id;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Large IN clause + ORDER BY", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user%' AND age BETWEEN 30 AND 50 AND salary > 45000 ORDER BY salary DESC LIMIT 5;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "LIKE + BETWEEN + WHERE + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*), MIN(salary), MAX(salary), AVG(salary), SUM(salary) FROM " + TBL + " WHERE age > 25;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple aggregates + WHERE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 70 AND id <= 130 AND age >= 30 ORDER BY age ASC, salary DESC LIMIT 25;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Range + WHERE + multi ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE NOT (age < 25 OR salary > 150000) ORDER BY id LIMIT 30;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "NOT with OR + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT age, salary, COUNT(*) FROM " + TBL + " GROUP BY age, salary ORDER BY age, salary LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multi-column GROUP BY + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 50 AND id < 150 AND name LIKE 'user%' AND age >= 25 ORDER BY id DESC LIMIT 40;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple AND conditions + ORDER BY DESC + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary >= 50000 AND salary <= 100000 AND age IN (30, 35, 40, 45) ORDER BY salary DESC LIMIT 10;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Salary range + IN + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY name LIMIT 50 OFFSET 25;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "ORDER BY + LIMIT + OFFSET", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 60 OR id = 80 OR id = 100 OR id = 120 OR id = 140 ORDER BY id;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple OR id values + ORDER BY", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 30 AND salary < 90000 AND name LIKE 'user%' AND id >= 60 ORDER BY age DESC, id ASC LIMIT 15;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Four conditions + multi ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*), age FROM " + TBL + " WHERE salary > 40000 GROUP BY age HAVING COUNT(*) > 1 ORDER BY age;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "GROUP BY + HAVING + ORDER BY", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE (age > 35 AND salary > 60000) OR (age < 28 AND salary < 50000) ORDER BY salary DESC LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Complex OR with AND groups + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT DISTINCT age, salary FROM " + TBL + " WHERE id > 50 ORDER BY age, salary LIMIT 30;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DISTINCT multi-column + WHERE + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id BETWEEN 50 AND 150 AND age NOT IN (20, 25) AND salary >= 30000 ORDER BY id LIMIT 35;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "BETWEEN + NOT IN + WHERE + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user12%' OR name LIKE 'user13%' OR name LIKE 'user14%' ORDER BY name LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple LIKE OR + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age = 30 OR age = 35 OR age = 40 OR age = 45 OR age = 50 ORDER BY age, id LIMIT 25;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Multiple OR age + multi ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary > 55000 AND salary < 90000 AND age >= 28 AND age <= 45 AND id > 40 ORDER BY salary DESC LIMIT 12;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Four range conditions + ORDER BY DESC + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) as cnt, AVG(salary) as avg_sal FROM " + TBL + " WHERE age > 25 GROUP BY age ORDER BY cnt DESC LIMIT 10;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Aliases + GROUP BY + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id % 5 = 0 AND age > 25 ORDER BY id LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Modulo + WHERE + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 100 AND id <= 200 AND (age > 30 OR salary > 70000) ORDER BY id DESC LIMIT 18;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Range + OR group + ORDER BY DESC + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary DESC, age ASC, id DESC LIMIT 8;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 8;
            appendStep(steps, seq++, "Three-column ORDER BY + LIMIT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user%' AND id > 50 AND age < 50 AND salary > 40000 ORDER BY name LIMIT 15;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "LIKE + three AND + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id IN (55, 66, 77, 88, 99, 110, 121, 132) AND age > 25 ORDER BY id;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "IN + WHERE + ORDER BY", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE NOT id IN (60, 70, 80, 90, 100, 110, 120, 130) AND age >= 25 ORDER BY id LIMIT 30;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "NOT IN + WHERE + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age BETWEEN 30 AND 40 AND salary BETWEEN 50000 AND 100000 ORDER BY age, salary LIMIT 20;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Double BETWEEN + multi ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE (age > 30 AND salary > 60000) AND (id < 150 OR id > 160) ORDER BY id LIMIT 25;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Nested AND/OR + ORDER BY + LIMIT", p, "rows=" + std::to_string(r.getRows().size()));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + " WHERE age > 25 AND salary > 50000 AND id < 200;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "COUNT with three AND conditions", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT MIN(age), MAX(age), AVG(age), MIN(salary), MAX(salary) FROM " + TBL + " WHERE id > 50;", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "Five aggregates + WHERE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 1 OFFSET 50;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 OFFSET 50", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 1 OFFSET 100;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 OFFSET 100", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 1 OFFSET 130;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 OFFSET 130", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 1 OFFSET 139;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "LIMIT 1 OFFSET 139 (last row)", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 1 OFFSET 140;", DB, v);
            bool p = r.getSuccess() && r.getRows().empty();
            appendStep(steps, seq++, "LIMIT 1 OFFSET 140 (beyond end)", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary > 0 ORDER BY salary DESC LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "Highest salary row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary > 0 ORDER BY salary ASC LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "Lowest salary row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 0 ORDER BY age DESC LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "Highest age row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 0 ORDER BY age ASC LIMIT 1;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "Lowest age row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 51;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "PK lookup single row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 100;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 1;
            appendStep(steps, seq++, "PK lookup another row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 150;", DB, v);
            bool p = r.getSuccess() && r.getRows().empty();
            appendStep(steps, seq++, "PK lookup non-existent row", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 60 AND id <= 80;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 21;
            appendStep(steps, seq++, "PK range scan 21 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 100 AND id <= 120;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 21;
            appendStep(steps, seq++, "PK range scan another 21 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 51 AND id <= 100 ORDER BY id;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "PK range scan 50 rows ordered", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 101 AND id <= 140 ORDER BY id DESC;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 40;
            appendStep(steps, seq++, "PK range scan 40 rows reverse", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 51 OR id = 100 OR id = 130;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 3;
            appendStep(steps, seq++, "PK lookup 3 specific rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id LIMIT 10;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 10;
            appendStep(steps, seq++, "First 10 rows by PK", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id DESC LIMIT 10;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 10;
            appendStep(steps, seq++, "Last 10 rows by PK", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 50 OFFSET 0;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "First 50 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 50 OFFSET 50;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "Middle 50 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 50 OFFSET 90;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 50;
            appendStep(steps, seq++, "Last 50 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 100;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 100;
            appendStep(steps, seq++, "First 100 rows", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0 ORDER BY id LIMIT 100 OFFSET 40;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 100;
            appendStep(steps, seq++, "100 rows with offset 40", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary DESC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "Top 20 by salary", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary ASC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "Bottom 20 by salary", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY age DESC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "Top 20 by age", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY age ASC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "Bottom 20 by age", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY name LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "First 20 by name", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY name DESC LIMIT 20;", DB, v);
            bool p = r.getSuccess() && r.getRows().size() == 20;
            appendStep(steps, seq++, "Last 20 by name", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }

        // ==================== Memory/throughput (20+) ====================
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + ";", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "Full table scan under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + ";", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "COUNT(*) under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id = 100;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 1000;
            appendStep(steps, seq++, "PK lookup under 1 second", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY salary DESC LIMIT 10;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 3000;
            appendStep(steps, seq++, "Top 10 sort under 3 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 25;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 3000;
            appendStep(steps, seq++, "Filtered scan under 3 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT MIN(salary), MAX(salary), AVG(salary) FROM " + TBL + ";", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "Aggregates under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id BETWEEN 60 AND 100;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "Range scan under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT DISTINCT age FROM " + TBL + " ORDER BY age;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 3000;
            appendStep(steps, seq++, "DISTINCT sort under 3 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY age, salary, name LIMIT 100;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "Multi-column sort under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT COUNT(*) FROM " + TBL + " WHERE salary > 50000;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "Filtered COUNT under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE name LIKE 'user%';", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 3000;
            appendStep(steps, seq++, "LIKE scan under 3 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id IN (55, 66, 77, 88, 99);", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "IN lookup under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE age > 30 AND salary < 90000 ORDER BY salary DESC LIMIT 50;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "Complex query under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " GROUP BY age ORDER BY age;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "GROUP BY under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " ORDER BY id LIMIT 1 OFFSET 70;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "OFFSET query under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id > 0;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "All rows query under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT SUM(salary), AVG(age), COUNT(*) FROM " + TBL + ";", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 2000;
            appendStep(steps, seq++, "Multiple aggregates under 2 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE id >= 51 AND id <= 150 ORDER BY id DESC LIMIT 25;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 3000;
            appendStep(steps, seq++, "Range + sort + limit under 3 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto start = std::chrono::steady_clock::now();
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("SELECT * FROM " + TBL + " WHERE salary > 0 AND age > 0 AND id > 0 ORDER BY salary DESC, age ASC LIMIT 50;", DB, v);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            bool p = r.getSuccess() && ms < 5000;
            appendStep(steps, seq++, "Multi-condition multi-sort under 5 seconds", p, "ms=" + std::to_string(ms));
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }

        // Cleanup
        {
            auto r = exec("DROP TABLE " + TBL + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP TABLE perf_users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto r = exec("DROP TABLE " + TBL2 + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP TABLE perf_orders", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto r = exec("DROP TABLE " + TBL3 + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP TABLE perf_items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
        }
        {
            auto r = exec("DROP DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP DATABASE perf_test_db", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " P-" << (seq-1) << "\n";
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

    writeReportLog("PerformanceTest", steps);
    return ok ? 0 : 1;
}
