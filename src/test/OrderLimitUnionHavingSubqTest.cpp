#include <algorithm>
#include <chrono>
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

#ifndef SERVER_PROJECT_ROOT
#error SERVER_PROJECT_ROOT is not defined.
#endif

namespace {

constexpr unsigned short TEST_PORT = 19088;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);

struct TestStep { std::string name; bool passed; std::string detail; };

std::array<unsigned char, 4> buildLenHeader(std::uint32_t len) {
    return {static_cast<unsigned char>(len >> 24U), static_cast<unsigned char>(len >> 16U),
            static_cast<unsigned char>(len >> 8U), static_cast<unsigned char>(len)};
}
std::uint32_t parseLenHeader(const std::array<unsigned char, 4> &h) {
    return (static_cast<std::uint32_t>(h[0]) << 24U) | (static_cast<std::uint32_t>(h[1]) << 16U)
           | (static_cast<std::uint32_t>(h[2]) << 8U) | static_cast<std::uint32_t>(h[3]);
}
auto projRoot() { return std::filesystem::path(SERVER_PROJECT_ROOT); }
auto reportPath() { return projRoot() / "src" / "test" / "OrderLimitUnionHavingSubqTestReport.md"; }

void prepStorage() {
    auto d = projRoot() / "src" / "storage";
    std::filesystem::create_directories(d / "data");
    std::filesystem::current_path(d);
}
void cleanupDb(const std::string &n) {
    auto p = std::filesystem::path("data") / n;
    if (std::filesystem::exists(p)) std::filesystem::remove_all(p);
}
void connRetry(asio::ip::tcp::socket *s, unsigned short port) {
    asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
    for (int i = 0; i < CONNECT_RETRY_COUNT; ++i) {
        std::error_code ec; s->connect(ep, ec);
        if (!ec) return;
        std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
    }
    throw std::runtime_error("connect retry failed");
}
void sendMsg(asio::ip::tcp::socket *s, const std::string &msg) {
    auto h = buildLenHeader(static_cast<std::uint32_t>(msg.size()));
    asio::write(*s, asio::buffer(h)); asio::write(*s, asio::buffer(msg));
}
std::string recvMsg(asio::ip::tcp::socket *s) {
    std::array<unsigned char, 4> h{};
    asio::read(*s, asio::buffer(h));
    std::string msg(parseLenHeader(h), '\0');
    asio::read(*s, asio::buffer(msg.data(), msg.size()));
    return msg;
}
NetworkTransferData reqRsp(asio::ip::tcp::socket *s, const NetworkTransferData &r) {
    sendMsg(s, r.toJson()); return NetworkTransferData::fromJson(recvMsg(s));
}
void addStep(std::vector<TestStep> *v, const std::string &n, bool p, const std::string &d) {
    if (v) v->push_back({n, p, d});
}
std::string j(const std::vector<std::string> &v, const std::string &s) {
    std::string r; for (std::size_t i = 0; i < v.size(); ++i) { if (i > 0) r += s; r += v[i]; } return r;
}
void writeRpt(const std::vector<TestStep> &s, bool o, const std::string &f) {
    auto p = reportPath(); std::filesystem::create_directories(p.parent_path());
    std::ofstream of(p, std::ios::trunc);
    if (!of.good()) return;
    of << "# OrderBy/Limit/Union/Having/Subquery Test Report\n\n";
    of << "- Overall: " << (o ? "PASS" : "FAIL") << "\n\n## Steps\n\n| Step | Result | Detail |\n|---|---|---|\n";
    for (auto &x : s) of << "| " << x.name << " | " << (x.passed ? "PASS" : "FAIL") << " | " << x.detail << " |\n";
    if (!f.empty()) of << "\n## Fatal\n\n- " << f << "\n";
}

} // namespace

