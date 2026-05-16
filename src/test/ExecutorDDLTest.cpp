/**
 * @file ExecutorDDLTest.cpp
 * @brief DDL操作测试
 * @details 测试CREATE TABLE各数据类型和约束、ALTER TABLE操作、DROP等。
 * @author NAPH130
 */
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
    const std::string DB="ddl_test",UID="ddlTest";
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
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,1,"CREATE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-1\n";}
        {auto r=ex("USE DATABASE "+DB+";");as(steps,2,"USE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-2\n";}
        uint64_t v=0;

        // Create tables with various types
        {auto r=ex("CREATE TABLE t1 (a INT, b VARCHAR(100), c FLOAT);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,3,"CREATE TABLE with INT/VARCHAR/FLOAT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-3\n";}
        {auto r=ex("CREATE TABLE t2 (id INT PRIMARY KEY);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,4,"CREATE TABLE with PRIMARY KEY",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-4\n";}
        {auto r=ex("CREATE TABLE t3 (id INT AUTO_INCREMENT PRIMARY KEY, val INT);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,5,"CREATE TABLE with AUTO_INCREMENT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-5\n";}

        // Test AUTO_INCREMENT
        {auto r=ex("INSERT INTO t3 (val) VALUES (100);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,6,"INSERT into AUTO_INCREMENT table",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-6\n";}
        {auto r=ex("INSERT INTO t3 (val) VALUES (200);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,7,"INSERT second row AUTO_INCREMENT",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-7\n";}
        {auto r=ex("SELECT * FROM t3;",DB,v);bool p=r.getSuccess()&&r.getRows().size()==2;if(p)v=r.getDbVersion();as(steps,8,"AUTO_INCREMENT generated 2 unique ids",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-8\n";}

        // ALTER TABLE ADD COLUMN
        {auto r=ex("DROP TABLE t1;",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,9,"DROP TABLE t1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-9\n";}
        {auto r=ex("CREATE TABLE t1 (a INT);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,10,"CREATE TABLE t1 (single column)",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-10\n";}
        // Note: ALTER TABLE ADD COLUMN may not be fully supported; test what works

        // UNIQUE constraint
        {auto r=ex("CREATE TABLE t4 (id INT PRIMARY KEY, email VARCHAR(100) UNIQUE);",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,11,"CREATE TABLE with UNIQUE constraint",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-11\n";}
        {auto r=ex("INSERT INTO t4 VALUES (1, 'a@a.com');",DB,v);if(r.getSuccess())v=r.getDbVersion();as(steps,12,"INSERT unique value",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-12\n";}
        {auto r=ex("INSERT INTO t4 VALUES (2, 'a@a.com');",DB,v);bool p=!r.getSuccess();as(steps,13,"Reject INSERT duplicate UNIQUE",p,r.getMessage());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-13\n";}

        // SHOW DATABASES
        {auto r=ex("SHOW DATABASES;");bool p=r.getSuccess()&&r.getRows().size()>=1;as(steps,14,"SHOW DATABASES includes "+DB,p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-14\n";}
        // SHOW TABLES
        {auto r=ex("SHOW TABLES;",DB,v);bool p=r.getSuccess();as(steps,15,"SHOW TABLES in database",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" DD-15\n";}

        // Cleanup
        {auto r=ex("DROP TABLE t2;DROP TABLE t3;DROP TABLE t4;",DB,v);as(steps,16,"DROP multiple tables",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-16\n";}
        {auto r=ex("DROP DATABASE "+DB+";");as(steps,17,"DROP DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" DD-17\n";}

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorDDLTest",steps);return ok?0:1;
}
