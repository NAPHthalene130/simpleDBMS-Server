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
constexpr unsigned short TEST_PORT=19094;
constexpr int CR=40;constexpr auto RI=std::chrono::milliseconds(100);
struct TestStepResult{int id;std::string name;bool passed;std::string detail;};
int gTotal=0,gPassed=0;
void as(std::vector<TestStepResult>&s,int id,const std::string&n,bool p,const std::string&d=""){++gTotal;if(p)++gPassed;s.push_back({id,n,p,d});}
std::array<unsigned char,4>bl(uint32_t l){return{(unsigned char)(l>>24),(unsigned char)(l>>16),(unsigned char)(l>>8),(unsigned char)l};}
uint32_t pl(const std::array<unsigned char,4>&h){return((uint32_t)h[0]<<24)|((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|(uint32_t)h[3];}
void sr(asio::ip::tcp::socket*s,const std::string&m){auto h=bl((uint32_t)m.size());asio::write(*s,asio::buffer(h));asio::write(*s,asio::buffer(m));}
std::string rr(asio::ip::tcp::socket*s){std::array<unsigned char,4>h{};asio::read(*s,asio::buffer(h));auto len=pl(h);std::string msg(len,'\0');asio::read(*s,asio::buffer(msg.data(),msg.size()));return msg;}
NetworkTransferData sndrcv(asio::ip::tcp::socket*s,const NetworkTransferData&r){sr(s,r.toJson());return NetworkTransferData::fromJson(rr(s));}
void cnct(asio::ip::tcp::socket*s,unsigned short p){asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"),p);for(int i=0;i<CR;++i){std::error_code ec;s->connect(ep,ec);if(!ec)return;std::this_thread::sleep_for(RI);}throw std::runtime_error("connect failed");}
std::string ns(){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};localtime_s(&tm,&t);std::ostringstream os;os<<std::put_time(&tm,"%Y-%m-%d %H:%M:%S");return os.str();}
void wrl(const std::string&suite,const std::vector<TestStepResult>&s){std::filesystem::create_directories("test");std::ofstream o("test/report.log",std::ios::app);if(!o.good())return;o<<"====================\n"<<suite<<"\n"<<ns()<<"\n"<<gPassed<<"/"<<gTotal<<"\n";for(auto&x:s)o<<"["<<(x.passed?"YES":"NO")<<"]"<<x.name<<"\n";}
}

int main(){
    const std::string DB="err_test_db",TBL="err_tbl",UID="errTest";
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Executor Error Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);
        auto ex=[&](const std::string&sql,const std::string&db="",std::uint64_t v=0){
            NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,UID);r.setSql(sql);
            if(!db.empty()){
                r.setDbName(db);
                std::map<std::string,std::uint64_t> vm;
                vm[db]=v;
                r.setDbVersionMap(vm);
            }return sndrcv(&sock,r);};
        auto exv=[&](const NetworkTransferData&r,const std::string&db)->std::uint64_t{
            const auto&m=r.getDbVersionMap();auto it=m.find(db);return it!=m.end()?it->second:0;};

        ex("DROP DATABASE "+DB+";");
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,1,"Setup CREATE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-1\n";}
        {auto r=ex("USE DATABASE "+DB+";");as(steps,2,"Setup USE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-2\n";}
        uint64_t v=0;
        {auto r=ex("CREATE TABLE "+TBL+" (id INT PRIMARY KEY, name VARCHAR(50) NOT NULL, val INT, email VARCHAR(100) UNIQUE);",DB,v);v=exv(r,DB);as(steps,3,"Setup CREATE TABLE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-3\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'Alice', 100, 'a@mail.com');",DB,v);v=exv(r,DB);as(steps,4,"Setup INSERT row 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-4\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (2, 'Bob', 200, 'b@mail.com');",DB,v);v=exv(r,DB);as(steps,5,"Setup INSERT row 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-5\n";}

        int seq=6;

        // === Semantic errors (25) ===
        {auto r=ex("SELECT * FROM nonexistent_table;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject SELECT nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO nonexistent VALUES (1);",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE nonexistent SET x=1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM nonexistent;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject DELETE nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE nonexistent;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject DROP nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE nonexistent_db;");as(steps,seq++,"Reject USE nonexistent database",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE nonexistent_db;");as(steps,seq++,"Reject DROP DATABASE nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'a');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT wrong column count few",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'a', 100, 'b', 'c');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT wrong column count many",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" (id, unknown_col) VALUES (1, 1);",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT unknown_col FROM "+TBL+";",DB,v);v=exv(r,DB);as(steps,seq++,"Reject SELECT unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE unknown_col = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject WHERE unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" ORDER BY unknown_col;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject ORDER BY unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" GROUP BY unknown_col;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject GROUP BY unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET unknown_col = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM "+TBL+" WHERE unknown_col = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject DELETE WHERE unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+TBL+" (x INT);",DB,v);v=exv(r,DB);as(steps,seq++,"Reject CREATE TABLE duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,seq++,"Reject CREATE DATABASE duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id = 'string_in_int';",DB,v);v=exv(r,DB);bool p=r.getSuccess();as(steps,seq++,"SELECT type mismatch in WHERE treated as string compare",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES ('abc', 'Eve', 500, 'e@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT string into PK INT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" HAVING unknown_col = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject HAVING unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET name = 'x' WHERE unknown_col = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE WHERE unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" (name) VALUES ('only_name');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT missing PK column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW TABLE nonexistent;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject SHOW TABLE nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id = "+TBL+".nonexistent_col;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject qualified unknown column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (3, 'Charlie', 300, 'c@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Setup row 3 for semantic tests",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Constraint violations (25) ===
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'Dup', 999, 'dup@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject PK duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (4, 'Charlie', 300, 'a@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UNIQUE duplicate email",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (5, 'Dave', 400, 'b@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UNIQUE duplicate email b",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" (id, val) VALUES (10, 1000);",DB,v);v=exv(r,DB);as(steps,seq++,"Reject NOT NULL violation missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (10, 'User10', 1000, 'u10@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Setup valid row 10",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET id = 2 WHERE id = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE PK to existing value",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET email = 'b@mail.com' WHERE id = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE UNIQUE to existing value",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET name = '' WHERE id = 1;",DB,v);v=exv(r,DB);as(steps,seq++,"UPDATE name to empty string allowed",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (11, 'User11', 1100, 'u11@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Setup valid row 11",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'Multi', 1, 'm1@mail.com'), (1, 'Multi2', 2, 'm2@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject multi-row INSERT PK dup",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_varchar5 (name VARCHAR(5));",DB,v);v=exv(r,DB);as(steps,seq++,"Setup VARCHAR(5) table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_varchar5 VALUES ('hello');",DB,v);v=exv(r,DB);as(steps,seq++,"VARCHAR(5) insert exactly 5 chars",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_varchar5 VALUES ('toolong');",DB,v);v=exv(r,DB);as(steps,seq++,"VARCHAR(5) insert longer string allowed as TEXT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_char3 (code CHAR(3));",DB,v);v=exv(r,DB);as(steps,seq++,"Setup CHAR(3) table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_char3 VALUES ('abc');",DB,v);v=exv(r,DB);as(steps,seq++,"CHAR(3) insert exactly 3 chars",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_char3 VALUES ('abcd');",DB,v);v=exv(r,DB);as(steps,seq++,"CHAR(3) insert longer string allowed as TEXT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_date_err (d DATE);",DB,v);v=exv(r,DB);as(steps,seq++,"Setup DATE table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_date_err VALUES ('not-a-date');",DB,v);v=exv(r,DB);as(steps,seq++,"DATE invalid format accepted as TEXT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_date_err VALUES ('2023-12-31');",DB,v);v=exv(r,DB);as(steps,seq++,"DATE valid format insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (12, 'User12', 'abc', 'u12@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT string into INT column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" (val) VALUES ('abc');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INSERT string into INT via partial",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (13, 'User13', 1300, 'u13@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Setup valid row 13",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET val = 'abc' WHERE id = 13;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE string into INT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (14, 'User14', 2147483648, 'u14@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject INT overflow",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'DupAll', 1, 'dupall@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Reject multiple constraint violation PK+UNIQUE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (15, 'User15', 1500, 'u15@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Setup valid row 15",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET id = 15 WHERE id = 13;",DB,v);v=exv(r,DB);as(steps,seq++,"Reject UPDATE PK to another existing",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Boundary/edge cases (15) ===
        {auto r=ex("INSERT INTO "+TBL+" VALUES (30, 'MaxInt', 2147483647, 'max@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary max positive INT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (31, 'MinInt', -2147483648, 'min@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary min negative INT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (32, 'Zero', 0, 'zero@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary zero value",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_varchar5 VALUES ('');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary empty string",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {std::string longStr(255,'x');auto r=ex("INSERT INTO t_varchar5 VALUES ('"+longStr+"');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary 255 chars into VARCHAR(5) allowed",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_varchar5 VALUES ('!@#$%^&*()');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary special characters in string",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_varchar5 VALUES (' ');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary single space string",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (33, 'Float', 3.141592653589793, 'float@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary float with many decimals",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (34, 'NegFloat', -3.14, 'neg@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary negative float",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_date_err VALUES ('9999-12-31');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary far future date",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM nonexistent;",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary SELECT from nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id = 99999;",DB,v);v=exv(r,DB);bool p=r.getSuccess()&&r.getRows().empty();as(steps,seq++,"Boundary SELECT non-existent row empty result",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (35, 'User35', 35, 'u35@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary small positive value",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (36, 'User36', -1, 'u36@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary negative one",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (37, 'User37', 37, 'u37@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Boundary another small value",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Resource errors (8) ===
        {std::string longSql(2500,'S');auto r=ex(longSql,DB,v);v=exv(r,DB);as(steps,seq++,"Reject very long SQL",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {std::string manyCols="CREATE TABLE t_many_cols (";for(int i=0;i<50;++i){if(i>0)manyCols+=",";manyCols+="c"+std::to_string(i)+" INT";}manyCols+=");";
         auto r=ex(manyCols,DB,v);v=exv(r,DB);as(steps,seq++,"CREATE TABLE with many columns",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_long_tbl_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz (id INT);",DB,v);v=exv(r,DB);as(steps,seq++,"CREATE TABLE very long name accepted",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_long_col (abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz INT);",DB,v);v=exv(r,DB);as(steps,seq++,"CREATE TABLE very long column name accepted",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {std::string longStr(260,'x');auto r=ex("INSERT INTO t_varchar5 VALUES ('"+longStr+"');",DB,v);v=exv(r,DB);as(steps,seq++,"Very long string value allowed",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {std::string bigValues="INSERT INTO "+TBL+" VALUES ";for(int i=0;i<20;++i){if(i>0)bigValues+=",";bigValues+="("+std::to_string(100+i)+", 'Name"+std::to_string(i)+"', "+std::to_string(i)+", 'e"+std::to_string(i)+"@m.com')";}bigValues+=";";
         auto r=ex(bigValues,DB,v);v=exv(r,DB);as(steps,seq++,"INSERT massive value list",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE ( ( ( ( id = 1 ) ) ) );",DB,v);v=exv(r,DB);bool p=r.getSuccess();as(steps,seq++,"Deeply nested WHERE parentheses",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_many_varchar (a VARCHAR(100), b VARCHAR(100), c VARCHAR(100), d VARCHAR(100), e VARCHAR(100), f VARCHAR(100), g VARCHAR(100), h VARCHAR(100), i VARCHAR(100), j VARCHAR(100));",DB,v);v=exv(r,DB);as(steps,seq++,"CREATE TABLE many VARCHAR columns",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Transaction/version errors (12) ===
        {auto r=ex("INSERT INTO "+TBL+" VALUES (20, 'User20', 2000, 'u20@mail.com');",DB,99999);as(steps,seq++,"Reject INSERT version mismatch",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET val = 999 WHERE id = 1;",DB,0);as(steps,seq++,"Reject UPDATE stale version 0",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM "+TBL+" WHERE id = 20;",DB,1);as(steps,seq++,"Reject DELETE version mismatch",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_version_err (id INT);",DB,12345);as(steps,seq++,"Reject CREATE TABLE version mismatch",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_varchar5;",DB,0);as(steps,seq++,"Reject DROP TABLE stale version",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+";",DB,99999);as(steps,seq++,"Reject SELECT version mismatch",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW TABLES;",DB,0);as(steps,seq++,"Reject SHOW TABLES version mismatch",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (21, 'User21', 2100, 'u21@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Version increment after INSERT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (22, 'User22', 2200, 'u22@mail.com');",DB,v);v=exv(r,DB);as(steps,seq++,"Version increment after second INSERT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+";",DB,v);v=exv(r,DB);bool p=r.getSuccess();as(steps,seq++,"SELECT with correct version after inserts",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE "+TBL+" SET val = 111 WHERE id = 21;",DB,v);v=exv(r,DB);as(steps,seq++,"Version increment after UPDATE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM "+TBL+" WHERE id = 22;",DB,v);v=exv(r,DB);as(steps,seq++,"Version increment after DELETE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Auth/permission errors (10) ===
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"root");req.setPassword("wrong");
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject VERIFY wrong password",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"");
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject VERIFY empty user",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("bad");
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject LOGIN wrong password",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"noSuchUser");req.setPassword("123456");
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject LOGIN non-existent user",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST,"");req.setSql("SHOW DATABASES;");
         auto r=sndrcv(&sock,req);as(steps,seq++,"SQL_EXEC with empty user ID",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST,"user_with_special_chars_123");req.setSql("SHOW DATABASES;");
         auto r=sndrcv(&sock,req);as(steps,seq++,"SQL_EXEC with special chars user ID",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST,"");
         auto r=sndrcv(&sock,req);as(steps,seq++,"DB_VERSION_REQUEST without auth",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST,"");
         auto r=sndrcv(&sock,req);as(steps,seq++,"DIRECTORY_REQUEST without auth",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req("UNKNOWN_TYPE",UID);
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject unknown request type",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("");
         auto r=sndrcv(&sock,req);as(steps,seq++,"Reject LOGIN empty password",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Syntax errors (22) - no dbName to avoid version desync ===
        {auto r=ex("");as(steps,seq++,"Reject empty SQL",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("   ");as(steps,seq++,"Reject whitespace only SQL",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT");as(steps,seq++,"Reject incomplete SELECT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE");as(steps,seq++,"Reject incomplete CREATE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO");as(steps,seq++,"Reject INSERT missing table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE");as(steps,seq++,"Reject DROP TABLE missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE (id = 1");as(steps,seq++,"Reject unmatched parenthesis",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id = ");as(steps,seq++,"Reject missing value after operator",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_syntax (id INT");as(steps,seq++,"Reject missing close paren CREATE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1, 'a'");as(steps,seq++,"Reject missing close paren INSERT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id !! 1");as(steps,seq++,"Reject invalid operator",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES ('unclosed)");as(steps,seq++,"Reject malformed string literal",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1.2.3)");as(steps,seq++,"Reject invalid number format",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("TABLE CREATE t_syntax (id INT);");as(steps,seq++,"Reject wrong keyword order",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM WHERE id = 1;");as(steps,seq++,"Reject DELETE missing table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("UPDATE SET name = 'x';");as(steps,seq++,"Reject UPDATE missing table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT FROM "+TBL+";");as(steps,seq++,"Reject SELECT missing columns",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+";;");as(steps,seq++,"Reject double semicolon",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" (1, 2);");as(steps,seq++,"Reject INSERT missing VALUES",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE (id INT);");as(steps,seq++,"Reject CREATE TABLE missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT err_tbl VALUES (1);");as(steps,seq++,"Reject INSERT missing INTO",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE id @ 1");as(steps,seq++,"Reject invalid token in WHERE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // === Semantic parser errors (10) - no dbName ===
        {auto r=ex("CREATE TABLE t_dup_col_err (a INT, a VARCHAR(10));");as(steps,seq++,"Reject CREATE TABLE duplicate column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_empty ();");as(steps,seq++,"Reject CREATE TABLE no columns",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_empty_col_err (INT);");as(steps,seq++,"Reject CREATE TABLE empty column name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_varchar_neg (name VARCHAR(-1));");as(steps,seq++,"Reject VARCHAR negative length",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" ORDER BY id ORDER BY name;");as(steps,seq++,"Reject double ORDER BY",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check_err (age INT CHECK (age > 0));");as(steps,seq++,"Reject CHECK constraint parse",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (NULL, 'Name', 100, 'n@mail.com');");as(steps,seq++,"Reject NULL literal in value list",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_boolean_err (flag BOOLEAN);");as(steps,seq++,"Reject BOOLEAN type",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_text_err (content TEXT);");as(steps,seq++,"Reject TEXT type",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM "+TBL+" JOIN "+TBL+" t2 ON t1.id = t2.id JOIN "+TBL+" t3 ON t2.id = t3.id JOIN "+TBL+" t4 ON t3.id = t4.id;");as(steps,seq++,"Multiple JOINs parse",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-"<<(seq-1)<<"\n";}

        // Cleanup
        ex("DROP TABLE "+TBL+";",DB,v);
        ex("DROP TABLE t_varchar5;",DB,v);
        ex("DROP TABLE t_char3;",DB,v);
        ex("DROP TABLE t_date_err;",DB,v);
        ex("DROP TABLE t_many_varchar;",DB,v);
        ex("DROP TABLE t_many_cols;",DB,v);
        ex("DROP DATABASE "+DB+";");

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorErrorTest",steps);return ok?0:1;
}
