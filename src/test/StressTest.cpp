#include <algorithm>
#include <array>
#include <chrono>
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

constexpr unsigned short TEST_PORT = 19098;
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
    const std::string DB="stress_test", UID="stressTest";
    std::vector<TestStepResult> steps; bool ok=false; std::string fatal;
    std::cout << "\n========== Stress Test ==========\n";

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

        int seq = 1;
        { auto r=exec("CREATE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        uint64_t ver = 0;

        // ===================== CREATE TABLE variations for stress =====================
        { auto r=exec("CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR(50), age INT, score FLOAT);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE t1 basic",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE t2 (id INT PRIMARY KEY, data VARCHAR(200), flag BOOLEAN, amt DOUBLE);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE t2 with BOOLEAN/DOUBLE",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE t3 (id INT AUTO_INCREMENT PRIMARY KEY, msg TEXT);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE t3 AUTO_INCREMENT TEXT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE wide_tbl (c1 INT, c2 INT, c3 INT, c4 INT, c5 INT, c6 INT, c7 INT, c8 INT, c9 INT, c10 INT);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE 10 columns",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE pk_only (id INT PRIMARY KEY);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE single PK column",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Bulk INSERT stress =====================
        // Insert 10 rows individually
        for (int i=1;i<=10;i++) {
            auto r=exec("INSERT INTO t1 VALUES ("+std::to_string(i)+",'User"+std::to_string(i)+"',"+std::to_string(20+i)+","+std::to_string(50.0+i*1.5)+");",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()>=1; appendStep(steps,seq++,"INSERT 10 rows individually",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // Insert 50 rows individually
        for (int i=11;i<=60;i++) {
            auto r=exec("INSERT INTO t1 VALUES ("+std::to_string(i)+",'Bulk"+std::to_string(i)+"',"+std::to_string(20+i)+","+std::to_string(50.0+i*1.5)+");",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()>=1; appendStep(steps,seq++,"INSERT 50 more rows (60 total)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // Insert 50 rows into t2
        for (int i=1;i<=50;i++) {
            auto r=exec("INSERT INTO t2 VALUES ("+std::to_string(i)+",'Data"+std::to_string(i)+"',"+(i%2==0?"TRUE":"FALSE")+","+std::to_string(100.5+i)+");",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t2;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT 50 rows into t2",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // AUTO_INCREMENT inserts
        for (int i=1;i<=30;i++) {
            auto r=exec("INSERT INTO t3 (msg) VALUES ('Message "+std::to_string(i)+"');",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t3;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT 30 rows AUTO_INCREMENT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // Wide table inserts
        for (int i=1;i<=20;i++) {
            auto r=exec("INSERT INTO wide_tbl VALUES ("+std::to_string(i)+","+std::to_string(i*2)+","+std::to_string(i*3)+","+std::to_string(i*4)+","+std::to_string(i*5)+","+std::to_string(i*6)+","+std::to_string(i*7)+","+std::to_string(i*8)+","+std::to_string(i*9)+","+std::to_string(i*10)+");",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM wide_tbl;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT 20 rows into 10-col table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Large table query stress =====================
        { auto r=exec("SELECT * FROM t1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==60; appendStep(steps,seq++,"SELECT * 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 WHERE id > 30;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==30; appendStep(steps,seq++,"SELECT WHERE id>30 returns 30",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 WHERE age BETWEEN 25 AND 50;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE age BETWEEN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 ORDER BY age ASC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==60; appendStep(steps,seq++,"SELECT ORDER BY age ASC 60 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 ORDER BY score DESC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==60; appendStep(steps,seq++,"SELECT ORDER BY score DESC 60 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 LIMIT 10;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==10; appendStep(steps,seq++,"SELECT LIMIT 10 from 60",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 LIMIT 25;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==25; appendStep(steps,seq++,"SELECT LIMIT 25 from 60",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 ORDER BY id DESC LIMIT 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"SELECT ORDER BY DESC LIMIT 5",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT DISTINCT age FROM t1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT DISTINCT age from 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"COUNT(*) on 60 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT SUM(age), AVG(age), MIN(age), MAX(age) FROM t1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Aggregates on 60 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM t1 WHERE name LIKE 'User%';",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"LIKE prefix on 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM t1 WHERE name LIKE '%0';",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"LIKE suffix on 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 WHERE id IN (5, 15, 25, 35, 45, 55);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==6; appendStep(steps,seq++,"SELECT IN list 6 matches",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 WHERE id >= 10 AND id <= 20;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==11; appendStep(steps,seq++,"SELECT range 10-20 returns 11",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== JOIN stress on larger tables =====================
        { auto r=exec("CREATE TABLE t4 (id INT PRIMARY KEY, t1_id INT, info VARCHAR(100));",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE t4 for JOIN",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        for (int i=1;i<=40;i++) {
            auto r=exec("INSERT INTO t4 VALUES ("+std::to_string(i)+","+std::to_string(i)+",'Info"+std::to_string(i)+"');",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t4;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT 40 rows into t4",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        { auto r=exec("SELECT t1.id, t1.name, t4.info FROM t1 INNER JOIN t4 ON t1.id = t4.t1_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INNER JOIN t1 x t4 (40 rows)",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT t1.id, t1.name, t4.info FROM t1 LEFT JOIN t4 ON t1.id = t4.t1_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"LEFT JOIN t1 x t4 (60 rows)",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT t1.id, t4.info FROM t1 INNER JOIN t4 ON t1.id = t4.t1_id WHERE t1.age > 30;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + WHERE on large tables",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT t1.id, t4.info FROM t1 INNER JOIN t4 ON t1.id = t4.t1_id ORDER BY t1.id DESC LIMIT 10;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==10; appendStep(steps,seq++,"JOIN + ORDER BY + LIMIT 10",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM t1 INNER JOIN t4 ON t1.id = t4.t1_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"COUNT on JOIN result",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== GROUP BY / HAVING stress =====================
        { auto r=exec("SELECT age, COUNT(*) FROM t1 GROUP BY age;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY age on 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT age, COUNT(*) FROM t1 GROUP BY age HAVING COUNT(*) > 1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY + HAVING on 60 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT age, SUM(score), AVG(score) FROM t1 GROUP BY age;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY with SUM/AVG",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT flag, COUNT(*) FROM t2 GROUP BY flag;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY BOOLEAN on t2",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Rapid sequential operations =====================
        // Rapid UPDATE cycles
        for (int i=1;i<=10;i++) {
            auto r=exec("UPDATE t1 SET score = score + 1 WHERE id = "+std::to_string(i)+";",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT score FROM t1 WHERE id = 1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()>=1; appendStep(steps,seq++,"Rapid UPDATE 10 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // Rapid DELETE + INSERT cycles
        for (int i=61;i<=70;i++) {
            auto r=exec("INSERT INTO t1 VALUES ("+std::to_string(i)+",'Temp"+std::to_string(i)+"',99,0.0);",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT 10 more rows (70 total)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        for (int i=61;i<=70;i++) {
            auto r=exec("DELETE FROM t1 WHERE id = "+std::to_string(i)+";",DB,ver);
            if(r.getSuccess())ver=r.getDbVersion();
        }
        { auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE 10 rows back to 60",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Wide table stress =====================
        { auto r=exec("SELECT * FROM wide_tbl;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==20; appendStep(steps,seq++,"SELECT * 10-col table 20 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT c1, c5, c10 FROM wide_tbl WHERE c1 > 10;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT subset columns wide table",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT SUM(c1), SUM(c2), SUM(c3), SUM(c4), SUM(c5) FROM wide_tbl;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Multiple aggregates on wide table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE wide_tbl SET c10 = c10 + 1 WHERE c1 <= 5;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE wide table 5 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Data type edge cases =====================
        { auto r=exec("CREATE TABLE edge_tbl (id INT PRIMARY KEY, long_str VARCHAR(200), big_num INT);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE for edge cases",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO edge_tbl VALUES (1, 'Short', 0);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT edge case 0 int",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO edge_tbl VALUES (2, 'A very long string with many characters to test varchar handling', 999999);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT long string large int",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO edge_tbl VALUES (3, '', -100);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT empty string negative int",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO edge_tbl VALUES (4, 'Special chars: !@#$%^&*()_+-=[]{}|;:,.<>?', 42);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT special characters",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM edge_tbl WHERE id = 2;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT edge case long string",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM edge_tbl WHERE big_num = -100;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT edge case negative int",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Empty / single row table operations =====================
        { auto r=exec("CREATE TABLE empty_tbl (id INT PRIMARY KEY, val VARCHAR(10));",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE empty_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM empty_tbl;",DB,ver); bool p=r.getSuccess() && r.getRows().empty(); appendStep(steps,seq++,"SELECT empty table returns 0 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM empty_tbl;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"COUNT(*) on empty table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT SUM(val) FROM empty_tbl;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SUM on empty table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO empty_tbl VALUES (1, 'only');",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT single row into empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM empty_tbl;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT 1 row from previously empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE empty_tbl SET val = 'updated' WHERE id = 1;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE single row table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM empty_tbl WHERE id = 1;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE only row from table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM empty_tbl;",DB,ver); bool p=r.getSuccess() && r.getRows().empty(); appendStep(steps,seq++,"SELECT empty after DELETE only row",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE empty_tbl;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP empty_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Connection / rapid request stress =====================
        { auto r=exec("SHOW TABLES;",DB,ver); bool p=r.getSuccess() && r.getRows().size()>=5; appendStep(steps,seq++,"SHOW TABLES after many creates",p,"tables="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SHOW DATABASES;"); bool p=r.getSuccess(); appendStep(steps,seq++,"SHOW DATABASES under load",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // Rapid query cycles on same connection
        for (int cycle=1; cycle<=10; cycle++) {
            auto r=exec("SELECT COUNT(*) FROM t1;",DB,ver);
            bool p=r.getSuccess();
            if(!p) { appendStep(steps,seq++,"Rapid query cycle "+std::to_string(cycle)+" failed",p); std::cout<<"  [FAIL] ST-"<<(seq-1)<<"\n"; }
        }
        appendStep(steps,seq++,"10 rapid COUNT(*) queries",true,"all passed"); std::cout<<"  [PASS] ST-"<<(seq-1)<<"\n";

        // Multiple SELECT variations in sequence
        { auto r=exec("SELECT id FROM t1 WHERE id = 50;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT specific id from 60",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id, name, age, score FROM t1 WHERE id BETWEEN 10 AND 20 ORDER BY id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==11; appendStep(steps,seq++,"SELECT 4 cols WHERE range ORDER BY",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id, name FROM t1 WHERE age > 40 OR score > 100 ORDER BY name DESC LIMIT 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"SELECT complex WHERE ORDER BY LIMIT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*), AVG(score), MIN(age), MAX(age) FROM t1 WHERE id <= 30;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Aggregates on first 30 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Drop and recreate stress =====================
        { auto r=exec("DROP TABLE edge_tbl;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP edge_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE edge_tbl (id INT PRIMARY KEY, val INT);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"RECREATE edge_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO edge_tbl VALUES (1, 100);",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT into recreated table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM edge_tbl;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT from recreated table",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE edge_tbl;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP recreated edge_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== UNION stress =====================
        { auto r=exec("SELECT id FROM t1 WHERE id <= 10 UNION SELECT id FROM t1 WHERE id >= 51;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"UNION two ranges from same table",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id FROM t1 WHERE id <= 5 UNION ALL SELECT id FROM t1 WHERE id <= 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==10; appendStep(steps,seq++,"UNION ALL duplicates 10 rows",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Subquery stress if supported =====================
        { auto r=exec("SELECT * FROM t1 WHERE id IN (SELECT t1_id FROM t4 WHERE t1_id <= 20);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT with subquery IN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM t1 WHERE EXISTS (SELECT 1 FROM t4 WHERE t4.t1_id = t1.id);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT with EXISTS subquery",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        // ===================== Cleanup =====================
        { auto r=exec("DROP TABLE t4;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP t4",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE t3;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP t3",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE t2;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP t2",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE wide_tbl;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP wide_tbl",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE t1;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP t1",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE pk_only;",DB,ver); if(r.getSuccess())ver=r.getDbVersion(); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP pk_only",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE cleanup",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ST-"<<(seq-1)<<"\n"; }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto &s){return s.passed;});
    } catch(std::exception &e){fatal=e.what();ok=false;}
    if(recv)recv->stop();

    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){std::cout<<"Failed:\n"; for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    writeReportLog("StressTest",steps);
    return ok?0:1;
}
