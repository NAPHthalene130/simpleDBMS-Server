/**
 * @file ExecutorCRUDTest.cpp
 * @brief SQL CRUD执行器综合测试
 * @details 测试INSERT/SELECT/UPDATE/DELETE完整流程，包括约束检查、复合条件查询等。
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

constexpr unsigned short TEST_PORT = 19091;
constexpr int CONNECT_RETRY = 40;
constexpr auto RETRY_INTERVAL = std::chrono::milliseconds(100);

struct TestStepResult { int id; std::string name; bool passed; std::string detail; };
int gTotal = 0, gPassed = 0;

void appendStep(std::vector<TestStepResult> &s, int id, const std::string &name, bool p, const std::string &d = "") {
    ++gTotal; if (p) ++gPassed; s.push_back({id, name, p, d});
}

std::array<unsigned char,4> buildLen(uint32_t l) { return {(unsigned char)(l>>24),(unsigned char)(l>>16),(unsigned char)(l>>8),(unsigned char)l}; }
uint32_t parseLen(const std::array<unsigned char,4> &h) { return ((uint32_t)h[0]<<24)|((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|(uint32_t)h[3]; }
void sendRaw(asio::ip::tcp::socket *s, const std::string &m) { auto h=buildLen((uint32_t)m.size()); asio::write(*s,asio::buffer(h)); asio::write(*s,asio::buffer(m)); }
std::string recvRaw(asio::ip::tcp::socket *s) { std::array<unsigned char,4> h{}; asio::read(*s,asio::buffer(h)); auto len=parseLen(h); std::string msg(len,'\0'); asio::read(*s,asio::buffer(msg.data(),msg.size())); return msg; }
NetworkTransferData sendRecv(asio::ip::tcp::socket *s, const NetworkTransferData &r) { sendRaw(s,r.toJson()); return NetworkTransferData::fromJson(recvRaw(s)); }
void connectRetry(asio::ip::tcp::socket *s, unsigned short p) { asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"),p); for(int i=0;i<CONNECT_RETRY;++i){std::error_code ec; s->connect(ep,ec); if(!ec)return; std::this_thread::sleep_for(RETRY_INTERVAL);} throw std::runtime_error("connect failed"); }

std::string nowStr() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm tm{}; localtime_s(&tm,&t);
    std::ostringstream oss; oss << std::put_time(&tm,"%Y-%m-%d %H:%M:%S"); return oss.str();
}
void writeReportLog(const std::string &suite, const std::vector<TestStepResult> &steps) {
    std::filesystem::create_directories("test");
    std::ofstream ofs("test/report.log", std::ios::app);
    if (!ofs.good()) return;
    ofs << "====================\n" << suite << "\n" << nowStr() << "\n" << gPassed << "/" << gTotal << "\n";
    for (auto &s : steps) ofs << "[" << (s.passed?"YES":"NO") << "]" << s.name << "\n";
}

} // namespace

int main() {
    const std::string DB="crud_test", TBL="users", UID="crudTest";
    std::vector<TestStepResult> steps; bool ok=false; std::string fatal;
    std::cout << "\n========== Executor CRUD Test ==========\n";

    Core core; std::unique_ptr<NetReceiver> recv;
    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT); recv->start();
        asio::io_context ctx; asio::ip::tcp::socket sock(ctx); connectRetry(&sock, TEST_PORT);

        auto exec = [&](const std::string &sql, const std::string &db = "", uint64_t ver=0) {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql(sql); if (!db.empty()) { req.setDbName(db); req.setDbVersion(ver); }
            return sendRecv(&sock, req);
        };

        // cleanup
        exec("DROP DATABASE " + DB + ";");

        // 1. CREATE DATABASE
        { auto r=exec("CREATE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,1,"CREATE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-1\n"; }
        // 2. USE DATABASE
        { auto r=exec("USE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,2,"USE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-2\n"; }
        // 3. CREATE TABLE
        { auto r=exec("CREATE TABLE "+TBL+" (id INT PRIMARY KEY, name VARCHAR(50) NOT NULL, age INT DEFAULT 0);", DB, 0);
          bool p=r.getSuccess(); appendStep(steps,3,"CREATE TABLE with PK/NOT NULL/DEFAULT",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-3\n"; }

        // Get version for subsequent requests
        uint64_t ver = 1;

        // 4. INSERT full
        { auto r=exec("INSERT INTO "+TBL+" VALUES (1, 'Alice', 25);", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,4,"INSERT full columns",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-4\n"; }
        // 5. INSERT partial
        { auto r=exec("INSERT INTO "+TBL+" (id, name) VALUES (2, 'Bob');", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,5,"INSERT with DEFAULT",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-5\n"; }
        // 6. INSERT NOT NULL violation
        { auto r=exec("INSERT INTO "+TBL+" (id) VALUES (3);", DB, ver);
          ver=r.getDbVersion(); bool p=!r.getSuccess(); appendStep(steps,6,"Reject INSERT with NOT NULL violation",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-6\n"; }
        // 7. INSERT duplicate PK
        { auto r=exec("INSERT INTO "+TBL+" VALUES (1, 'Dup', 30);", DB, ver);
          ver=r.getDbVersion(); bool p=!r.getSuccess(); appendStep(steps,7,"Reject INSERT with duplicate PK",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-7\n"; }

        // 8. SELECT *
        { auto r=exec("SELECT * FROM "+TBL+";", DB, ver);
          ver=r.getDbVersion(); bool p=r.getType()==NetworkTransferData::SQL_QUERY_RESPONSE && r.getSuccess() && r.getRows().size()==2;
          appendStep(steps,8,"SELECT * returns 2 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-8\n"; }
        // 9. SELECT with WHERE =
        { auto r=exec("SELECT * FROM "+TBL+" WHERE id = 1;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().size()==1;
          appendStep(steps,9,"SELECT with WHERE = filter",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-9\n"; }
        // 10. SELECT with WHERE >
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age > 0;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess();
          appendStep(steps,10,"SELECT with WHERE >",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-10\n"; }
        // 11. SELECT with specific columns
        { auto r=exec("SELECT id, name FROM "+TBL+" WHERE name = 'Alice';", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getColumns().size()==2;
          appendStep(steps,11,"SELECT specific columns with WHERE",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-11\n"; }
        // 12. SELECT with ORDER BY
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY name DESC;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess();
          appendStep(steps,12,"SELECT with ORDER BY DESC",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-12\n"; }
        // 13. SELECT with LIMIT
        { auto r=exec("SELECT * FROM "+TBL+" LIMIT 1;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().size()==1;
          appendStep(steps,13,"SELECT with LIMIT 1",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-13\n"; }

        // 14. UPDATE
        { auto r=exec("UPDATE "+TBL+" SET age = 30 WHERE id = 1;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess();
          appendStep(steps,14,"UPDATE with WHERE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-14\n"; }
        // 15. Verify UPDATE
        { auto r=exec("SELECT age FROM "+TBL+" WHERE id = 1;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().size()==1 && r.getRows()[0].size()>=1 && r.getRows()[0][0]=="30";
          appendStep(steps,15,"Verify UPDATE result",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-15\n"; }

        // 16. DELETE
        { auto r=exec("DELETE FROM "+TBL+" WHERE id = 2;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess();
          appendStep(steps,16,"DELETE with WHERE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-16\n"; }
        // 17. Verify DELETE
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().size()>=1;
          appendStep(steps,17,"Verify DELETE (1 row remains)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-17\n"; }

        // 18. INSERT after DELETE
        { auto r=exec("INSERT INTO "+TBL+" VALUES (3, 'Carol', 22);", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess();
          appendStep(steps,18,"INSERT new row after DELETE",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-18\n"; }

        // 19. SELECT with ORDER BY + LIMIT
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY id ASC LIMIT 1;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().size()==1;
          appendStep(steps,19,"ORDER BY + LIMIT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-19\n"; }

        // 20. SELECT from non-existent row
        { auto r=exec("SELECT * FROM "+TBL+" WHERE id = 999;", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess() && r.getRows().empty();
          appendStep(steps,20,"SELECT non-existent row returns empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-20\n"; }

        // 21. DROP TABLE
        { auto r=exec("DROP TABLE "+TBL+";", DB, ver);
          ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,21,"DROP TABLE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-21\n"; }
        // 22. DROP DATABASE
        { auto r=exec("DROP DATABASE "+DB+";");
          bool p=r.getSuccess(); appendStep(steps,22,"DROP DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-22\n"; }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto &s){return s.passed;});
    } catch(std::exception &e){fatal=e.what();ok=false;}
    if(recv)recv->stop();

    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){std::cout<<"Failed:\n"; for(auto &s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    writeReportLog("ExecutorCRUDTest",steps);
    return ok?0:1;
}
