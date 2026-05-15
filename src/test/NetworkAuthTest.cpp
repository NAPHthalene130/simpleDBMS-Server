/**
 * @file NetworkAuthTest.cpp
 * @brief 网络认证与会话测试
 * @details 测试登录/验证流程、无效凭证、多连接会话隔离。
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
}

int main(){
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Network Auth Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);

        // 1. VERIFY with default root/123456
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"root");req.setPassword("123456");
         auto r=sndrcv(&sock,req);bool p=r.getSuccess();
         as(steps,1,"VERIFY with correct credentials",p,r.getMessage());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-1\n";}
        // 2. VERIFY with wrong password
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"root");req.setPassword("wrong");
         auto r=sndrcv(&sock,req);bool p=!r.getSuccess();
         as(steps,2,"VERIFY with wrong password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-2\n";}
        // 3. VERIFY with empty credentials
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"");auto r=sndrcv(&sock,req);
         as(steps,3,"VERIFY with empty user rejected",!r.getSuccess());std::cout<<"  "<<(!r.getSuccess()?"[PASS]":"[FAIL]")<<" NA-3\n";}
        // 4. LOGIN with correct credentials
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("123456");
         auto r=sndrcv(&sock,req);bool p=r.getSuccess();
         as(steps,4,"LOGIN with correct credentials",p,r.getMessage());std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-4\n";}
        // 5. LOGIN with wrong password
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("bad");
         auto r=sndrcv(&sock,req);bool p=!r.getSuccess();
         as(steps,5,"LOGIN with wrong password rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-5\n";}
        // 6. LOGIN with non-existent user
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"noSuchUser");req.setPassword("123456");
         auto r=sndrcv(&sock,req);bool p=!r.getSuccess();
         as(steps,6,"LOGIN with non-existent user rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-6\n";}
        // 7. After LOGIN, can execute SQL
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("123456");sndrcv(&sock,req);
         NetworkTransferData q(NetworkTransferData::SQL_EXEC_REQUEST,"root");q.setSql("SHOW DATABASES;");
         auto r=sndrcv(&sock,q);bool p=r.getSuccess();
         as(steps,7,"Execute SQL after login succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-7\n";}

        // 8. Second connection - VERIFY
        asio::io_context ctx2;asio::ip::tcp::socket sock2(ctx2);cnct(&sock2,TEST_PORT);
        {NetworkTransferData req(NetworkTransferData::VERIFY_REQUEST,"root");req.setPassword("123456");
         auto r=sndrcv(&sock2,req);
         as(steps,8,"Second connection VERIFY succeeds",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" NA-8\n";}
        // 9. Second connection LOGIN
        {NetworkTransferData req(NetworkTransferData::LOGIN_REQUEST,"root");req.setPassword("123456");
         auto r=sndrcv(&sock2,req);
         as(steps,9,"Second connection LOGIN succeeds",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" NA-9\n";}
        sock2.shutdown(asio::ip::tcp::socket::shutdown_both);sock2.close();

        // 10. DB_VERSION_REQUEST
        {NetworkTransferData req(NetworkTransferData::DB_VERSION_REQUEST,"");
         auto r=sndrcv(&sock,req);bool p=r.getSuccess();
         as(steps,10,"DB_VERSION_REQUEST succeeds",p,"databases="+std::to_string(r.getDatabases().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-10\n";}
        // 11. DIRECTORY_REQUEST
        {NetworkTransferData req(NetworkTransferData::DIRECTORY_REQUEST,"");
         auto r=sndrcv(&sock,req);bool p=r.getSuccess();
         as(steps,11,"DIRECTORY_REQUEST succeeds",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-11\n";}
        // 12. Unknown request type
        {NetworkTransferData req("UNKNOWN_TYPE","root");
         auto r=sndrcv(&sock,req);bool p=!r.getSuccess();
         as(steps,12,"Unknown request type rejected",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" NA-12\n";}

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("NetworkAuthTest",steps);return ok?0:1;
}
