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

constexpr unsigned short TEST_PORT = 19086;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);

struct TestStepResult { std::string name; bool passed; std::string detail; };

std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength) {
    return {(unsigned char)(messageLength>>24), (unsigned char)(messageLength>>16),
            (unsigned char)(messageLength>>8), (unsigned char)(messageLength)}; }

std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &h) {
    return ((uint32_t)h[0]<<24)|((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|(uint32_t)h[3]; }

std::filesystem::path getReportPath() { return "ExecutorFullTestReport.md"; }

void appendStepResult(std::vector<TestStepResult> *r, const std::string &n, bool p, const std::string &d) {
    if (r) r->push_back({n,p,d}); }

void sendRawMessage(asio::ip::tcp::socket *s, const std::string &m) {
    auto h = buildLengthHeader((uint32_t)m.size());
    asio::write(*s, asio::buffer(h)); asio::write(*s, asio::buffer(m)); }

std::string receiveRawMessage(asio::ip::tcp::socket *s) {
    std::array<unsigned char,4> h{};
    asio::read(*s, asio::buffer(h)); auto len = parseLengthHeader(h);
    std::string msg(len,'\0'); asio::read(*s, asio::buffer(msg.data(),msg.size())); return msg; }

NetworkTransferData sendRecv(asio::ip::tcp::socket *s, const NetworkTransferData &r) {
    sendRawMessage(s, r.toJson()); return NetworkTransferData::fromJson(receiveRawMessage(s)); }

void connectWithRetry(asio::ip::tcp::socket *s, unsigned short port) {
    asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
    for (int i=0;i<CONNECT_RETRY_COUNT;++i) { std::error_code ec; s->connect(ep,ec);
        if (!ec) return; std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL); }
    throw std::runtime_error("connect failed"); }

void writeReport(const std::vector<TestStepResult> &steps, bool ok, const std::string &fatal) {
    auto p = getReportPath();
    std::ofstream ofs(p, std::ios::trunc); if (!ofs.good()) return;
    ofs << "# Executor Full Test Report\n\n- Overall: " << (ok?"PASS":"FAIL")
        << "\n- Report: `"<<p.string()<<"`\n\n## Steps\n\n| Step | Result | Detail |\n|---|---|---|\n";
    for (auto &s : steps) ofs << "| "<<s.name<<" | "<<(s.passed?"PASS":"FAIL")<<" | "<<s.detail<<" |\n";
    if (!fatal.empty()) ofs << "\n## Fatal\n```\n"<<fatal<<"\n```\n";
}
} // namespace

int main() {
    const std::string DB = "test_exec_db", TBL = "users", UID = "test";
    bool ok = false; std::string fatal; std::vector<TestStepResult> steps;

    Core core; std::unique_ptr<NetReceiver> recv;
    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT); recv->start();
        asio::io_context ctx; asio::ip::tcp::socket sock(ctx); connectWithRetry(&sock, TEST_PORT);

        // CREATE DATABASE
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setSql("CREATE DATABASE "+DB+";"); auto r = sendRecv(&sock, req);
          bool p = r.getType()==NetworkTransferData::SQL_EXEC_RESPONSE && r.getSuccess();
          appendStepResult(&steps,"1-CREATE DATABASE",p,r.getMessage()); }

        // USE DATABASE
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setSql("USE DATABASE "+DB+";"); auto r = sendRecv(&sock, req);
          bool p = r.getSuccess(); appendStepResult(&steps,"2-USE DATABASE",p,r.getMessage()); }

        // CREATE TABLE with constraints
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB);
          req.setSql("CREATE TABLE "+TBL+" (id INT PRIMARY KEY, name CHAR(20) NOT NULL, age INT DEFAULT 0);");
          auto r = sendRecv(&sock, req);
          auto dir = std::filesystem::path("data")/DB;
          bool p = r.getSuccess() && std::filesystem::exists(dir/(TBL+".tdf")) && std::filesystem::exists(dir/(TBL+".trd"));
          appendStepResult(&steps,"3-CREATE TABLE",p,r.getMessage()); }

        // SHOW DATABASES
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setSql("SHOW DATABASES;"); auto r = sendRecv(&sock, req);
          bool p = r.getType()==NetworkTransferData::SQL_QUERY_RESPONSE && r.getSuccess() && !r.getRows().empty();
          appendStepResult(&steps,"4-SHOW DATABASES",p,"rows="+std::to_string(r.getRows().size())); }

        // SHOW TABLES
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("SHOW TABLES;"); auto r = sendRecv(&sock, req);
          bool p = r.getType()==NetworkTransferData::SQL_QUERY_RESPONSE && r.getSuccess() && !r.getRows().empty();
          appendStepResult(&steps,"5-SHOW TABLES",p,"rows="+std::to_string(r.getRows().size())); }

        // INSERT full columns
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("INSERT INTO "+TBL+" VALUES (1, 'Alice', 25);");
          auto r = sendRecv(&sock, req);
          bool p = r.getType()==NetworkTransferData::SQL_EXEC_RESPONSE && r.getSuccess();
          appendStepResult(&steps,"6-INSERT full",p,r.getMessage()); }

        // INSERT partial
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("INSERT INTO "+TBL+" (id, name) VALUES (2, 'Bob');");
          auto r = sendRecv(&sock, req);
          bool p = r.getSuccess(); appendStepResult(&steps,"7-INSERT partial",p,r.getMessage()); }

        // INSERT NOT NULL violation
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("INSERT INTO "+TBL+" (id) VALUES (3);");
          auto r = sendRecv(&sock, req);
          bool p = !r.getSuccess(); appendStepResult(&steps,"8-INSERT NOT NULL fail",p,r.getMessage()); }

        // INSERT duplicate PK
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("INSERT INTO "+TBL+" VALUES (1, 'Dup', 30);");
          auto r = sendRecv(&sock, req);
          bool p = !r.getSuccess(); appendStepResult(&steps,"9-INSERT dup PK fail",p,r.getMessage()); }

        // DROP TABLE
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("DROP TABLE "+TBL+";"); auto r = sendRecv(&sock, req);
          bool p = r.getSuccess(); appendStepResult(&steps,"10-DROP TABLE",p,r.getMessage()); }

        // SHOW TABLES after drop
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setDbName(DB); req.setSql("SHOW TABLES;"); auto r = sendRecv(&sock, req);
          bool p = r.getSuccess() && r.getRows().empty();
          appendStepResult(&steps,"11-SHOW TABLES empty",p,"rows="+std::to_string(r.getRows().size())); }

        // DROP DATABASE
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setSql("DROP DATABASE "+DB+";"); auto r = sendRecv(&sock, req);
          bool p = r.getSuccess(); appendStepResult(&steps,"12-DROP DATABASE",p,r.getMessage()); }

        // SHOW DATABASES after drop
        { auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, UID);
          req.setSql("SHOW DATABASES;"); auto r = sendRecv(&sock, req);
          bool p = r.getSuccess();
          appendStepResult(&steps,"13-SHOW DATABASES after drop",p,"rows="+std::to_string(r.getRows().size())); }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        ok = std::all_of(steps.begin(), steps.end(), [](auto &s){ return s.passed; });
    } catch (std::exception &e) { fatal = e.what(); ok = false; }

    if (recv) recv->stop();
    writeReport(steps, ok, fatal);
    return ok ? 0 : 1;
}
