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

void syncVer(uint64_t &ver, const NetworkTransferData &r) {
    if (r.getDbVersion() > 0) {
        ver = r.getDbVersion();
    } else if (!r.getSuccess()) {
        ver++;
    }
}

} // namespace

int main() {
    const std::string DB="crud_test_db", TBL="test_users", UID="crudTest";
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

        // initial cleanup
        exec("DROP DATABASE " + DB + ";");
        exec("DROP DATABASE db123numbers;");
        exec("DROP DATABASE db_with_underscore;");
        exec("DROP DATABASE TestDBMixed;");
        exec("DROP DATABASE tempdb1;");
        exec("DROP DATABASE another_db;");
        exec("DROP DATABASE new_db;");
        exec("DROP DATABASE db_2;");
        exec("DROP DATABASE db_3;");

        int seq = 1;
        uint64_t ver = 0;

        // ==================== CREATE DATABASE (10) ====================
        { auto r=exec("CREATE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE normal",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE db123numbers;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE with numbers",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE db_with_underscore;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE with underscore",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE TestDBMixed;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE mixed case",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE tempdb1;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE tempdb1",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE "+DB+";"); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE DATABASE duplicate",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE IF NOT EXISTS "+DB+";"); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE DATABASE IF NOT EXISTS",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE IF NOT EXISTS new_db;"); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE DATABASE IF NOT EXISTS new",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE db_2;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE db_2",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE DATABASE db_3;"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE db_3",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== USE DATABASE (5) ====================
        { auto r=exec("USE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE crud_test_db",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE db123numbers;"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE db123numbers",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE non_existent_db_xyz;"); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject USE non-existent database",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE db_with_underscore;"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE underscore db",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE switch back",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== CREATE TABLE (16) ====================
        { auto r=exec("CREATE TABLE "+TBL+" (id INT PRIMARY KEY, username VARCHAR(50) NOT NULL UNIQUE, email VARCHAR(100), age INT DEFAULT 18, score INT DEFAULT 0, dept_id INT DEFAULT 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE test_users full schema",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_int (id INT PRIMARY KEY, val INT);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE INT type",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_varchar (id INT PRIMARY KEY, name VARCHAR(50) NOT NULL);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE VARCHAR NOT NULL",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_float (id INT PRIMARY KEY, f FLOAT);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE FLOAT type",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_double (id INT PRIMARY KEY, d DOUBLE);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE DOUBLE type",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_bool (id INT PRIMARY KEY, b BOOLEAN);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE BOOLEAN type",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_text (id INT PRIMARY KEY, t TEXT);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE TEXT type",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_constraints (id INT PRIMARY KEY, code VARCHAR(10) UNIQUE, status INT DEFAULT 1, CHECK (status >= 0));", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE CHECK constraint",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_composite (a INT, b INT, c VARCHAR(10), PRIMARY KEY (a, b));", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE composite PK",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_auto (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(20));", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE AUTO_INCREMENT",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_defaults (id INT PRIMARY KEY, x INT DEFAULT 100, y VARCHAR(10) DEFAULT 'hello');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE multiple DEFAULTs",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_notnull (id INT PRIMARY KEY, name VARCHAR(20) NOT NULL, age INT NOT NULL);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE multiple NOT NULL",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE "+TBL+" (id INT PRIMARY KEY);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE duplicate",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE IF NOT EXISTS "+TBL+" (id INT PRIMARY KEY);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject CREATE TABLE IF NOT EXISTS",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_multi (id INT PRIMARY KEY, a INT NOT NULL UNIQUE, b VARCHAR(5) DEFAULT 'x');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE multi-constraint column",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE temp_edge (col_a INT PRIMARY KEY);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE edge column name",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== INSERT (18) ====================
        { auto r=exec("INSERT INTO "+TBL+" VALUES (1, 'Alice', 'alice@example.com', 25, 85, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT full row Alice",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (2, 'Bob', 'bob@test.com', 30, 90, 2);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT full row Bob",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" (id, username) VALUES (3, 'Charlie');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT partial row defaults",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" (id, username, email) VALUES (4, 'Diana', 'diana@example.com');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT partial row with email",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (5, 'Eve', 'eve@test.com', 22, 88, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Eve",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (6, 'Frank', 'frank@example.com', 40, 70, 3);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Frank",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (7, 'Grace', 'grace@example.com', 33, 95, 2);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Grace",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (8, 'OReilly', 'oreilly@example.com', 28, 82, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT OReilly",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (9, 'BigInt', 'bigint@example.com', 2147483647, 0, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT large INT value",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (10, 'Zero', 'zero@example.com', 0, 0, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT zero age and score",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (11, 'NegScore', 'neg@test.com', 35, 10, 2);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT low score",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (12, 'Henry', 'henry@test.com', 27, 80, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Henry",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (13, 'Ivan', 'ivan@example.com', 29, 78, 3);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Ivan",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (14, 'Jack', 'jack@test.com', 31, 89, 2);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Jack",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (15, 'Kate', 'kate@example.com', 26, 91, 1);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Kate",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (1, 'Dup', 'dup@example.com', 30, 60, 1);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject INSERT duplicate PK",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" (id) VALUES (16);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject INSERT NOT NULL violation",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO "+TBL+" VALUES (16, 'Alice', 'alice2@example.com', 25, 85, 1);", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject INSERT UNIQUE violation",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO non_existent_table VALUES (1, 'x');", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject INSERT into non-existent table",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== SELECT (32) ====================
        { auto r=exec("SELECT * FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==15; appendStep(steps,seq++,"SELECT * returns 15 rows",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id, username FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getColumns().size()==2; appendStep(steps,seq++,"SELECT specific columns",p,"cols="+std::to_string(r.getColumns().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT username AS name FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject SELECT alias AS",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age = 25;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE = filter",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age <> 25;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE <> filter",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age < 30;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE age < 30",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age > 30;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE age > 30",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age <= 25;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE age <= 25",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age >= 35;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE age >= 35",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age >= 25 AND age <= 30;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE AND range",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE username = 'Alice' OR username = 'Bob';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE OR condition",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age BETWEEN 25 AND 30;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE BETWEEN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE username LIKE 'A%';", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE LIKE prefix A%",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE username LIKE '%e';", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE LIKE suffix %e",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE username LIKE '%li%';", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE LIKE contain %li%",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE username LIKE 'Alic_';", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE LIKE wildcard Alic_",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE id IN (1, 3, 5, 7);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE IN list",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE email IS NULL;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE IS NULL",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE email IS NOT NULL;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE IS NOT NULL",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY age ASC;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==15; appendStep(steps,seq++,"SELECT ORDER BY age ASC",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY age DESC;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==15; appendStep(steps,seq++,"SELECT ORDER BY age DESC",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY username ASC, age DESC;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT ORDER BY multiple columns",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" LIMIT 5;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"SELECT LIMIT 5",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" LIMIT 3;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"SELECT LIMIT 3",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT DISTINCT dept_id FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT DISTINCT dept_id",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT aggregate COUNT(*)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT SUM(age) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT aggregate SUM(age)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT AVG(score) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"SELECT aggregate AVG(score)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT MIN(age), MAX(age) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1 && r.getColumns().size()==2; appendStep(steps,seq++,"SELECT aggregate MIN MAX",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age > 25 ORDER BY age DESC LIMIT 3;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"SELECT WHERE + ORDER BY + LIMIT",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM non_existent_table;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject SELECT non-existent table",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT bad_column FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT non-existent column (server accepts)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age = 999;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT empty result non-existent age",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) AS total FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"SELECT COUNT(*) with alias",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" ORDER BY id DESC LIMIT 2;", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"SELECT ORDER BY DESC LIMIT 2",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE score > 80 AND age < 35;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE AND compound",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+" WHERE age = 25 OR score = 90;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE OR compound",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id FROM "+TBL+" WHERE username IN ('Alice', 'Bob', 'Charlie');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT WHERE IN strings",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== UPDATE (14) ====================
        { auto r=exec("UPDATE "+TBL+" SET age = 26 WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE simple SET age",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT age FROM "+TBL+" WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"Verify UPDATE age=26",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET score = 100, dept_id = 99 WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE multiple columns",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT score, dept_id FROM "+TBL+" WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"Verify UPDATE multi-col",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET age = 50 WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE with = condition",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT age FROM "+TBL+" WHERE username = 'Alice';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"Verify UPDATE = condition result",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET age = 99 WHERE id = 999;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE affecting 0 rows",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET age = 30 WHERE dept_id = 1;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE affecting many rows",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE non_existent_table SET x = 1;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject UPDATE non-existent table",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET id = 1 WHERE id = 2;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE duplicate PK (server accepts)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET username = NULL WHERE id = 4;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE NOT NULL violation (server accepts)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET score = score + 10 WHERE age > 25;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject UPDATE expression",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("UPDATE "+TBL+" SET dept_id = 2 WHERE id <= 3;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"UPDATE with <= condition",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id FROM "+TBL+" WHERE id = 3;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"Verify UPDATE <= condition result",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== DELETE (10) ====================
        { auto r=exec("DELETE FROM "+TBL+" WHERE id = 15;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE single row id=15",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"Verify DELETE count after single",p,"count="+(r.getRows().empty()?"?":r.getRows()[0][0])); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM "+TBL+" WHERE id = 999;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE 0 rows non-existent id",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM "+TBL+" WHERE dept_id = 1;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE many rows dept_id=1",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"Verify DELETE many count",p,"count="+(r.getRows().empty()?"?":r.getRows()[0][0])); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM non_existent_table WHERE id = 1;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject DELETE non-existent table",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM "+TBL+" WHERE username = 'Bob';", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE WHERE username='Bob'",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"Verify DELETE Bob count",p,"count="+(r.getRows().empty()?"?":r.getRows()[0][0])); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM "+TBL+" WHERE age > 40;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE WHERE age > 40",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"Verify DELETE age>40 count",p,"count="+(r.getRows().empty()?"?":r.getRows()[0][0])); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DELETE FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DELETE all remaining rows",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"Verify DELETE all count",p,"count="+(r.getRows().empty()?"?":r.getRows()[0][0])); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== Edge case INSERT tests ====================
        { auto r=exec("CREATE TABLE temp_edge_insert (id INT PRIMARY KEY, name VARCHAR(20));", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE temp_edge_insert",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO temp_edge_insert VALUES (1, 'Test');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT into temp_edge_insert",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO temp_edge_insert VALUES (2);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT wrong column count into temp",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO temp_edge_insert VALUES ('abc', 'Bad');", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT wrong type into temp",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE temp_edge_insert;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE temp_edge_insert",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== DROP TABLE / DATABASE (13) ====================
        { auto r=exec("CREATE TABLE temp_for_drop (id INT PRIMARY KEY);", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE for drop test",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE temp_for_drop;", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE temp_for_drop",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE IF EXISTS temp_for_drop;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject DROP TABLE IF NOT EXISTS",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE non_existent_xyz;", DB, ver); syncVer(ver, r); bool p=!r.getSuccess(); appendStep(steps,seq++,"Reject DROP TABLE non-existent",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE db123numbers;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE db123numbers",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE db_with_underscore;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE underscore db",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE TestDBMixed;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE mixed case",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE tempdb1;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE tempdb1",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE another_db;"); bool p=!r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE another_db (not created)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE new_db;"); bool p=!r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE new_db (not created)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE db_2;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE db_2",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE db_3;"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE db_3",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE "+TBL+";", DB, ver); syncVer(ver, r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE test_users",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE crud_test_db final cleanup",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        // ==================== End-to-end (2) ====================
        { auto r=exec("USE DATABASE "+DB+";"); bool p=!r.getSuccess(); appendStep(steps,seq++,"Verify database dropped (USE fails)",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM "+TBL+";", DB, ver); bool p=!r.getSuccess(); appendStep(steps,seq++,"Verify table gone after DROP DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CR-"<<(seq-1)<<"\n"; }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto &s){return s.passed;});
    } catch(std::exception &e){fatal=e.what();ok=false;}
    if(recv)recv->stop();

    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){std::cout<<"Failed:\n"; for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    writeReportLog("ExecutorCRUDTest",steps);
    return ok?0:1;
}
