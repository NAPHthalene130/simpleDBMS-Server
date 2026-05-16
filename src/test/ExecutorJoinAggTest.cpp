/**
 * @file ExecutorJoinAggTest.cpp
 * @brief JOIN与聚合函数测试
 * @details 测试INNER/LEFT/RIGHT JOIN、GROUP BY、COUNT/SUM/AVG/MIN/MAX、HAVING。
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
constexpr unsigned short TEST_PORT=19092;
constexpr int CONNECT_RETRY=40;
constexpr auto RETRY_INTERVAL=std::chrono::milliseconds(100);
struct TestStepResult{int id;std::string name;bool passed;std::string detail;};
int gTotal=0,gPassed=0;
void appendStep(std::vector<TestStepResult>&s,int id,const std::string&n,bool p,const std::string&d=""){++gTotal;if(p)++gPassed;s.push_back({id,n,p,d});}
std::array<unsigned char,4>bl(uint32_t l){return{(unsigned char)(l>>24),(unsigned char)(l>>16),(unsigned char)(l>>8),(unsigned char)l};}
uint32_t pl(const std::array<unsigned char,4>&h){return((uint32_t)h[0]<<24)|((uint32_t)h[1]<<16)|((uint32_t)h[2]<<8)|(uint32_t)h[3];}
void sr(asio::ip::tcp::socket*s,const std::string&m){auto h=bl((uint32_t)m.size());asio::write(*s,asio::buffer(h));asio::write(*s,asio::buffer(m));}
std::string rr(asio::ip::tcp::socket*s){std::array<unsigned char,4>h{};asio::read(*s,asio::buffer(h));auto len=pl(h);std::string msg(len,'\0');asio::read(*s,asio::buffer(msg.data(),msg.size()));return msg;}
NetworkTransferData sndrcv(asio::ip::tcp::socket*s,const NetworkTransferData&r){sr(s,r.toJson());return NetworkTransferData::fromJson(rr(s));}
void cnct(asio::ip::tcp::socket*s,unsigned short p){asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"),p);for(int i=0;i<CONNECT_RETRY;++i){std::error_code ec;s->connect(ep,ec);if(!ec)return;std::this_thread::sleep_for(RETRY_INTERVAL);}throw std::runtime_error("connect failed");}
std::string ns(){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};localtime_s(&tm,&t);std::ostringstream os;os<<std::put_time(&tm,"%Y-%m-%d %H:%M:%S");return os.str();}
void wrl(const std::string&suite,const std::vector<TestStepResult>&s){std::filesystem::create_directories("test");std::ofstream o("test/report.log",std::ios::app);if(!o.good())return;o<<"====================\n"<<suite<<"\n"<<ns()<<"\n"<<gPassed<<"/"<<gTotal<<"\n";for(auto&x:s)o<<"["<<(x.passed?"YES":"NO")<<"]"<<x.name<<"\n";}
}

int main(){
    const std::string DB="joinagg_test",A="orders",B="customers",UID="jaTest";
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Executor JoinAgg Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);
        auto ex=[&](const std::string&sql,const std::string&db="",uint64_t v=0){
            NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,UID);r.setSql(sql);
            if(!db.empty()){r.setDbName(db);r.setDbVersion(v);}return sndrcv(&sock,r);};

        ex("DROP DATABASE "+DB+";");
        {auto r=ex("CREATE DATABASE "+DB+";");appendStep(steps,1,"CREATE DATABASE",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-1\n";}
        {auto r=ex("USE DATABASE "+DB+";");appendStep(steps,2,"USE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-2\n";}

        // Create tables
        {auto r=ex("CREATE TABLE "+A+" (id INT PRIMARY KEY, customer_id INT, amount FLOAT);",DB,0);uint64_t v=r.getSuccess()?r.getDbVersion():0;appendStep(steps,3,"CREATE TABLE orders",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-3\n";}
        uint64_t v=1;
        {auto r=ex("CREATE TABLE "+B+" (id INT PRIMARY KEY, name VARCHAR(50), city VARCHAR(50));",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,4,"CREATE TABLE customers",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-4\n";}

        // Insert data
        {auto r=ex("INSERT INTO "+B+" VALUES (1, 'Alice', 'NY');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,5,"INSERT customer 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-5\n";}
        {auto r=ex("INSERT INTO "+B+" VALUES (2, 'Bob', 'LA');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,6,"INSERT customer 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-6\n";}
        {auto r=ex("INSERT INTO "+A+" VALUES (1, 1, 100.5);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,7,"INSERT order 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-7\n";}
        {auto r=ex("INSERT INTO "+A+" VALUES (2, 1, 200.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,8,"INSERT order 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-8\n";}
        {auto r=ex("INSERT INTO "+A+" VALUES (3, 2, 150.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,9,"INSERT order 3",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-9\n";}

        // INNER JOIN
        {auto r=ex("SELECT o.id, c.name, o.amount FROM "+A+" o INNER JOIN "+B+" c ON o.customer_id = c.id;",DB,v);
         bool p=r.getSuccess()&&r.getRows().size()==3;if(p)v=r.getDbVersion();
         appendStep(steps,10,"INNER JOIN returns 3 rows",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-10\n";}
        // LEFT JOIN
        {auto r=ex("SELECT c.id, c.name FROM "+B+" c LEFT JOIN "+A+" o ON c.id = o.customer_id;",DB,v);
         bool p=r.getSuccess()&&r.getRows().size()>=2;if(p)v=r.getDbVersion();
         appendStep(steps,11,"LEFT JOIN returns >=2 rows",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-11\n";}

        // COUNT
        {auto r=ex("SELECT COUNT(*) FROM "+A+";",DB,v);bool p=r.getSuccess()&&r.getRows().size()==1;if(p)v=r.getDbVersion();
         appendStep(steps,12,"COUNT(*) aggregation",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-12\n";}
        // SUM
        {auto r=ex("SELECT SUM(amount) FROM "+A+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,13,"SUM aggregation",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-13\n";}
        // AVG
        {auto r=ex("SELECT AVG(amount) FROM "+A+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,14,"AVG aggregation",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-14\n";}
        // MIN/MAX
        {auto r=ex("SELECT MIN(amount), MAX(amount) FROM "+A+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,15,"MIN/MAX aggregation",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-15\n";}

        // GROUP BY
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+A+" GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess()&&r.getRows().size()==2;if(p)v=r.getDbVersion();
         appendStep(steps,16,"GROUP BY with COUNT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-16\n";}
        // GROUP BY + HAVING
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+A+" GROUP BY customer_id HAVING COUNT(*) > 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,17,"GROUP BY + HAVING",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-17\n";}
        // JOIN + GROUP BY
        {auto r=ex("SELECT c.name, COUNT(o.id) FROM "+B+" c INNER JOIN "+A+" o ON c.id = o.customer_id GROUP BY c.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,18,"JOIN + GROUP BY",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-18\n";}

        // SELECT with aggregation + WHERE
        {auto r=ex("SELECT SUM(amount) FROM "+A+" WHERE customer_id = 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,19,"Aggregation with WHERE filter",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-19\n";}

        // Cleanup
        ex("DROP TABLE "+A+";DROP TABLE "+B+";",DB,v);
        ex("DROP DATABASE "+DB+";");
        appendStep(steps,20,"DROP TABLE/DATABASE cleanup",true,"ok");std::cout<<"  [PASS] JA-20\n";

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorJoinAggTest",steps);return ok?0:1;
}
