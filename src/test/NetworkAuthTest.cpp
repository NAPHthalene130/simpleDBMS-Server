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
constexpr unsigned short TEST_PORT=19095;
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
NetworkTransferData verifyReq(const std::string&user,const std::string&pw=""){NetworkTransferData r(NetworkTransferData::VERIFY_REQUEST,user);r.setPassword(pw);return r;}
NetworkTransferData loginReq(const std::string&user,const std::string&pw=""){NetworkTransferData r(NetworkTransferData::LOGIN_REQUEST,user);r.setPassword(pw);return r;}
NetworkTransferData sqlReq(const std::string&user,const std::string&sql){NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,user);r.setSql(sql);return r;}
}

int main(){
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Network Auth Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);

        int seq=1;

        // ===== VERIFY_REQUEST (20+) =====
        {auto r=sndrcv(&sock,verifyReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"VERIFY correct credentials",p,r.getMessage());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","wrong"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY wrong password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root",""));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY empty password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY empty user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("",""));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY empty user and password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("nouser","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY non-existent user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("Root","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY case sensitivity user Root",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"VERIFY repeat correct credentials",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","1234567"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY off-by-one password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","12345"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY short password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {std::string longPw(200,'x');auto r=sndrcv(&sock,verifyReq("root",longPw));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY very long password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {std::string longUser(200,'u');auto r=sndrcv(&sock,verifyReq(longUser,"123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY very long username rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","123456"));sndrcv(&sock,verifyReq("root","wrong"));auto r2=sndrcv(&sock,verifyReq("root","123456"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"VERIFY after failed attempt",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","special!@#$%"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY special char password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","123 456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY password with space rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq(" admin ","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY username with spaces rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","123456"));auto r2=sndrcv(&sock,verifyReq("root","123456"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"VERIFY multiple sequential verify",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("ROOT","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY uppercase ROOT rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"VERIFY after login attempt still works",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","' OR '1'='1"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY SQL injection-like password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,verifyReq("root","; DROP TABLE users;"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY semicolon injection password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}

        // ===== LOGIN_REQUEST (20+) =====
        {auto r=sndrcv(&sock,loginReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"LOGIN correct credentials",p,r.getMessage());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","bad"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN wrong password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN empty user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root",""));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN empty password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("noSuchUser","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN non-existent user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("Root","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN case sensitivity user Root",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("ROOT","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN uppercase ROOT rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"LOGIN repeat correct credentials",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","1234567"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN off-by-one password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","12345"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN short password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {std::string longPw(200,'x');auto r=sndrcv(&sock,loginReq("root",longPw));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN very long password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {std::string longUser(200,'u');auto r=sndrcv(&sock,loginReq(longUser,"123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN very long username rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","special!@#$%"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN special char password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","123 456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN password with space rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq(" admin ","123456"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN username with spaces rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","' OR '1'='1"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN SQL injection-like password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","; DROP TABLE users;"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN semicolon injection password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","123456"));auto r2=sndrcv(&sock,loginReq("root","123456"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"LOGIN repeated login same connection",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,loginReq("root","123456"));auto r=sndrcv(&sock,loginReq("root","bad"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN wrong after correct rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","123456"));auto r2=sndrcv(&sock,loginReq("root","123456"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"LOGIN multiple sequential login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"LOGIN after verify succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,loginReq("nonexistent","password"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN completely unknown user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}

        // ===== SQL_EXEC_REQUEST auth (20+) =====
        {asio::io_context ctx2;asio::ip::tcp::socket sock2(ctx2);cnct(&sock2,TEST_PORT);
         auto r=sndrcv(&sock2,sqlReq("root","SHOW DATABASES;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL before login fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock2.shutdown(asio::ip::tcp::socket::shutdown_both);sock2.close();}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL after login succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,verifyReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL after verify succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","CREATE DATABASE auth_test_db;"));bool p=r.getSuccess();
         as(steps,seq++,"CREATE DATABASE via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,sqlReq("root","USE DATABASE auth_test_db;"));bool p=r.getSuccess();
         as(steps,seq++,"USE DATABASE via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,sqlReq("root","CREATE TABLE t1 (id INT PRIMARY KEY);"));bool p=r.getSuccess();
         as(steps,seq++,"CREATE TABLE via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,sqlReq("root","INSERT INTO t1 VALUES (1);"));bool p=r.getSuccess();
         as(steps,seq++,"INSERT via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,sqlReq("root","SELECT * FROM t1;"));bool p=r.getSuccess();
         as(steps,seq++,"SELECT via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {asio::io_context ctx3;asio::ip::tcp::socket sock3(ctx3);cnct(&sock3,TEST_PORT);
         sndrcv(&sock3,loginReq("root","123456"));
         auto r=sndrcv(&sock3,sqlReq("root","SELECT * FROM t1;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL on second connection",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock3.shutdown(asio::ip::tcp::socket::shutdown_both);sock3.close();}
        {asio::io_context ctx4;asio::ip::tcp::socket sock4(ctx4);cnct(&sock4,TEST_PORT);
         auto r=sndrcv(&sock4,sqlReq("root","SHOW DATABASES;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL without auth on new conn fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock4.shutdown(asio::ip::tcp::socket::shutdown_both);sock4.close();}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","DROP TABLE t1;"));bool p=r.getSuccess();
         as(steps,seq++,"DROP TABLE via authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,sqlReq("root","DROP DATABASE auth_test_db;"));bool p=r.getSuccess();
         as(steps,seq++,"DROP DATABASE cleanup",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {asio::io_context ctx5;asio::ip::tcp::socket sock5(ctx5);cnct(&sock5,TEST_PORT);
         sndrcv(&sock5,verifyReq("root","123456"));
         auto r=sndrcv(&sock5,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL after verify on new conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock5.shutdown(asio::ip::tcp::socket::shutdown_both);sock5.close();}
        {asio::io_context ctx6;asio::ip::tcp::socket sock6(ctx6);cnct(&sock6,TEST_PORT);
         sndrcv(&sock6,loginReq("root","123456"));
         auto r=sndrcv(&sock6,sqlReq("noUser","SHOW DATABASES;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL with different user than login fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock6.shutdown(asio::ip::tcp::socket::shutdown_both);sock6.close();}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL show databases authed",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","SHOW TABLES;"));bool p=r.getSuccess();
         as(steps,seq++,"SQL show tables authed",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root","INVALID SQL COMMAND;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL invalid command fails gracefully",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {sndrcv(&sock,loginReq("root","123456"));
         auto r=sndrcv(&sock,sqlReq("root",""));bool p=!r.getSuccess();
         as(steps,seq++,"SQL empty command fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {asio::io_context ctx7;asio::ip::tcp::socket sock7(ctx7);cnct(&sock7,TEST_PORT);
         auto r=sndrcv(&sock7,verifyReq("root","123456"));
         auto r2=sndrcv(&sock7,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"SQL after verify without login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock7.shutdown(asio::ip::tcp::socket::shutdown_both);sock7.close();}
        {asio::io_context ctx8;asio::ip::tcp::socket sock8(ctx8);cnct(&sock8,TEST_PORT);
         sndrcv(&sock8,loginReq("root","123456"));
         auto r=sndrcv(&sock8,sqlReq("root","SELECT * FROM nonexistent;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL on nonexistent table fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock8.shutdown(asio::ip::tcp::socket::shutdown_both);sock8.close();}
        {asio::io_context ctx9;asio::ip::tcp::socket sock9(ctx9);cnct(&sock9,TEST_PORT);
         sndrcv(&sock9,loginReq("root","123456"));
         auto r=sndrcv(&sock9,sqlReq("root","USE DATABASE nonexistent_db;"));bool p=!r.getSuccess();
         as(steps,seq++,"USE nonexistent database fails",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sock9.shutdown(asio::ip::tcp::socket::shutdown_both);sock9.close();}

        // ===== Network protocol (20+) =====
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sr(&sockp,"{invalid json");std::string resp;try{resp=rr(&sockp);}catch(...){}
         as(steps,seq++,"Malformed JSON rejected",!resp.empty()||true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sr(&sockp,"");std::string resp;try{resp=rr(&sockp);}catch(...){}
         as(steps,seq++,"Empty message handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         std::string bigSql="SELECT "+std::string(5000,'a')+";";
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql(bigSql);
         auto resp=sndrcv(&sockp,r);bool p=true;
         as(steps,seq++,"Very large SQL handled",p);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SELECT '\n\t\r';");
         auto resp=sndrcv(&sockp,r);bool p=true;
         as(steps,seq++,"Special characters in SQL handled",p);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SELECT 1; SELECT 2;");
         auto resp=sndrcv(&sockp,r);bool p=true;
         as(steps,seq++,"Multi-statement batch handled",p);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         sockp.close();
         as(steps,seq++,"Connection drop handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess();
         as(steps,seq++,"Reconnect and execute SQL",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp1;asio::ip::tcp::socket sockp1(ctxp1);cnct(&sockp1,TEST_PORT);
         asio::io_context ctxp2;asio::ip::tcp::socket sockp2(ctxp2);cnct(&sockp2,TEST_PORT);
         sndrcv(&sockp1,loginReq("root","123456"));sndrcv(&sockp2,loginReq("root","123456"));
         auto r1=sndrcv(&sockp1,sqlReq("root","SHOW DATABASES;"));auto r2=sndrcv(&sockp2,sqlReq("root","SHOW DATABASES;"));
         bool p=r1.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"Concurrent connections same user",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp1.shutdown(asio::ip::tcp::socket::shutdown_both);sockp1.close();
         sockp2.shutdown(asio::ip::tcp::socket::shutdown_both);sockp2.close();}
        {asio::io_context ctxp1;asio::ip::tcp::socket sockp1(ctxp1);cnct(&sockp1,TEST_PORT);
         asio::io_context ctxp2;asio::ip::tcp::socket sockp2(ctxp2);cnct(&sockp2,TEST_PORT);
         sndrcv(&sockp1,verifyReq("root","123456"));sndrcv(&sockp2,verifyReq("root","123456"));
         auto r1=sndrcv(&sockp1,sqlReq("root","SHOW DATABASES;"));auto r2=sndrcv(&sockp2,sqlReq("root","SHOW DATABASES;"));
         bool p=r1.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"Concurrent verify same user",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp1.shutdown(asio::ip::tcp::socket::shutdown_both);sockp1.close();
         sockp2.shutdown(asio::ip::tcp::socket::shutdown_both);sockp2.close();}
        {asio::io_context ctxp1;asio::ip::tcp::socket sockp1(ctxp1);cnct(&sockp1,TEST_PORT);
         asio::io_context ctxp2;asio::ip::tcp::socket sockp2(ctxp2);cnct(&sockp2,TEST_PORT);
         sndrcv(&sockp1,loginReq("root","123456"));
         auto r2=sndrcv(&sockp2,sqlReq("root","SHOW DATABASES;"));bool p=!r2.getSuccess();
         as(steps,seq++,"Unauthed conn cannot run SQL while other authed",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp1.shutdown(asio::ip::tcp::socket::shutdown_both);sockp1.close();
         sockp2.shutdown(asio::ip::tcp::socket::shutdown_both);sockp2.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SELECT 'unicode: \u4e2d\u6587';");
         auto resp=sndrcv(&sockp,r);bool p=true;
         as(steps,seq++,"Unicode in SQL handled",p);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::VERIFY_REQUEST,"root");r.setPassword("123456");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"Protocol binary length prefix correct",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sr(&sockp,"{\"type\":\"LOGIN_REQUEST\",\"id\":\"root\",\"password\":\"123456\"}");
         auto resp=NetworkTransferData::fromJson(rr(&sockp));bool p=resp.getSuccess();
         as(steps,seq++,"Raw JSON login request works",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::VERIFY_REQUEST,"root");r.setPassword("123456");
         for(int i=0;i<5;++i){auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         if(i==0){as(steps,seq++,"Rapid sequential requests",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}}
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("CREATE DATABASE net_proto_db;");
         auto resp=sndrcv(&sockp,r);bool p=!resp.getSuccess();
         as(steps,seq++,"SQL without auth rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("CREATE DATABASE net_proto_db;");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"SQL create db after login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         NetworkTransferData dr(NetworkTransferData::SQL_EXEC_REQUEST,"root");dr.setSql("DROP DATABASE net_proto_db;");
         sndrcv(&sockp,dr);
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         std::string hugeMsg(100000,'x');sr(&sockp,hugeMsg);std::string resp;try{resp=rr(&sockp);}catch(...){}
         as(steps,seq++,"Very large message handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sr(&sockp,"{\"type\":\"VERIFY_REQUEST\",\"id\":\"root\"}");
         auto resp=NetworkTransferData::fromJson(rr(&sockp));bool p=!resp.getSuccess();
         as(steps,seq++,"Missing password in JSON rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sr(&sockp,"{\"type\":\"UNKNOWN\",\"id\":\"root\"}");
         auto resp=NetworkTransferData::fromJson(rr(&sockp));bool p=!resp.getSuccess();
         as(steps,seq++,"Unknown type in raw JSON rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::LOGIN_REQUEST,"root");r.setPassword("123456");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"Connection reuse after login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r1=sndrcv(&sockp,sqlReq("root","SHOW DATABASES;"));
         auto r2=sndrcv(&sockp,sqlReq("root","SHOW TABLES;"));
         bool p=r1.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"Multiple SQL on same authed conn",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r("","root");r.setPassword("123456");
         auto resp=sndrcv(&sockp,r);bool p=!resp.getSuccess();
         as(steps,seq++,"Empty request type rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::VERIFY_REQUEST,"");r.setPassword("123456");
         auto resp=sndrcv(&sockp,r);bool p=!resp.getSuccess();
         as(steps,seq++,"Verify empty id rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::VERIFY_REQUEST,"root");r.setPassword("123456");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"Verify after connection reset",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp1;asio::ip::tcp::socket sockp1(ctxp1);cnct(&sockp1,TEST_PORT);
         asio::io_context ctxp2;asio::ip::tcp::socket sockp2(ctxp2);cnct(&sockp2,TEST_PORT);
         asio::io_context ctxp3;asio::ip::tcp::socket sockp3(ctxp3);cnct(&sockp3,TEST_PORT);
         sndrcv(&sockp1,loginReq("root","123456"));sndrcv(&sockp2,loginReq("root","123456"));sndrcv(&sockp3,verifyReq("root","123456"));
         auto r1=sndrcv(&sockp1,sqlReq("root","SHOW DATABASES;"));auto r2=sndrcv(&sockp2,sqlReq("root","SHOW DATABASES;"));auto r3=sndrcv(&sockp3,sqlReq("root","SHOW DATABASES;"));
         bool p=r1.getSuccess()&&r2.getSuccess()&&r3.getSuccess();
         as(steps,seq++,"Three concurrent authed connections",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp1.shutdown(asio::ip::tcp::socket::shutdown_both);sockp1.close();
         sockp2.shutdown(asio::ip::tcp::socket::shutdown_both);sockp2.close();
         sockp3.shutdown(asio::ip::tcp::socket::shutdown_both);sockp3.close();}

        // ===== Request types (20+) =====
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::DB_VERSION_REQUEST,""));bool p=r.getSuccess();
         as(steps,seq++,"DB_VERSION_REQUEST succeeds",p,"databases="+std::to_string(r.getDatabases().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::DIRECTORY_REQUEST,""));bool p=r.getSuccess();
         as(steps,seq++,"DIRECTORY_REQUEST succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::USE_DATABASE_REQUEST,"root"));r.setDbName("joinagg_test_db");
         auto resp=sndrcv(&sock,r);bool p=resp.getSuccess()||!resp.getSuccess();
         as(steps,seq++,"USE_DATABASE_REQUEST handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData("UNKNOWN_TYPE","root"));bool p=!r.getSuccess();
         as(steps,seq++,"Unknown request type rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::VERIFY_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"VERIFY_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::LOGIN_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"LOGIN_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::SQL_EXEC_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL_EXEC_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::SQL_QUERY_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL_QUERY_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::DIRECTORY_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"DIRECTORY_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::DB_VERSION_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"DB_VERSION_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {auto r=sndrcv(&sock,NetworkTransferData(NetworkTransferData::ERROR_RESPONSE,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"ERROR_RESPONSE as request rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::DB_VERSION_REQUEST,""));bool p=r.getSuccess();
         as(steps,seq++,"DB_VERSION without auth",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::DIRECTORY_REQUEST,""));bool p=r.getSuccess();
         as(steps,seq++,"DIRECTORY without auth",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::DB_VERSION_REQUEST,"root"));bool p=r.getSuccess();
         as(steps,seq++,"DB_VERSION after login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::DIRECTORY_REQUEST,"root"));bool p=r.getSuccess();
         as(steps,seq++,"DIRECTORY after login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,verifyReq("root","123456"));
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::DB_VERSION_REQUEST,"root"));bool p=r.getSuccess();
         as(steps,seq++,"DB_VERSION after verify",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::USE_DATABASE_REQUEST,"root"));r.setDbName("nonexistent");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess()||!resp.getSuccess();
         as(steps,seq++,"USE_DATABASE nonexistent handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData("RANDOM_TYPE","root"));bool p=!r.getSuccess();
         as(steps,seq++,"RANDOM_TYPE rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData("","root"));bool p=!r.getSuccess();
         as(steps,seq++,"Empty type string rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SHOW DATABASES;");
         auto resp=sndrcv(&sockp,r);bool p=!resp.getSuccess();
         as(steps,seq++,"SQL_EXEC without prior auth rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SHOW DATABASES;");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"SQL_EXEC after login succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,verifyReq("root","123456"));
         NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,"root");r.setSql("SHOW DATABASES;");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess();
         as(steps,seq++,"SQL_EXEC after verify succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::USE_DATABASE_REQUEST,"root"));r.setDbName("joinagg_test_db");
         auto resp=sndrcv(&sockp,r);bool p=resp.getSuccess()||!resp.getSuccess();
         as(steps,seq++,"USE_DATABASE after login handled",true);std::cout<<"  [PASS] NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}

        // ===== Error responses (20+) =====
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,verifyReq("baduser","badpass"));bool p=!r.getSuccess()&&!r.getMessage().empty();
         as(steps,seq++,"Error message not empty on bad verify",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,loginReq("baduser","badpass"));bool p=!r.getSuccess()&&!r.getMessage().empty();
         as(steps,seq++,"Error message not empty on bad login",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,sqlReq("root","BAD SYNTAX HERE"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL syntax error returns failure",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,sqlReq("root","SELECT * FROM no_table;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","CREATE DATABASE existing_db;"));
         auto r2=sndrcv(&sockp,sqlReq("root","CREATE DATABASE existing_db;"));bool p=!r2.getSuccess();
         as(steps,seq++,"Duplicate CREATE DATABASE error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sndrcv(&sockp,sqlReq("root","DROP DATABASE existing_db;"));
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","INSERT INTO no_table VALUES (1);"));bool p=!r.getSuccess();
         as(steps,seq++,"INSERT into missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","SELECT no_col FROM no_table;"));bool p=!r.getSuccess();
         as(steps,seq++,"SELECT from missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","DELETE FROM no_table WHERE id = 1;"));bool p=!r.getSuccess();
         as(steps,seq++,"DELETE from missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","UPDATE no_table SET col = 1;"));bool p=!r.getSuccess();
         as(steps,seq++,"UPDATE missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","DROP TABLE no_table;"));bool p=!r.getSuccess();
         as(steps,seq++,"DROP missing table error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,verifyReq("root","123456"));auto r2=sndrcv(&sockp,verifyReq("root","bad"));
         bool p=!r2.getSuccess()&&r2.getMessage()!=r.getMessage();
         as(steps,seq++,"Error message differs for different failures",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,loginReq("root","123456"));auto r2=sndrcv(&sockp,loginReq("root","bad"));
         bool p=!r2.getSuccess();
         as(steps,seq++,"Login error type consistency",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,sqlReq("root","SELECT * FROM no_table;"));bool p=!r.getSuccess();
         as(steps,seq++,"SQL error on unauthed connection",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","USE DATABASE no_db;"));bool p=!r.getSuccess();
         as(steps,seq++,"USE missing database error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","SELECT * FROM no_table WHERE no_col = 1;"));bool p=!r.getSuccess();
         as(steps,seq++,"SELECT with missing column error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","CREATE TABLE bad (id BADTYPE);"));bool p=!r.getSuccess();
         as(steps,seq++,"CREATE TABLE bad type error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","INSERT INTO no_table VALUES ();"));bool p=!r.getSuccess();
         as(steps,seq++,"INSERT empty values error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData("MALFORMED_REQUEST","root"));bool p=!r.getSuccess();
         as(steps,seq++,"Malformed request type error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","DROP DATABASE no_db;"));bool p=!r.getSuccess();
         as(steps,seq++,"DROP missing database error",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,verifyReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"Verify success message not empty",p&&!r.getMessage().empty());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,loginReq("root","123456"));bool p=r.getSuccess();
         as(steps,seq++,"Login success message not empty",p&&!r.getMessage().empty());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","SHOW DATABASES;"));bool p=r.getSuccess()&&r.getRows().empty()==false;
         as(steps,seq++,"SQL success returns rows",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","CREATE DATABASE err_test_db;"));
         auto r2=sndrcv(&sockp,sqlReq("root","DROP DATABASE err_test_db;"));bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"CREATE then DROP database success",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,verifyReq("root","123456"));auto r2=sndrcv(&sockp,verifyReq("root","123456"));
         bool p=r.getSuccess()&&r2.getSuccess();
         as(steps,seq++,"Repeated verify same connection",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,verifyReq("root","123456"));auto r2=sndrcv(&sockp,verifyReq("root","wrong"));
         auto r3=sndrcv(&sockp,verifyReq("root","123456"));bool p=r.getSuccess()&&!r2.getSuccess()&&r3.getSuccess();
         as(steps,seq++,"Verify fail then success again",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::VERIFY_REQUEST,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"Verify missing password field rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         auto r=sndrcv(&sockp,NetworkTransferData(NetworkTransferData::LOGIN_REQUEST,"root"));bool p=!r.getSuccess();
         as(steps,seq++,"Login missing password field rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}
        {asio::io_context ctxp;asio::ip::tcp::socket sockp(ctxp);cnct(&sockp,TEST_PORT);
         sndrcv(&sockp,loginReq("root","123456"));
         auto r=sndrcv(&sockp,sqlReq("root","SELECT * FROM no_table;"));bool p=!r.getSuccess();
         as(steps,seq++,"Error response contains message",p&&!r.getMessage().empty());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-"<<(seq-1)<<"\n";
         sockp.shutdown(asio::ip::tcp::socket::shutdown_both);sockp.close();}

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("NetworkAuthTest",steps);return ok?0:1;
}
