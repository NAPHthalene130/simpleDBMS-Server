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
    const std::string DB="joinagg_test_db",UID="jaTest";
    const std::string O="orders",C="customers",P="products",CAT="categories",E="employees",D="departments";
    std::vector<TestStepResult> steps;bool ok=false;std::string fatal;
    std::cout<<"\n========== Executor JoinAgg Test ==========\n";
    Core core;std::unique_ptr<NetReceiver> recv;
    try{
        recv=std::make_unique<NetReceiver>(&core,TEST_PORT);recv->start();
        asio::io_context ctx;asio::ip::tcp::socket sock(ctx);cnct(&sock,TEST_PORT);
        auto ex=[&](const std::string&sql,const std::string&db="",uint64_t v=0){
            NetworkTransferData r(NetworkTransferData::SQL_EXEC_REQUEST,UID);r.setSql(sql);
            if(!db.empty()){r.setDbName(db);r.setDbVersion(v);}return sndrcv(&sock,r);};

        int seq=1;

        // ===== SETUP: DATABASE =====
        ex("DROP DATABASE "+DB+";");
        {auto r=ex("CREATE DATABASE "+DB+";");appendStep(steps,seq++,"CREATE DATABASE",r.getSuccess(),r.getMessage());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("USE DATABASE "+DB+";");appendStep(steps,seq++,"USE DATABASE",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: TABLES =====
        uint64_t v=1;
        {auto r=ex("CREATE TABLE "+O+" (id INT PRIMARY KEY, customer_id INT, product_id INT, amount FLOAT, qty INT);",DB,0);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE orders",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+C+" (id INT PRIMARY KEY, name VARCHAR(50), city VARCHAR(50), region VARCHAR(50));",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE customers",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+P+" (id INT PRIMARY KEY, name VARCHAR(50), category_id INT, price FLOAT);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE products",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+CAT+" (id INT PRIMARY KEY, name VARCHAR(50));",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE categories",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+E+" (id INT PRIMARY KEY, name VARCHAR(50), dept_id INT, salary FLOAT);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE employees",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("CREATE TABLE "+D+" (id INT PRIMARY KEY, name VARCHAR(50), location VARCHAR(50));",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"CREATE TABLE departments",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT customers =====
        {auto r=ex("INSERT INTO "+C+" VALUES (1, 'Alice', 'NY', 'East');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT customer Alice",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+C+" VALUES (2, 'Bob', 'LA', 'West');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT customer Bob",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+C+" VALUES (3, 'Carol', 'NY', 'East');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT customer Carol",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+C+" VALUES (4, 'Dave', 'SF', 'West');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT customer Dave",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+C+" VALUES (5, 'Eve', 'LA', 'West');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT customer Eve",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT categories =====
        {auto r=ex("INSERT INTO "+CAT+" VALUES (1, 'Electronics');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT category Electronics",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+CAT+" VALUES (2, 'Clothing');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT category Clothing",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+CAT+" VALUES (3, 'Food');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT category Food",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT products =====
        {auto r=ex("INSERT INTO "+P+" VALUES (1, 'Laptop', 1, 1200.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT product Laptop",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+P+" VALUES (2, 'Shirt', 2, 35.5);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT product Shirt",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+P+" VALUES (3, 'Phone', 1, 800.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT product Phone",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+P+" VALUES (4, 'Apple', 3, 1.5);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT product Apple",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+P+" VALUES (5, 'Jeans', 2, 60.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT product Jeans",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT departments =====
        {auto r=ex("INSERT INTO "+D+" VALUES (1, 'Engineering', 'Building A');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT dept Engineering",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+D+" VALUES (2, 'Sales', 'Building B');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT dept Sales",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+D+" VALUES (3, 'HR', 'Building C');",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT dept HR",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT employees =====
        {auto r=ex("INSERT INTO "+E+" VALUES (1, 'John', 1, 90000.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT emp John",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+E+" VALUES (2, 'Jane', 1, 95000.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT emp Jane",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+E+" VALUES (3, 'Jim', 2, 60000.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT emp Jim",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+E+" VALUES (4, 'Jack', 2, 62000.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT emp Jack",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+E+" VALUES (5, 'Jill', 3, 55000.0);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT emp Jill",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== SETUP: INSERT orders =====
        {auto r=ex("INSERT INTO "+O+" VALUES (1, 1, 1, 100.5, 2);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 1",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (2, 1, 3, 200.0, 1);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 2",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (3, 2, 2, 150.0, 3);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 3",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (4, 2, 5, 75.0, 1);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 4",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (5, 3, 4, 12.0, 10);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 5",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (6, 3, 1, 1200.0, 1);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 6",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (7, 4, 3, 800.0, 1);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 7",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("INSERT INTO "+O+" VALUES (8, 5, 2, 35.5, 2);",DB,v);if(r.getSuccess())v=r.getDbVersion();appendStep(steps,seq++,"INSERT order 8",r.getSuccess());std::cout<<"  "<<(r.getSuccess()?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== JOIN OPERATIONS (40+) =====
        {auto r=ex("SELECT o.id, c.name, o.amount FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"INNER JOIN orders+customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.id, c.name FROM "+C+" c LEFT JOIN "+O+" o ON c.id = o.customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"LEFT JOIN customers+orders",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o RIGHT JOIN "+C+" c ON o.customer_id = c.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"RIGHT JOIN orders+customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN default INNER",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT a.id, b.name FROM "+C+" a JOIN "+C+" b ON a.id = b.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"SELF JOIN customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE o.amount > 50;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with WHERE",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id ORDER BY o.amount DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with ORDER BY",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id LIMIT 3;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with LIMIT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, p.name, o.amount FROM "+O+" o INNER JOIN "+P+" p ON o.product_id = p.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN orders+products",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name, p.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id INNER JOIN "+P+" p ON o.product_id = p.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Triple JOIN",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT e.name, d.name FROM "+E+" e INNER JOIN "+D+" d ON e.dept_id = d.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN employees+departments",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT e.name, d.name FROM "+E+" e LEFT JOIN "+D+" d ON e.dept_id = d.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"LEFT JOIN employees+departments",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT e.name, d.name FROM "+E+" e RIGHT JOIN "+D+" d ON e.dept_id = d.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"RIGHT JOIN employees+departments",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id AND c.region = 'East';",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with multiple conditions",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name, p.name, cat.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id JOIN "+P+" p ON o.product_id = p.id JOIN "+CAT+" cat ON p.category_id = cat.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN 4 tables",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id WHERE c.city = 'NY' ORDER BY o.amount;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + WHERE + ORDER BY",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id WHERE c.city = 'LA' LIMIT 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + WHERE + LIMIT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, p.name, o.qty * p.price AS total FROM "+O+" o JOIN "+P+" p ON o.product_id = p.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with expression alias",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id JOIN "+P+" p ON o.product_id = p.id WHERE p.price > 100;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with WHERE on joined table",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name FROM "+C+" c LEFT JOIN "+O+" o ON c.id = o.customer_id WHERE o.id IS NULL;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"LEFT JOIN find non-ordering customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE o.amount BETWEEN 50 AND 300;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with BETWEEN",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE c.name LIKE 'A%';",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with LIKE",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE c.city IN ('NY','SF');",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with IN",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT a.id, b.id FROM "+C+" a JOIN "+C+" b ON a.region = b.region WHERE a.id < b.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"SELF JOIN with region",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id ORDER BY c.name, o.amount DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with multi-column ORDER BY",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name, p.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id JOIN "+P+" p ON o.product_id = p.id ORDER BY o.amount DESC LIMIT 3;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Triple JOIN ORDER BY LIMIT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o LEFT JOIN "+C+" c ON o.customer_id = c.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"LEFT JOIN orders+customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, COUNT(o.id) FROM "+C+" c LEFT JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"LEFT JOIN + GROUP BY + COUNT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, SUM(o.amount) FROM "+C+" c INNER JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + GROUP BY + SUM",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, AVG(e.salary) FROM "+D+" d INNER JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + GROUP BY + AVG",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT p.name, COUNT(o.id) FROM "+P+" p LEFT JOIN "+O+" o ON p.id = o.product_id GROUP BY p.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + GROUP BY product counts",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, o.amount FROM "+C+" c INNER JOIN "+O+" o ON c.id = o.customer_id WHERE o.amount > (SELECT AVG(amount) FROM "+O+");",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with subquery in WHERE",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE c.region = 'East' AND o.amount > 100;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with AND condition",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE c.region = 'East' OR o.amount > 500;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with OR condition",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id JOIN "+P+" p ON o.product_id = p.id WHERE p.category_id = 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Triple JOIN with WHERE",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id ORDER BY o.id LIMIT 5 OFFSET 0;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with LIMIT only",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, p.name FROM "+C+" c CROSS JOIN "+P+" p;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"CROSS JOIN",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name, o.amount FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id ORDER BY o.amount ASC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN ORDER BY ASC",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, SUM(o.amount), AVG(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name HAVING SUM(o.amount) > 200;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + GROUP BY + HAVING SUM",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.city, COUNT(o.id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.city HAVING COUNT(o.id) >= 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + GROUP BY city HAVING COUNT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id WHERE o.id = 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with equality on PK",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE o.amount <> 100.5;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with not equal",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE o.amount >= 100;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with >=",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT o.id, c.name FROM "+O+" o INNER JOIN "+C+" c ON o.customer_id = c.id WHERE o.amount <= 200;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with <=",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT e.name, d.name, e.salary FROM "+E+" e JOIN "+D+" d ON e.dept_id = d.id WHERE e.salary > 60000 ORDER BY e.salary DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN + WHERE + ORDER BY salary",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, o.amount FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id JOIN "+P+" p ON o.product_id = p.id WHERE p.price < 100;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Triple JOIN price filter",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.region, SUM(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.region;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN GROUP BY region SUM",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.region, COUNT(DISTINCT o.customer_id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.region;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"JOIN with COUNT DISTINCT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== AGGREGATIONS (30+) =====
        {auto r=ex("SELECT COUNT(*) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT(*) orders",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(id) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT(column) orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) FROM "+C+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT(*) customers",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"SUM amount orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(qty) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"SUM qty orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT AVG(amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"AVG amount orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT AVG(salary) FROM "+E+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"AVG salary employees",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT MIN(amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"MIN amount orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT MAX(amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"MAX amount orders",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT MIN(salary), MAX(salary) FROM "+E+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"MIN MAX salary employees",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT MIN(price), MAX(price), AVG(price) FROM "+P+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"MIN MAX AVG price products",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*), SUM(amount), AVG(amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"Multiple aggregates together",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) FROM "+O+" WHERE amount > 100;",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT with WHERE",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(amount) FROM "+O+" WHERE customer_id = 1;",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"SUM with WHERE customer_id=1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT AVG(amount) FROM "+O+" WHERE qty > 1;",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"AVG with WHERE qty>1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT MIN(amount), MAX(amount) FROM "+O+" WHERE customer_id = 2;",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"MIN MAX with WHERE customer_id=2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(o.amount) FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id WHERE c.region = 'West';",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"SUM on joined tables with WHERE",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) FROM "+E+" e JOIN "+D+" d ON e.dept_id = d.id WHERE d.name = 'Engineering';",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"COUNT on joined tables",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT AVG(e.salary) FROM "+E+" e JOIN "+D+" d ON e.dept_id = d.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"AVG on joined tables",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(DISTINCT customer_id) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT DISTINCT customer_id",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(DISTINCT amount) FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"SUM DISTINCT amount",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) AS cnt FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT with alias",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT SUM(amount) AS total FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"SUM with alias",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT AVG(amount) AS avg_amt FROM "+O+";",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"AVG with alias",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) FROM "+O+" o JOIN "+C+" c ON o.customer_id = c.id JOIN "+P+" p ON o.product_id = p.id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"COUNT triple join",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"COUNT with GROUP BY",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount), AVG(amount) FROM "+O+" GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"SUM AVG with GROUP BY",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT product_id, MIN(amount), MAX(amount) FROM "+O+" GROUP BY product_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"MIN MAX with GROUP BY product",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, COUNT(o.id), SUM(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Aggregates on joined tables GROUP BY",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT COUNT(*) FROM "+O+" WHERE amount > 99999;",DB,v);bool p=r.getSuccess();if(p)v=r.getDbVersion();
         appendStep(steps,seq++,"COUNT empty result",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== GROUP BY (20+) =====
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY single column",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT region, city, COUNT(*) FROM "+C+" GROUP BY region, city;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY multiple columns",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount) FROM "+O+" GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY with SUM",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT product_id, AVG(amount) FROM "+O+" GROUP BY product_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY with AVG",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, COUNT(*) FROM "+E+" GROUP BY dept_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY dept_id COUNT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, MIN(salary), MAX(salary) FROM "+E+" GROUP BY dept_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY dept_id MIN MAX",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id HAVING COUNT(*) > 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY + HAVING COUNT>1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount) FROM "+O+" GROUP BY customer_id HAVING SUM(amount) > 200;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY + HAVING SUM>200",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT region, COUNT(*) FROM "+C+" GROUP BY region HAVING COUNT(*) >= 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY region HAVING COUNT>=2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT city, COUNT(*) FROM "+C+" GROUP BY city HAVING COUNT(*) = 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY city HAVING COUNT=2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" WHERE amount > 50 GROUP BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY with WHERE",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id ORDER BY COUNT(*) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY ORDER BY COUNT DESC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount) FROM "+O+" GROUP BY customer_id ORDER BY SUM(amount);",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY ORDER BY SUM ASC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, COUNT(o.id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY on joined table",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.region, COUNT(o.id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.region HAVING COUNT(o.id) > 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY joined table HAVING",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, COUNT(e.id) FROM "+D+" d JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY department names COUNT",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, AVG(e.salary) FROM "+D+" d JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name ORDER BY AVG(e.salary) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY dept AVG ORDER BY",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT product_id, COUNT(*) FROM "+O+" GROUP BY product_id HAVING COUNT(*) >= 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY product HAVING COUNT>=1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT city, SUM(1) FROM "+C+" GROUP BY city;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY city constant agg",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" WHERE qty >= 1 GROUP BY customer_id HAVING COUNT(*) > 0 ORDER BY customer_id;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY WHERE HAVING ORDER BY",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT region, city, COUNT(*) FROM "+C+" GROUP BY region, city HAVING COUNT(*) = 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"GROUP BY multi-col HAVING COUNT=1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== HAVING (15+) =====
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id HAVING COUNT(*) > 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING with COUNT>1",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount) FROM "+O+" GROUP BY customer_id HAVING SUM(amount) > 300;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING with SUM>300",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, AVG(amount) FROM "+O+" GROUP BY customer_id HAVING AVG(amount) > 100;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING with AVG>100",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, MIN(salary) FROM "+E+" GROUP BY dept_id HAVING MIN(salary) > 50000;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING with MIN>50000",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, MAX(salary) FROM "+E+" GROUP BY dept_id HAVING MAX(salary) < 100000;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING with MAX<100000",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" WHERE amount > 50 GROUP BY customer_id HAVING COUNT(*) >= 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING combined with WHERE",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, COUNT(o.id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name HAVING COUNT(o.id) > 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING on joined tables COUNT",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, SUM(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name HAVING SUM(o.amount) > 200;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING on joined tables SUM",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.region, COUNT(o.id) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.region HAVING COUNT(o.id) > 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING region COUNT>2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, AVG(e.salary) FROM "+D+" d JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name HAVING AVG(e.salary) > 60000;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING dept AVG>60000",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*), SUM(amount) FROM "+O+" GROUP BY customer_id HAVING COUNT(*) > 1 AND SUM(amount) > 150;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING complex AND condition",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id HAVING COUNT(*) = 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING COUNT=2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, COUNT(*) FROM "+E+" GROUP BY dept_id HAVING COUNT(*) >= 2;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING dept COUNT>=2",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT product_id, COUNT(*) FROM "+O+" GROUP BY product_id HAVING COUNT(*) BETWEEN 1 AND 3;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING COUNT BETWEEN",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, SUM(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name HAVING SUM(o.amount) >= 100 AND SUM(o.amount) <= 1500;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING joined SUM range",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, COUNT(e.id), MIN(e.salary), MAX(e.salary) FROM "+D+" d JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name HAVING MIN(e.salary) > 50000 AND MAX(e.salary) < 100000;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"HAVING joined MIN MAX range",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== ORDER BY with aggregates (5+) =====
        {auto r=ex("SELECT customer_id, COUNT(*) FROM "+O+" GROUP BY customer_id ORDER BY COUNT(*) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY COUNT(*) DESC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, SUM(amount) FROM "+O+" GROUP BY customer_id ORDER BY SUM(amount);",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY SUM(amount) ASC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT dept_id, AVG(salary) FROM "+E+" GROUP BY dept_id ORDER BY AVG(salary) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY AVG(salary) DESC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT product_id, COUNT(*) FROM "+O+" GROUP BY product_id ORDER BY COUNT(*) ASC LIMIT 3;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY COUNT(*) ASC LIMIT",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT c.name, SUM(o.amount) FROM "+C+" c JOIN "+O+" o ON c.id = o.customer_id GROUP BY c.name ORDER BY SUM(o.amount) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY SUM on joined table",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT d.name, COUNT(e.id) FROM "+D+" d JOIN "+E+" e ON d.id = e.dept_id GROUP BY d.name ORDER BY COUNT(e.id) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY COUNT on joined dept",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT customer_id, MIN(amount), MAX(amount) FROM "+O+" GROUP BY customer_id ORDER BY MAX(amount) DESC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY MAX(amount) DESC",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT region, COUNT(*) FROM "+C+" GROUP BY region ORDER BY COUNT(*) ASC;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"ORDER BY COUNT(*) ASC region",p);std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== UNION (10+) =====
        {auto r=ex("SELECT name FROM "+C+" UNION SELECT name FROM "+D+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION customers+departments",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+C+" UNION ALL SELECT name FROM "+D+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION ALL customers+departments",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT id, name FROM "+E+" UNION SELECT id, name FROM "+C+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION employees+customers",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT id FROM "+O+" UNION SELECT id FROM "+P+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION orders+products ids",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+CAT+" UNION ALL SELECT name FROM "+P+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION ALL categories+products",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT city FROM "+C+" UNION SELECT location FROM "+D+" ORDER BY city;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION with ORDER BY",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+C+" UNION SELECT name FROM "+D+" LIMIT 4;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION with LIMIT",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT id, name FROM "+P+" UNION SELECT id, name FROM "+CAT+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION products+categories",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+D+" UNION ALL SELECT name FROM "+D+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION ALL same table",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+E+" UNION SELECT name FROM "+D+" UNION SELECT name FROM "+CAT+";",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"Triple UNION",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT id FROM "+O+" WHERE amount > 100 UNION SELECT id FROM "+O+" WHERE qty > 1;",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION with WHERE clauses",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}
        {auto r=ex("SELECT name FROM "+C+" WHERE region = 'East' UNION SELECT name FROM "+C+" WHERE region = 'West';",DB,v);
         bool p=r.getSuccess();if(p)v=r.getDbVersion();appendStep(steps,seq++,"UNION same table different WHERE",p,"rows="+std::to_string(r.getRows().size()));std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" JA-"<<(seq-1)<<"\n";}

        // ===== CLEANUP =====
        ex("DROP TABLE "+O+";",DB,v);
        ex("DROP TABLE "+C+";",DB,v);
        ex("DROP TABLE "+P+";",DB,v);
        ex("DROP TABLE "+CAT+";",DB,v);
        ex("DROP TABLE "+E+";",DB,v);
        ex("DROP TABLE "+D+";",DB,v);
        ex("DROP DATABASE "+DB+";");
        appendStep(steps,seq++,"DROP all tables and database",true,"ok");std::cout<<"  [PASS] JA-"<<(seq-1)<<"\n";

        sock.shutdown(asio::ip::tcp::socket::shutdown_both);sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto&s){return s.passed;});
    }catch(std::exception&e){fatal=e.what();ok=false;}
    if(recv)recv->stop();
    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    wrl("ExecutorJoinAggTest",steps);return ok?0:1;
}
