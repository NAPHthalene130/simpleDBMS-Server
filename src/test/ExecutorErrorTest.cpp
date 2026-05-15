/**
 * @file ExecutorErrorTest.cpp
 * @brief SQL错误处理与边界测试
 * @details 测试各种错误SQL的拒绝：语法错误、语义错误、约束违反、不存在的对象等。
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
    const std::string DB="err_test",TBL="t",UID="errTest";
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Executor Error Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);
        auto ex=[&](const std::string&sql,const std::string&db="",uint64_t v=0){
            NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,UID);r.setSql(sql);
            if(!db.empty()){r.setDbName(db);r.setDbVersion(v);}return sndrcv(&sock,r);};

        ex("DROP DATABASE "+DB+";");
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,1,"Setup CREATE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-1\n";}
        {auto r=ex("USE DATABASE "+DB+";");as(steps,2,"Setup USE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-2\n";}
        {auto r=ex("CREATE TABLE "+TBL+" (id INT PRIMARY KEY, val INT);",DB,0);uint64_t v=0;if(r.getSuccess())v=r.getDbVersion();as(steps,3,"Setup CREATE TABLE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-3\n";}
        uint64_t v=1;

        // Error cases - should all fail
        {auto r=ex("CREATE DATABASE "+DB+";");as(steps,4,"Reject CREATE DATABASE already exists",!r.getSuccess(),r.getMessage());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-4\n";}
        {auto r=ex("CREATE TABLE "+TBL+" (x INT);",DB,v);as(steps,5,"Reject CREATE TABLE already exists",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-5\n";}
        {auto r=ex("SELECT * FROM nonexistent_table;",DB,v);as(steps,6,"Reject SELECT from nonexistent table",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-6\n";}
        {auto r=ex("INSERT INTO nonexistent VALUES (1);",DB,v);as(steps,7,"Reject INSERT into nonexistent table",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-7\n";}
        {auto r=ex("DROP TABLE nonexistent;",DB,v);as(steps,8,"Reject DROP nonexistent table",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-8\n";}
        {auto r=ex("GARBAGE SQL SYNTAX ERROR");as(steps,9,"Reject garbage SQL",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-9\n";}
        {auto r=ex("");as(steps,10,"Reject empty SQL",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-10\n";}
        {auto r=ex("INSERT INTO "+TBL+" VALUES (1);",DB,v);as(steps,11,"Reject INSERT with wrong column count",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-11\n";}
        {auto r=ex("UPDATE nonexistent SET x=1;",DB,v);as(steps,12,"Reject UPDATE nonexistent table",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-12\n";}
        {auto r=ex("DELETE FROM nonexistent;",DB,v);as(steps,13,"Reject DELETE from nonexistent table",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-13\n";}
        {auto r=ex("USE DATABASE nonexistent_db_xyz;");as(steps,14,"Reject USE nonexistent database",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-14\n";}
        {auto r=ex("SELECT * FROM "+TBL+" WHERE unknown_col = 1;",DB,v);as(steps,15,"Reject SELECT with unknown column",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" ER-15\n";}

        // Cleanup
        ex("DROP TABLE "+TBL+";",DB,v);
        ex("DROP DATABASE "+DB+";");
        as(steps,16,"Cleanup",true);std::cout<<"  [PASS] ER-16\n";

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorErrorTest",steps);return ok?0:1;
}
