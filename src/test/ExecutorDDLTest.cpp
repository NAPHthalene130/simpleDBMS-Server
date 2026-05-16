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
constexpr unsigned short TEST_PORT=19093;
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
    const std::string DB="ddl_test_db",UID="ddlTest";
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Executor DDL Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);
        auto ex=[&](const std::string&sql,const std::string&db="",uint64_t v=0){
            NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,UID);r.setSql(sql);
            if(!db.empty()){r.setDbName(db);r.setDbVersion(v);}return sndrcv(&sock,r);};

        ex("DROP DATABASE "+DB+";");
        int seq=1;

        // === CREATE DATABASE (12) ===
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,seq++,"CREATE DATABASE normal",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE db123;");as(steps,seq++,"CREATE DATABASE with numbers",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE test_db;");as(steps,seq++,"CREATE DATABASE with underscore",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE myDB;");as(steps,seq++,"CREATE DATABASE mixed case",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE db_1_2_3;");as(steps,seq++,"CREATE DATABASE multiple underscores and numbers",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE a;");as(steps,seq++,"CREATE DATABASE single char",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE ABC;");as(steps,seq++,"CREATE DATABASE uppercase",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE db_with_underscores_and_123;");as(steps,seq++,"CREATE DATABASE long mixed name",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,seq++,"Reject CREATE DATABASE duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess()&&r.getRows().size()>=1;as(steps,seq++,"SHOW DATABASES after creates",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE another_db;");as(steps,seq++,"CREATE DATABASE another",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess()&&r.getRows().size()>=2;as(steps,seq++,"SHOW DATABASES has multiple",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === DROP DATABASE (10) ===
        {auto r=ex("DROP DATABASE db123;");as(steps,seq++,"DROP DATABASE with numbers",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE test_db;");as(steps,seq++,"DROP DATABASE with underscore",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE nonexistent_db_xyz;");as(steps,seq++,"Reject DROP DATABASE nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE temp_drop_db;");as(steps,seq++,"Setup CREATE for drop test",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE temp_drop_db;");as(steps,seq++,"Setup USE for drop test",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_drop (id INT);");as(steps,seq++,"Setup CREATE TABLE in db to drop",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_drop VALUES (1);");as(steps,seq++,"Setup INSERT in db to drop",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE temp_drop_db;");as(steps,seq++,"DROP DATABASE with tables and data",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE temp_drop_db;");as(steps,seq++,"Reject double DROP DATABASE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE temp_drop_db;");as(steps,seq++,"Reject USE after DROP",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === USE DATABASE (5) ===
        {auto r=ex("USE DATABASE "+DB+";");as(steps,seq++,"USE DATABASE normal",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE nonexistent_xyz;");as(steps,seq++,"Reject USE nonexistent database",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE another_db;");as(steps,seq++,"USE DATABASE switch to another",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE "+DB+";");as(steps,seq++,"USE DATABASE switch back",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE;");as(steps,seq++,"Reject USE DATABASE missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === CREATE TABLE success (20) ===
        {auto r=ex("CREATE TABLE t_int (id INT);");as(steps,seq++,"CREATE TABLE INT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_varchar (name VARCHAR(100));");as(steps,seq++,"CREATE TABLE VARCHAR",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_float (val FLOAT);");as(steps,seq++,"CREATE TABLE FLOAT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_double (val DOUBLE);");as(steps,seq++,"CREATE TABLE DOUBLE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_char (code CHAR(10));");as(steps,seq++,"CREATE TABLE CHAR",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_date (d DATE);");as(steps,seq++,"CREATE TABLE DATE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_datetime (dt DATETIME);");as(steps,seq++,"CREATE TABLE DATETIME",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_pk (id INT PRIMARY KEY);");as(steps,seq++,"CREATE TABLE PRIMARY KEY",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_notnull (name VARCHAR(50) NOT NULL);");as(steps,seq++,"CREATE TABLE NOT NULL",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_unique (email VARCHAR(100) UNIQUE);");as(steps,seq++,"CREATE TABLE UNIQUE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default (status INT DEFAULT 0);");as(steps,seq++,"CREATE TABLE DEFAULT int",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_str (name VARCHAR(50) DEFAULT 'unknown');");as(steps,seq++,"CREATE TABLE DEFAULT string",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_autoinc (id INT AUTO_INCREMENT PRIMARY KEY);");as(steps,seq++,"CREATE TABLE AUTO_INCREMENT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_fk (id INT, ref_id INT, FOREIGN KEY (ref_id) REFERENCES t_pk(id));");as(steps,seq++,"CREATE TABLE FOREIGN KEY",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_multi_col (a INT, b VARCHAR(10), c FLOAT, d DOUBLE, e DATE);");as(steps,seq++,"CREATE TABLE multiple columns",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_single (x INT);");as(steps,seq++,"CREATE TABLE single column",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_noconstraints (a INT, b VARCHAR(20));");as(steps,seq++,"CREATE TABLE no constraints",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_pk_notnull (id INT PRIMARY KEY NOT NULL, name VARCHAR(50) NOT NULL);");as(steps,seq++,"CREATE TABLE PK+NOT NULL",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_all_constraints (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE NOT NULL, status INT DEFAULT 1);");as(steps,seq++,"CREATE TABLE multiple constraints",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_float (val FLOAT DEFAULT 0.5);");as(steps,seq++,"CREATE TABLE DEFAULT float",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_double (val DOUBLE DEFAULT 1.5);");as(steps,seq++,"CREATE TABLE DEFAULT double",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === DROP TABLE (10) ===
        {auto r=ex("DROP TABLE t_varchar;");as(steps,seq++,"DROP TABLE normal",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE nonexistent_table;");as(steps,seq++,"Reject DROP TABLE nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_pk VALUES (1);");as(steps,seq++,"Setup INSERT before drop",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_pk;");as(steps,seq++,"DROP TABLE after insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_drop1 (id INT);");as(steps,seq++,"Setup DROP TABLE multiple 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_drop2 (id INT);");as(steps,seq++,"Setup DROP TABLE multiple 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_drop1;");as(steps,seq++,"DROP TABLE first of multiple",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_drop2;");as(steps,seq++,"DROP TABLE second of multiple",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_recreate (id INT);");as(steps,seq++,"Setup DROP then recreate",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_recreate;");as(steps,seq++,"DROP TABLE for recreate",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_recreate (id INT);");as(steps,seq++,"CREATE TABLE after drop recreate",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_recreate;");as(steps,seq++,"DROP TABLE recreated table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === SHOW TABLES (8) ===
        {auto r=ex("SHOW TABLES;");bool p=r.getSuccess();as(steps,seq++,"SHOW TABLES baseline",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_show1 (id INT);");as(steps,seq++,"Setup SHOW TABLES 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_show2 (id INT);");as(steps,seq++,"Setup SHOW TABLES 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_show3 (id INT);");as(steps,seq++,"Setup SHOW TABLES 3",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW TABLES;");bool p=r.getSuccess()&&r.getRows().size()>=3;as(steps,seq++,"SHOW TABLES has 3 new tables",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP TABLE t_show1;");as(steps,seq++,"Setup DROP for SHOW TABLES",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW TABLES;");bool p=r.getSuccess();as(steps,seq++,"SHOW TABLES after drop",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW TABLES;");as(steps,seq++,"SHOW TABLES uses session db",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === SHOW DATABASES (6) ===
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess()&&r.getRows().size()>=1;as(steps,seq++,"SHOW DATABASES baseline",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE show_db1;");as(steps,seq++,"Setup SHOW DATABASES 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE DATABASE show_db2;");as(steps,seq++,"Setup SHOW DATABASES 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess()&&r.getRows().size()>=2;as(steps,seq++,"SHOW DATABASES after creates",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE show_db1;");as(steps,seq++,"Setup DROP for SHOW DATABASES",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DROP DATABASE show_db2;");as(steps,seq++,"Setup DROP 2 for SHOW DATABASES",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess();as(steps,seq++,"SHOW DATABASES after drops",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === AUTO_INCREMENT (16) ===
        {auto r=ex("CREATE TABLE t_ai (id INT AUTO_INCREMENT PRIMARY KEY, val INT);");as(steps,seq++,"Setup AUTO_INCREMENT table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (1, 100);");as(steps,seq++,"AUTO_INCREMENT explicit value 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (2, 200);");as(steps,seq++,"AUTO_INCREMENT explicit value 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai WHERE id = 1;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT verify explicit value 1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai WHERE id = 2;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT verify explicit value 2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (5, 500);");as(steps,seq++,"AUTO_INCREMENT explicit value 5",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (6, 600);");as(steps,seq++,"AUTO_INCREMENT sequential after 5",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai WHERE id = 6;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT verify value 6",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("DELETE FROM t_ai WHERE id = 2;");as(steps,seq++,"AUTO_INCREMENT delete middle row",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (7, 700);");as(steps,seq++,"AUTO_INCREMENT gap handling after delete",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai WHERE id = 7;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT gap fill value 7",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (100, 10000);");as(steps,seq++,"AUTO_INCREMENT explicit large value",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai VALUES (101, 800);");as(steps,seq++,"AUTO_INCREMENT after large explicit",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai WHERE id = 101;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT next after 100 is 101",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_ai2 (id INT AUTO_INCREMENT PRIMARY KEY);");as(steps,seq++,"AUTO_INCREMENT second table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_ai2 VALUES (1);");as(steps,seq++,"AUTO_INCREMENT second table insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT * FROM t_ai2 WHERE id = 1;");bool p=r.getSuccess()&&r.getRows().size()==1;as(steps,seq++,"AUTO_INCREMENT second table value 1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === UNIQUE constraint (12) ===
        {auto r=ex("CREATE TABLE t_unique_test (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE);");as(steps,seq++,"Setup UNIQUE table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_test VALUES (1, 'a@a.com');");as(steps,seq++,"UNIQUE valid insert 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_test VALUES (2, 'b@b.com');");as(steps,seq++,"UNIQUE valid insert 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_test VALUES (3, 'a@a.com');");as(steps,seq++,"Reject UNIQUE duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_test VALUES (4, '');");as(steps,seq++,"UNIQUE insert empty string",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_test VALUES (5, '');");as(steps,seq++,"Reject UNIQUE duplicate empty string",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_unique_multi (id INT PRIMARY KEY, code VARCHAR(10) UNIQUE, name VARCHAR(50) UNIQUE);");as(steps,seq++,"Setup multiple UNIQUE columns",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_multi VALUES (1, 'A', 'Alice');");as(steps,seq++,"UNIQUE multi valid insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_multi VALUES (2, 'A', 'Bob');");as(steps,seq++,"Reject UNIQUE multi first col dup",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_multi VALUES (3, 'B', 'Alice');");as(steps,seq++,"Reject UNIQUE multi second col dup",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_unique_nn (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE NOT NULL);");as(steps,seq++,"Setup UNIQUE NOT NULL",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_nn VALUES (1, 'x@x.com');");as(steps,seq++,"UNIQUE NOT NULL valid insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_unique_nn VALUES (2, 'x@x.com');");as(steps,seq++,"Reject UNIQUE NOT NULL duplicate",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === DEFAULT values (12) ===
        {auto r=ex("CREATE TABLE t_default_test (id INT PRIMARY KEY, status INT DEFAULT 0, name VARCHAR(50) DEFAULT 'unknown');");as(steps,seq++,"Setup DEFAULT table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_test (id) VALUES (1);");as(steps,seq++,"DEFAULT missing columns get defaults",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT status, name FROM t_default_test WHERE id = 1;");bool p=r.getSuccess()&&r.getRows().size()==1&&r.getRows()[0].size()>=2&&r.getRows()[0][0]=="0"&&r.getRows()[0][1]=="unknown";as(steps,seq++,"DEFAULT verify INT and VARCHAR defaults",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_test VALUES (2, 5, 'custom');");as(steps,seq++,"DEFAULT explicit values override",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT status, name FROM t_default_test WHERE id = 2;");bool p=r.getSuccess()&&r.getRows().size()==1&&r.getRows()[0].size()>=2&&r.getRows()[0][0]=="5"&&r.getRows()[0][1]=="custom";as(steps,seq++,"DEFAULT verify explicit override",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_pk (id INT PRIMARY KEY DEFAULT 100, name VARCHAR(50));");as(steps,seq++,"Setup DEFAULT with PK",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_pk (name) VALUES ('test');");as(steps,seq++,"DEFAULT with PK missing",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT id FROM t_default_pk WHERE name = 'test';");bool p=r.getSuccess()&&r.getRows().size()==1&&r.getRows()[0].size()>=1&&r.getRows()[0][0]=="100";as(steps,seq++,"DEFAULT verify PK default 100",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_float2 (val FLOAT DEFAULT 3.14);");as(steps,seq++,"Setup DEFAULT FLOAT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_float2 VALUES (2.71);");as(steps,seq++,"DEFAULT FLOAT explicit insert",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_default_nn (id INT PRIMARY KEY, name VARCHAR(50) NOT NULL DEFAULT 'noname');");as(steps,seq++,"Setup DEFAULT NOT NULL",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_nn (id) VALUES (1);");as(steps,seq++,"DEFAULT NOT NULL missing gets default",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO t_default_nn (id, name) VALUES (2, 'explicit');");as(steps,seq++,"DEFAULT NOT NULL explicit overrides",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // === Parser error tests (no dbName to avoid version desync) ===
        // ALTER TABLE (15)
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x INT;");as(steps,seq++,"Reject ALTER TABLE ADD COLUMN",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int DROP COLUMN id;");as(steps,seq++,"Reject ALTER TABLE DROP COLUMN",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int MODIFY COLUMN id VARCHAR(50);");as(steps,seq++,"Reject ALTER TABLE MODIFY COLUMN",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE nonexistent ADD COLUMN x INT;");as(steps,seq++,"Reject ALTER TABLE nonexistent table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int RENAME TO t_int2;");as(steps,seq++,"Reject ALTER TABLE RENAME",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x INT DEFAULT 0;");as(steps,seq++,"Reject ALTER TABLE ADD with DEFAULT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x FLOAT;");as(steps,seq++,"Reject ALTER TABLE ADD FLOAT",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x DOUBLE;");as(steps,seq++,"Reject ALTER TABLE ADD DOUBLE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x DATE;");as(steps,seq++,"Reject ALTER TABLE ADD DATE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x CHAR(10);");as(steps,seq++,"Reject ALTER TABLE ADD CHAR",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x VARCHAR(50);");as(steps,seq++,"Reject ALTER TABLE ADD VARCHAR",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x INT NOT NULL;");as(steps,seq++,"Reject ALTER TABLE ADD NOT NULL",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x INT PRIMARY KEY;");as(steps,seq++,"Reject ALTER TABLE ADD PK",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int ADD COLUMN x INT UNIQUE;");as(steps,seq++,"Reject ALTER TABLE ADD UNIQUE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("ALTER TABLE t_int DROP COLUMN x;");as(steps,seq++,"Reject ALTER TABLE DROP nonexistent column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // TRUNCATE TABLE (10)
        {auto r=ex("TRUNCATE TABLE t_int;");as(steps,seq++,"Reject TRUNCATE TABLE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE nonexistent;");as(steps,seq++,"Reject TRUNCATE TABLE nonexistent",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE t_float;");as(steps,seq++,"Reject TRUNCATE TABLE with data",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE t_char;");as(steps,seq++,"Reject TRUNCATE TABLE empty table",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE t_int;");as(steps,seq++,"Reject TRUNCATE without TABLE keyword",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE;");as(steps,seq++,"Reject TRUNCATE TABLE missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE t_int, t_float;");as(steps,seq++,"Reject TRUNCATE multiple tables",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE t_int CASCADE;");as(steps,seq++,"Reject TRUNCATE TABLE CASCADE",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE db1.t_int;");as(steps,seq++,"Reject TRUNCATE TABLE qualified name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("TRUNCATE TABLE 123;");as(steps,seq++,"Reject TRUNCATE TABLE number name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // CREATE TABLE parser errors (10)
        {auto r=ex("CREATE TABLE t_boolean (flag BOOLEAN);");as(steps,seq++,"Reject CREATE TABLE BOOLEAN unsupported",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_text (content TEXT);");as(steps,seq++,"Reject CREATE TABLE TEXT unsupported",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check (age INT CHECK (age > 0));");as(steps,seq++,"Reject CREATE TABLE CHECK unsupported",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE (id INT);");as(steps,seq++,"Reject CREATE TABLE missing name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_empty ();");as(steps,seq++,"Reject CREATE TABLE no columns",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_empty_col (INT);");as(steps,seq++,"Reject CREATE TABLE empty column name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_dup_col (a INT, a VARCHAR(10));");as(steps,seq++,"Reject CREATE TABLE duplicate column",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_varchar0 (name VARCHAR(0));");as(steps,seq++,"Reject CREATE TABLE VARCHAR(0)",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_int (id INT);");as(steps,seq++,"Reject CREATE TABLE duplicate name",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_longname_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz (id INT);");as(steps,seq++,"Reject CREATE TABLE name too long",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // CHECK constraints (5)
        {auto r=ex("CREATE TABLE t_check1 (age INT CHECK (age > 0));");as(steps,seq++,"Reject CHECK single condition",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check2 (score INT CHECK (score >= 0 AND score <= 100));");as(steps,seq++,"Reject CHECK compound condition",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check3 (name VARCHAR(50) CHECK (name <> ''));");as(steps,seq++,"Reject CHECK string condition",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check4 (id INT PRIMARY KEY, age INT CHECK (age > 0), status INT CHECK (status >= 0));");as(steps,seq++,"Reject CHECK multiple columns",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE t_check5 (age INT, CHECK (age > 0));");as(steps,seq++,"Reject CHECK table-level constraint",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-"<<(seq-1)<<"\n";}

        // Cleanup
        ex("DROP DATABASE "+DB+";");
        ex("DROP DATABASE another_db;");
        ex("DROP DATABASE myDB;");
        ex("DROP DATABASE a;");
        ex("DROP DATABASE ABC;");
        ex("DROP DATABASE db_with_underscores_and_123;");

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorDDLTest",steps);return ok?0:1;
}
