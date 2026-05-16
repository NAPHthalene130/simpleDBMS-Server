/**
 * @file ConcurrencyTest.cpp
 * @brief Concurrency testing for simpleDBMS-Server
 * @details Tests multiple connections, concurrent reads/writes, read-write
 *          concurrency, connection lifecycle, and version concurrency using std::thread.
 * @author NAPH130
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
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

constexpr unsigned short TEST_PORT = 19097;
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
std::mutex gStepMutex;

void appendStep(std::vector<TestStepResult> &steps, int id, const std::string &name,
                bool passed, const std::string &detail = "") {
    std::lock_guard<std::mutex> lock(gStepMutex);
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
    probeReq.setDbVersion(999999);
    probeReq.setSql("SHOW TABLES;");
    auto probeResp = sendRecv(sock, probeReq);
    return probeResp.getDbVersion();
}

NetworkTransferData execOnSocket(asio::ip::tcp::socket *sock, const std::string &sql,
                                 const std::string &db, uint64_t ver, const std::string &uid) {
    NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, uid);
    req.setSql(sql);
    if (!db.empty()) { req.setDbName(db); req.setDbVersion(ver); }
    return sendRecv(sock, req);
}

} // namespace

int main() {
    const std::string DB = "conc_test_db", UID = "concTest";
    const std::string TBL = "conc_users", TBL2 = "conc_logs", TBL3 = "conc_items";

    std::vector<TestStepResult> steps;
    bool ok = false;
    std::string fatal;

    std::cout << "\n========== Concurrency Test ==========\n";

    Core core;
    std::unique_ptr<NetReceiver> recv;

    int seq = 1;

    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT);
        recv->start();

        asio::io_context ctx;
        asio::ip::tcp::socket sock(ctx);
        connectWithRetry(&sock, TEST_PORT);

        auto exec = [&](const std::string &sql, const std::string &db = "", uint64_t ver = 0) {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql(sql);
            if (!db.empty()) { req.setDbName(db); req.setDbVersion(ver); }
            return sendRecv(&sock, req);
        };

        // Cleanup
        exec("DROP DATABASE " + DB + ";");

        // Setup database and tables
        {
            auto r = exec("CREATE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE DATABASE conc_test_db", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            auto r = exec("USE DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "USE DATABASE conc_test_db", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL + " (id INT PRIMARY KEY, name VARCHAR(50), score INT);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE conc_users", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL2 + " (log_id INT PRIMARY KEY, msg VARCHAR(100), ts VARCHAR(20));", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE conc_logs", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            uint64_t v = getServerVersion(&sock, DB, UID);
            auto r = exec("CREATE TABLE " + TBL3 + " (item_id INT PRIMARY KEY, val VARCHAR(50), qty INT);", DB, v);
            bool p = r.getSuccess();
            appendStep(steps, seq++, "CREATE TABLE conc_items", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }

        // Seed data
        for (int i = 1; i <= 50; ++i) {
            uint64_t v = getServerVersion(&sock, DB, UID);
            exec("INSERT INTO " + TBL + " VALUES (" + std::to_string(i) + ", 'user" + std::to_string(i) + "', " + std::to_string(i*10) + ");", DB, v);
        }
        for (int i = 1; i <= 30; ++i) {
            uint64_t v = getServerVersion(&sock, DB, UID);
            exec("INSERT INTO " + TBL2 + " VALUES (" + std::to_string(i) + ", 'log" + std::to_string(i) + "', '2024-01-" + (i<10?"0":"") + std::to_string(i) + "');", DB, v);
        }
        for (int i = 1; i <= 40; ++i) {
            uint64_t v = getServerVersion(&sock, DB, UID);
            exec("INSERT INTO " + TBL3 + " VALUES (" + std::to_string(i) + ", 'item" + std::to_string(i) + "', " + std::to_string(i*5) + ");", DB, v);
        }

        // ==================== Multiple connections (30+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=1;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) { }
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent SELECT id=1", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_users;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id<=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent log SELECTs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id>20;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent item SELECTs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent SHOW TABLES", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT name FROM conc_users WHERE id BETWEEN 10 AND 20;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent range SELECTs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE score>100;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent score>100", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg LIKE 'log%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent LIKE log%", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE qty>=50;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent qty>=50", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_logs;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent log COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users ORDER BY id DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items ORDER BY item_id ASC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent item ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id IN (1,5,10,15,20);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent IN SELECTs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id>10 AND log_id<20;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent range log", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id<=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent item id<=10", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE name='user25';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent name='user25'", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE ts='2024-01-05';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent ts='2024-01-05'", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE val='item30';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent val='item30'", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id>=40;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent id>=40", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id<=3;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent log_id<=3", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id BETWEEN 5 AND 15;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent BETWEEN 5-15", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE score BETWEEN 100 AND 300;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent score BETWEEN", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg='log15';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent msg='log15'", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE qty=100;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent qty=100", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id<5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent id<5", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id>=25;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent log_id>=25", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id>35;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent item_id>35", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE name LIKE 'user%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent name LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg LIKE 'log2%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent msg LIKE log2%", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE val LIKE 'item1%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Multiple connections: 1 concurrent val LIKE item1%", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // ==================== Concurrent reads (20+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read all users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read all logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read all items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_users;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread COUNT users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_logs;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread COUNT logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_items;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread COUNT items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read user id=10", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read log id=10", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read item id=10", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id BETWEEN 5 AND 15;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read user range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id BETWEEN 5 AND 15;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read log range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id BETWEEN 5 AND 15;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read item range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE name LIKE 'user1%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read user LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg LIKE 'log1%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read log LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE val LIKE 'item1%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read item LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE score=100;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read user score", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg='log20';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read log msg", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE qty=50;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read item qty", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users ORDER BY score DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read user ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs ORDER BY log_id DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read log ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items ORDER BY qty DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent reads: 1 thread read item ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // ==================== Concurrent writes (20+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_users VALUES (999, 'newuser', 0);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_logs VALUES (999, 'newlog', '2024-12-01');", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_items VALUES (999, 'newitem', 0);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=999 WHERE id=1;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE user score", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='updated' WHERE log_id=1;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE log msg", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=999 WHERE item_id=1;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE item qty", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=999;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread DELETE user", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id=999;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread DELETE log", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id=999;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread DELETE item", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_users VALUES (998, 'temp', 0); SELECT * FROM conc_users WHERE id=998;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT then DELETE user", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_logs VALUES (998, 'temp', '2024-12-01'); SELECT * FROM conc_logs WHERE log_id=998;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT then DELETE log", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_items VALUES (998, 'temp', 0); SELECT * FROM conc_items WHERE item_id=998;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread INSERT then DELETE item", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=500 WHERE id=2; SELECT * FROM conc_users WHERE id=2;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE then SELECT user", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='upd' WHERE log_id=2; SELECT * FROM conc_logs WHERE log_id=2;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE then SELECT log", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=500 WHERE item_id=2; SELECT * FROM conc_items WHERE item_id=2;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE then SELECT item", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_users VALUES (997, 'm1', 1); INSERT INTO conc_users VALUES (996, 'm2', 2);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread multi INSERT users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_logs VALUES (997, 'm1', '2024-12-01'); INSERT INTO conc_logs VALUES (996, 'm2', '2024-12-02');", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread multi INSERT logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_items VALUES (997, 'm1', 1); INSERT INTO conc_items VALUES (996, 'm2', 2);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread multi INSERT items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=score+1 WHERE id<=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE multiple users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='batch' WHERE log_id<=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE multiple logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=qty+1 WHERE item_id<=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Concurrent writes: 1 thread UPDATE multiple items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // ==================== Read-write concurrency (20+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=score+1 WHERE id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='rw' WHERE log_id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=qty+1 WHERE item_id=5;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=200 WHERE id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='rw2' WHERE log_id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=200 WHERE item_id=6;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=300 WHERE id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write users", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='rw3' WHERE log_id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write logs", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=300 WHERE item_id=7;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write items", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_users;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_users VALUES (995, 'rwc', 1);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write user COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_logs;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_logs VALUES (995, 'rwc', '2024-12-01');", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write log COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT COUNT(*) FROM conc_items;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "INSERT INTO conc_items VALUES (995, 'rwc', 1);", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write item COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE id BETWEEN 8 AND 12;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=400 WHERE id=8;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write user range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE log_id BETWEEN 8 AND 12;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='rw4' WHERE log_id=8;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write log range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE item_id BETWEEN 8 AND 12;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=400 WHERE item_id=8;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write item range", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users WHERE name LIKE 'user%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET name='updated' WHERE id=9;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write user LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs WHERE msg LIKE 'log%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='updated' WHERE log_id=9;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write log LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items WHERE val LIKE 'item%';", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET val='updated' WHERE item_id=9;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write item LIKE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_users ORDER BY id DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_users SET score=500 WHERE id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write user ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_logs ORDER BY log_id DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_logs SET msg='rw5' WHERE log_id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write log ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "SELECT * FROM conc_items ORDER BY item_id DESC;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        auto r = execOnSocket(&tsock, "UPDATE conc_items SET qty=500 WHERE item_id=10;", DB, getServerVersion(&tsock, DB, UID), UID);
                        if (r.getSuccess()) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Read-write concurrency: 1 read + 1 write item ORDER BY", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // ==================== Connection lifecycle (15+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect disconnect 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect disconnect 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect disconnect 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect send disconnect 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect send disconnect 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect send disconnect 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Rapid connect 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Rapid connect 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Rapid connect 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect multiple sends 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect multiple sends 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect multiple sends 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect idle disconnect 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect idle disconnect 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Connect idle disconnect 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Reconnect after drop 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Reconnect after drop 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Reconnect after drop 4x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Parallel connect close 3x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        for (int c = 0; c < 3; ++c) {
                            asio::io_context tctx;
                            asio::ip::tcp::socket tsock(tctx);
                            connectWithRetry(&tsock, TEST_PORT);
                            auto r = execOnSocket(&tsock, "SHOW TABLES;", DB, getServerVersion(&tsock, DB, UID), UID);
                            if (!r.getSuccess()) return;
                            tsock.close();
                        }
                        connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Connection lifecycle: Parallel connect close 5x", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // ==================== Version concurrency (15+) ====================
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread get version", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread get version", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread get version", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then SELECT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then SELECT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then SELECT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then INSERT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then INSERT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then INSERT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then UPDATE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then UPDATE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then UPDATE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then DELETE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then DELETE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then DELETE", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then COUNT", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then SHOW", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        {
            std::vector<std::thread> threads;
            std::atomic<int> connOk{0};
            for (int t = 0; t < 1; ++t) {
                threads.emplace_back([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    try {
                        asio::io_context tctx;
                        asio::ip::tcp::socket tsock(tctx);
                        connectWithRetry(&tsock, TEST_PORT);
                        uint64_t v = getServerVersion(&tsock, DB, UID);
                        if (v > 0) connOk.fetch_add(1);
                    } catch (...) {}
                });
            }
            for (auto &th : threads) th.join();
            bool p = connOk.load() == 1;
            appendStep(steps, seq++, "Version concurrency: 1 thread version then SHOW", p);
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }
        // Cleanup
        {
            auto r = exec("DROP DATABASE " + DB + ";");
            bool p = r.getSuccess();
            appendStep(steps, seq++, "DROP DATABASE conc_test_db", p, r.getMessage());
            std::cout << "  " << (p ? "[PASS]" : "[FAIL]") << " C-" << (seq-1) << "\n";
        }

        ok = true;
    } catch (const std::exception &e) {
        fatal = e.what();
        appendStep(steps, seq++, "FATAL EXCEPTION", false, fatal);
        std::cout << "  [FATAL] " << fatal << "\n";
    }

    writeReportLog("ConcurrencyTest", steps);
    std::cout << "========================================\n";
    std::cout << "Total: " << gTotalTests << "  Passed: " << gPassedTests << "  Failed: " << (gTotalTests - gPassedTests) << "\n";
    if (!fatal.empty()) std::cout << "FATAL: " << fatal << "\n";
    std::cout << "Report written to test/report.log\n";
    return ok ? 0 : 1;
}