int main() {
    const std::string uid = "ExtTester", db = "ExtTestDb", t1 = "t1", t2 = "t2";
    std::vector<TestStep> steps; std::string fatal; bool overall = false;
    prepStorage(); cleanupDb(db);

    Core core; std::unique_ptr<NetReceiver> recv;
    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT); recv->start();
        asio::io_context ctx; asio::ip::tcp::socket sock(ctx); connRetry(&sock, TEST_PORT);

        auto exec = [&](const std::string &sql) { NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST, uid); r.setDbName(db); r.setSql(sql); return reqRsp(&sock, r); };
        auto execOk = [&](const std::string &sql, const std::string &step, const std::string &detail) { auto r = exec(sql); addStep(&steps, step, r.getSuccess(), detail.empty() ? r.getMessage() : detail); };
        auto q = [&](const std::string &sql) -> NetworkTransferData { NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST, uid); r.setDbName(db); r.setSql(sql); return reqRsp(&sock, r); };

        // Setup
        execOk("CREATE DATABASE " + db + ";", "CREATE DATABASE", "");
        execOk("USE DATABASE " + db + ";", "USE DATABASE", "");
        execOk("CREATE TABLE " + t1 + " (id INT, name VARCHAR(20), val INT);", "CREATE t1", "");
        execOk("CREATE TABLE " + t2 + " (id INT, title VARCHAR(30));", "CREATE t2", "");
        execOk("INSERT INTO " + t1 + " VALUES (1, 'a', 10);", "INSERT t1 row1", "");
        execOk("INSERT INTO " + t1 + " VALUES (2, 'b', 30);", "INSERT t1 row2", "");
        execOk("INSERT INTO " + t1 + " VALUES (3, 'c', 20);", "INSERT t1 row3", "");
        execOk("INSERT INTO " + t2 + " VALUES (1, 'alpha');", "INSERT t2 row1", "");
        execOk("INSERT INTO " + t2 + " VALUES (3, 'gamma');", "INSERT t2 row2", "");

        // === ORDER BY ===
        {
            auto r = q("SELECT id FROM " + t1 + " ORDER BY id;");
            bool ok = r.getSuccess() && r.getRows().size() == 3 && r.getRows()[0][0] == "1" && r.getRows()[2][0] == "3";
            addStep(&steps, "ORDER BY ASC", ok, "rows=" + std::to_string(r.getRows().size()) + " first=" + (r.getRows().empty()?"":r.getRows()[0][0]));
        }
        {
            auto r = q("SELECT id FROM " + t1 + " ORDER BY id DESC;");
            bool ok = r.getSuccess() && r.getRows().size() == 3 && r.getRows()[0][0] == "3";
            addStep(&steps, "ORDER BY DESC", ok, "first=" + (r.getRows().empty()?"":r.getRows()[0][0]));
        }
        {
            auto r = q("SELECT val FROM " + t1 + " ORDER BY val;");
            bool ok = r.getSuccess() && r.getRows()[0][0] == "10" && r.getRows()[2][0] == "30";
            addStep(&steps, "ORDER BY val ASC", ok, "first=" + (r.getRows().empty()?"":r.getRows()[0][0]));
        }

        // === LIMIT ===
        {
            auto r = q("SELECT id FROM " + t1 + " ORDER BY id LIMIT 2;");
            bool ok = r.getSuccess() && r.getRows().size() == 2;
            addStep(&steps, "LIMIT 2", ok, "rows=" + std::to_string(r.getRows().size()));
        }
        {
            auto r = q("SELECT id FROM " + t1 + " ORDER BY id DESC LIMIT 1;");
            bool ok = r.getSuccess() && r.getRows().size() == 1 && r.getRows()[0][0] == "3";
            addStep(&steps, "ORDER BY DESC LIMIT 1", ok, "val=" + (r.getRows().empty()?"":r.getRows()[0][0]));
        }

        // === UNION ===
        {
            auto r = q("SELECT id FROM " + t1 + " UNION SELECT id FROM " + t2 + ";");
            // t1 has ids 1,2,3; t2 has ids 1,3 → UNION = 1,2,3
            bool ok = r.getSuccess() && r.getRows().size() == 3;
            addStep(&steps, "UNION", ok, "rows=" + std::to_string(r.getRows().size()) + " msg=" + r.getMessage());
        }
        {
            auto r = q("SELECT id FROM " + t1 + " UNION ALL SELECT id FROM " + t2 + ";");
            // UNION ALL = 1,2,3,1,3 = 5
            bool ok = r.getSuccess() && r.getRows().size() == 5;
            addStep(&steps, "UNION ALL", ok, "rows=" + std::to_string(r.getRows().size()));
        }

        // === HAVING ===
        {
            auto r = q("SELECT val, COUNT(*) FROM " + t1 + " GROUP BY val HAVING COUNT(*) >= 1;");
            bool ok = r.getSuccess() && r.getRows().size() == 3;
            addStep(&steps, "HAVING COUNT>=1", ok, "rows=" + std::to_string(r.getRows().size()));
        }
        {
            auto r = q("SELECT val, COUNT(*) FROM " + t1 + " GROUP BY val HAVING val > 15;");
            bool ok = r.getSuccess() && r.getRows().size() == 2;
            addStep(&steps, "HAVING val>15", ok, "rows=" + std::to_string(r.getRows().size()));
        }

        // === JOIN + HAVING ===
        {
            auto r = q("SELECT name, COUNT(*) FROM " + t1 + " INNER JOIN " + t2
                       + " ON " + t1 + ".id = " + t2 + ".id GROUP BY name HAVING COUNT(*) >= 1;");
            bool ok = r.getSuccess() && r.getRows().size() == 2 && r.getColumns().size() >= 2;
            addStep(&steps, "JOIN+HAVING COUNT>=1", ok, "rows=" + std::to_string(r.getRows().size()) + " cols=" + j(r.getColumns(), ","));
        }

        // === SUBQUERY (WHERE id IN (SELECT...)) ===
        {
            auto r = q("SELECT name FROM " + t1 + " WHERE id IN (SELECT id FROM " + t2 + ");");
            // t2 has ids 1,3; t1 ids 1(a),2(b),3(c) → a,c returned
            bool ok = r.getSuccess() && r.getRows().size() >= 2;
            addStep(&steps, "WHERE IN subquery", ok, "rows=" + std::to_string(r.getRows().size()) + " msg=" + r.getMessage());
        }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        overall = std::all_of(steps.begin(), steps.end(), [](auto &s) { return s.passed; });
    } catch (const std::exception &e) { fatal = e.what(); overall = false; }
    if (recv) recv->stop();
    cleanupDb(db);
    writeRpt(steps, overall, fatal);
    if (!fatal.empty()) std::cerr << "FATAL: " << fatal << std::endl;
    for (auto &s : steps) std::cout << s.name << ": " << (s.passed ? "PASS" : "FAIL") << " - " << s.detail << std::endl;
    return overall ? 0 : 1;
}
