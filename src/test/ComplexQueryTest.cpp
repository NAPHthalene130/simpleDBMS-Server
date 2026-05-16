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

constexpr unsigned short TEST_PORT = 19099;
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
    if (r.getDbVersion() > 0) ver = r.getDbVersion();
    else if (!r.getSuccess()) ver++;
}

} // namespace

int main() {
    const std::string DB="complex_test_db", UID="complexTest";
    std::vector<TestStepResult> steps; bool ok=false; std::string fatal;
    std::cout << "\n========== Complex Query Test ==========\n";

    Core core; std::unique_ptr<NetReceiver> recv;
    try {
        recv = std::make_unique<NetReceiver>(&core, TEST_PORT); recv->start();
        asio::io_context ctx; asio::ip::tcp::socket sock(ctx); connectRetry(&sock, TEST_PORT);

        auto exec = [&](const std::string &sql, const std::string &db = "", uint64_t ver=0) {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, UID);
            req.setSql(sql); if (!db.empty()) { req.setDbName(db); req.setDbVersion(ver); }
            return sendRecv(&sock, req);
        };

        exec("DROP DATABASE " + DB + ";");

        int seq = 1;
        { auto r=exec("CREATE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("USE DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"USE DATABASE",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        uint64_t ver = 0;

        // ===================== Complex schema setup =====================
        { auto r=exec("CREATE TABLE employees (id INT PRIMARY KEY, name VARCHAR(50), dept_id INT, salary INT, age INT, manager_id INT);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE employees",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE departments (id INT PRIMARY KEY, dept_name VARCHAR(50), location VARCHAR(50), budget INT);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE departments",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE projects (id INT PRIMARY KEY, proj_name VARCHAR(50), dept_id INT, cost INT);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE projects",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("CREATE TABLE assignments (emp_id INT, proj_id INT, hours INT);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"CREATE TABLE assignments",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // Insert employees
        { auto r=exec("INSERT INTO employees VALUES (1, 'Alice', 1, 8000, 30, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Alice",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (2, 'Bob', 1, 7000, 25, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Bob",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (3, 'Carol', 2, 12000, 45, NULL);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Carol",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (4, 'Dave', 2, 6000, 28, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Dave",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (5, 'Eve', 3, 9000, 35, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Eve",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (6, 'Frank', 3, 5000, 22, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Frank",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (7, 'Grace', 1, 7500, 29, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Grace",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO employees VALUES (8, 'Henry', 2, 6500, 32, 3);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Henry",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // Insert departments
        { auto r=exec("INSERT INTO departments VALUES (1, 'Engineering', 'Building A', 500000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Eng dept",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO departments VALUES (2, 'Sales', 'Building B', 300000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Sales dept",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO departments VALUES (3, 'HR', 'Building C', 150000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT HR dept",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // Insert projects
        { auto r=exec("INSERT INTO projects VALUES (1, 'Alpha', 1, 100000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Alpha",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO projects VALUES (2, 'Beta', 1, 80000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Beta",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO projects VALUES (3, 'Gamma', 2, 60000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Gamma",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO projects VALUES (4, 'Delta', 3, 45000);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT Delta",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // Insert assignments
        { auto r=exec("INSERT INTO assignments VALUES (1, 1, 40);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 1-1",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (1, 2, 20);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 1-2",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (2, 1, 35);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 2-1",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (3, 3, 50);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 3-3",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (4, 2, 25);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 4-2",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (5, 4, 30);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 5-4",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (6, 4, 15);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 6-4",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (7, 1, 40);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 7-1",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("INSERT INTO assignments VALUES (8, 3, 20);",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"INSERT assign 8-3",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Complex WHERE conditions =====================
        { auto r=exec("SELECT * FROM employees WHERE salary > 6000 AND age < 35;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"WHERE salary>6000 AND age<35",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary > 6000 OR age < 25;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==6; appendStep(steps,seq++,"WHERE salary>6000 OR age<25",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE (salary > 7000 AND age > 28) OR (dept_id = 3 AND salary < 6000);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE (A AND B) OR (C AND D)",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary BETWEEN 6000 AND 9000 AND dept_id IN (1, 2);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE BETWEEN AND IN combined",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE NOT (salary < 6000);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"WHERE NOT (salary<6000)",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE age >= 25 AND age <= 35 AND salary >= 6000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE triple AND range",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE (dept_id = 1 OR dept_id = 2) AND salary > 6500;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE (OR) AND combined",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE name LIKE 'A%' OR name LIKE 'E%';",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"WHERE LIKE OR pattern",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary > 6000 AND (age < 30 OR dept_id = 2);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE AND (OR) nested",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE id IN (1, 3, 5, 7) AND salary >= 7500;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"WHERE IN AND comparison",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE NOT dept_id = 3 AND salary > 5000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE NOT AND",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE age > 25 AND age < 35 AND dept_id <> 3;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE range AND <>",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary >= 6000 AND salary <= 9000 AND age BETWEEN 25 AND 35;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE AND AND BETWEEN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE (id = 1 OR id = 2) AND (dept_id = 1 OR dept_id = 2);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"WHERE (OR) AND (OR)",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE name LIKE '%e%' AND salary > 6000;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"WHERE LIKE contain AND compare",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE id > 2 AND id < 7 AND dept_id >= 2 AND salary <= 9000;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"WHERE 4 chained conditions",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Complex JOINs =====================
        { auto r=exec("SELECT e.name, d.dept_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"INNER JOIN employees x departments",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name, d.location FROM employees e LEFT JOIN departments d ON e.dept_id = d.id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"LEFT JOIN employees x departments",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, p.proj_name FROM employees e INNER JOIN projects p ON e.dept_id = p.dept_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN employees x projects via dept_id",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name, p.proj_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id INNER JOIN projects p ON d.id = p.dept_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN employees-depts-projects",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, a.proj_id, a.hours FROM employees e INNER JOIN assignments a ON e.id = a.emp_id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==9; appendStep(steps,seq++,"JOIN employees x assignments",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name, a.hours FROM employees e INNER JOIN departments d ON e.dept_id = d.id INNER JOIN assignments a ON e.id = a.emp_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN e-d-a",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, p.proj_name, a.hours FROM employees e INNER JOIN projects p ON e.dept_id = p.dept_id INNER JOIN assignments a ON e.id = a.emp_id AND p.id = a.proj_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN with composite ON",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name FROM employees e LEFT JOIN departments d ON e.dept_id = d.id WHERE e.salary > 7000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"LEFT JOIN + WHERE salary>7000",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id ORDER BY e.salary DESC LIMIT 3;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"JOIN + ORDER BY + LIMIT 3",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, COUNT(e.id) FROM departments d LEFT JOIN employees e ON d.id = e.dept_id GROUP BY d.dept_name;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"LEFT JOIN + GROUP BY COUNT",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, p.proj_name FROM employees e INNER JOIN projects p ON e.dept_id = p.dept_id WHERE p.cost > 50000 ORDER BY p.cost DESC;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + WHERE + ORDER BY cost",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, a.proj_id FROM employees e INNER JOIN assignments a ON e.id = a.emp_id WHERE a.hours >= 30 ORDER BY a.hours DESC LIMIT 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"JOIN + WHERE hours + ORDER + LIMIT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e1.name AS emp, e2.name AS mgr FROM employees e1 INNER JOIN employees e2 ON e1.manager_id = e2.id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELF JOIN employees",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name, p.proj_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id LEFT JOIN projects p ON d.id = p.dept_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"INNER + LEFT mixed JOINs",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, e.name, a.hours FROM departments d INNER JOIN employees e ON d.id = e.dept_id INNER JOIN assignments a ON e.id = a.emp_id WHERE a.hours > 20;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN + WHERE hours>20",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Subqueries =====================
        { auto r=exec("SELECT * FROM employees WHERE dept_id IN (SELECT id FROM departments WHERE budget > 200000);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"Subquery IN departments budget",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE dept_id NOT IN (SELECT id FROM departments WHERE budget < 200000);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"Subquery NOT IN departments budget",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE EXISTS (SELECT 1 FROM departments WHERE departments.id = employees.dept_id AND budget > 400000);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Correlated EXISTS subquery",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary > (SELECT AVG(salary) FROM employees);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Scalar subquery AVG comparison",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE id IN (SELECT emp_id FROM assignments WHERE hours > 30);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"Subquery IN assignments hours",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE dept_id IN (SELECT dept_id FROM projects WHERE cost > 70000);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Subquery IN projects cost",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE NOT EXISTS (SELECT 1 FROM assignments WHERE assignments.emp_id = employees.id AND hours < 20);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"NOT EXISTS correlated subquery",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name, salary FROM employees WHERE salary > (SELECT MIN(salary) FROM employees WHERE dept_id = 2);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Scalar subquery MIN with filter",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM departments WHERE id IN (SELECT DISTINCT dept_id FROM employees WHERE salary > 7500);",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"Subquery with DISTINCT",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE dept_id = (SELECT id FROM departments WHERE dept_name = 'Engineering');",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"Scalar subquery exact match",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name FROM employees e WHERE e.id IN (SELECT a.emp_id FROM assignments a INNER JOIN projects p ON a.proj_id = p.id WHERE p.cost > 50000);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Nested subquery with JOIN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE age BETWEEN (SELECT MIN(age) FROM employees) AND (SELECT MIN(age)+10 FROM employees);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Subqueries in BETWEEN",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Complex aggregations =====================
        { auto r=exec("SELECT dept_id, COUNT(*), AVG(salary), MIN(age), MAX(age) FROM employees GROUP BY dept_id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"GROUP BY dept with 4 aggregates",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, COUNT(*) FROM employees GROUP BY dept_id HAVING COUNT(*) > 2;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"GROUP BY + HAVING COUNT>2",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, AVG(salary) FROM employees GROUP BY dept_id HAVING AVG(salary) > 7000;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"GROUP BY + HAVING AVG>7000",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, COUNT(e.id), AVG(e.salary) FROM employees e INNER JOIN departments d ON e.dept_id = d.id GROUP BY d.dept_name;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"JOIN + GROUP BY + aggregates",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, COUNT(e.id) FROM employees e INNER JOIN departments d ON e.dept_id = d.id GROUP BY d.dept_name HAVING COUNT(e.id) >= 2;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"JOIN + GROUP BY + HAVING >=2",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT proj_id, SUM(hours), AVG(hours) FROM assignments GROUP BY proj_id HAVING SUM(hours) > 50;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==2; appendStep(steps,seq++,"GROUP BY proj with SUM/AVG HAVING",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, COUNT(a.proj_id) FROM employees e INNER JOIN assignments a ON e.id = a.emp_id GROUP BY e.name;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + GROUP BY + COUNT projects",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, COUNT(*), SUM(salary) FROM employees WHERE age > 25 GROUP BY dept_id HAVING SUM(salary) > 15000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE + GROUP BY + HAVING SUM",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, MIN(salary), MAX(salary), MAX(salary)-MIN(salary) AS range FROM employees GROUP BY dept_id;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY with calculated range",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, e.name, e.salary FROM departments d INNER JOIN employees e ON d.id = e.dept_id WHERE e.salary = (SELECT MAX(salary) FROM employees WHERE dept_id = d.id);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + correlated MAX subquery",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== UNION / Set operations =====================
        { auto r=exec("SELECT name FROM employees WHERE dept_id = 1 UNION SELECT name FROM employees WHERE salary > 8000;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==4; appendStep(steps,seq++,"UNION two filtered sets",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE dept_id = 1 UNION ALL SELECT name FROM employees WHERE salary > 8000;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"UNION ALL duplicates kept",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id FROM employees WHERE dept_id = 1 UNION SELECT id FROM employees WHERE dept_id = 2 UNION SELECT id FROM employees WHERE dept_id = 3;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"Triple UNION all employees",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_name FROM departments WHERE budget > 200000 UNION ALL SELECT dept_name FROM departments WHERE location = 'Building B';",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"UNION ALL on departments",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE salary > 7000 UNION SELECT proj_name FROM projects WHERE cost > 60000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"UNION across different tables",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE dept_id = 1 UNION SELECT name FROM employees WHERE dept_id = 2 ORDER BY name;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"UNION with ORDER BY",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Complex ORDER BY =====================
        { auto r=exec("SELECT * FROM employees ORDER BY dept_id ASC, salary DESC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"ORDER BY dept ASC salary DESC",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees ORDER BY age DESC, name ASC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"ORDER BY age DESC name ASC",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees ORDER BY salary DESC, age ASC, name ASC LIMIT 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"ORDER BY 3 cols + LIMIT 5",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, name, salary FROM employees ORDER BY dept_id ASC, salary DESC LIMIT 3;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"ORDER BY dept+salary LIMIT 3",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM assignments ORDER BY hours DESC, emp_id ASC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==9; appendStep(steps,seq++,"ORDER BY hours DESC emp_id ASC",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name, salary FROM employees ORDER BY salary DESC LIMIT 1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"ORDER BY salary DESC LIMIT 1 (top earner)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name, age FROM employees ORDER BY age ASC LIMIT 1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==1; appendStep(steps,seq++,"ORDER BY age ASC LIMIT 1 (youngest)",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, e.name, e.salary FROM employees e INNER JOIN departments d ON e.dept_id = d.id ORDER BY d.dept_name ASC, e.salary DESC;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + ORDER BY dept+salary",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Complex SELECT expressions =====================
        { auto r=exec("SELECT name, salary, salary/2 AS half_salary FROM employees WHERE salary > 6000;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"SELECT with expression alias",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name, age, salary, age*100+salary AS score FROM employees LIMIT 5;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==5; appendStep(steps,seq++,"SELECT with calculated score",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT id, name, dept_id AS department FROM employees WHERE dept_id = 1;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"SELECT column alias department",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT DISTINCT dept_id FROM employees ORDER BY dept_id;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"SELECT DISTINCT dept_id ORDER BY",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name, salary FROM employees WHERE salary > 5000 AND salary < 10000 AND dept_id IN (1, 2, 3) ORDER BY salary DESC LIMIT 4;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==4; appendStep(steps,seq++,"Complex WHERE IN ORDER LIMIT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(DISTINCT dept_id) FROM employees;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"COUNT DISTINCT dept_id",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE (salary >= 6000 AND salary <= 9000) OR age = 22;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==6; appendStep(steps,seq++,"WHERE (range) OR equality",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE id BETWEEN 2 AND 6 AND dept_id <> 3 AND name LIKE '%a%';",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"WHERE BETWEEN AND <> AND LIKE",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name, p.proj_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id INNER JOIN projects p ON e.dept_id = p.dept_id WHERE p.cost > 50000 ORDER BY p.cost DESC LIMIT 5;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN + WHERE + ORDER + LIMIT",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, COUNT(*) AS cnt, AVG(salary) AS avg_sal FROM employees GROUP BY dept_id HAVING COUNT(*) >= 2 AND AVG(salary) > 6000 ORDER BY avg_sal DESC;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"GROUP BY + HAVING 2 conds + ORDER",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Edge cases with complex SQL =====================
        { auto r=exec("SELECT * FROM employees WHERE id = 999;",DB,ver); bool p=r.getSuccess() && r.getRows().empty(); appendStep(steps,seq++,"Complex WHERE no match empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE salary > 100000;",DB,ver); bool p=r.getSuccess() && r.getRows().empty(); appendStep(steps,seq++,"High salary no match empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT * FROM employees WHERE (dept_id = 1 AND salary < 5000) OR (dept_id = 2 AND salary < 5000);",DB,ver); bool p=r.getSuccess() && r.getRows().empty(); appendStep(steps,seq++,"Complex OR no match empty",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT COUNT(*) FROM employees WHERE 1 = 0;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Impossible condition COUNT",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT name FROM employees WHERE id IN (1, 2, 3, 4, 5, 6, 7, 8) AND dept_id IN (1, 2, 3) AND salary > 0;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==8; appendStep(steps,seq++,"Maximum IN list all match",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name, d.dept_name FROM employees e INNER JOIN departments d ON e.dept_id = d.id WHERE d.budget = (SELECT MAX(budget) FROM departments);",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"JOIN + scalar MAX subquery",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT dept_id, COUNT(*), SUM(salary), AVG(salary), MIN(age), MAX(age) FROM employees GROUP BY dept_id ORDER BY SUM(salary) DESC;",DB,ver); bool p=r.getSuccess() && r.getRows().size()==3; appendStep(steps,seq++,"5 aggregates + ORDER BY SUM",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT a.emp_id, SUM(a.hours) FROM assignments a INNER JOIN employees e ON a.emp_id = e.id INNER JOIN departments d ON e.dept_id = d.id WHERE d.budget > 200000 GROUP BY a.emp_id HAVING SUM(a.hours) > 30;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"3-table JOIN + WHERE + GROUP + HAVING",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT e.name FROM employees e WHERE e.id IN (SELECT a.emp_id FROM assignments a WHERE a.proj_id IN (SELECT p.id FROM projects p WHERE p.cost > 60000));",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Nested subquery 3 levels deep",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("SELECT d.dept_name, (SELECT COUNT(*) FROM employees e WHERE e.dept_id = d.id) AS emp_count FROM departments d;",DB,ver); bool p=r.getSuccess(); appendStep(steps,seq++,"Scalar subquery in SELECT list",p,"rows="+std::to_string(r.getRows().size())); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        // ===================== Cleanup =====================
        { auto r=exec("DROP TABLE assignments;",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE assignments",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE projects;",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE projects",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE employees;",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE employees",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP TABLE departments;",DB,ver); syncVer(ver,r); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP TABLE departments",p); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }
        { auto r=exec("DROP DATABASE "+DB+";"); bool p=r.getSuccess(); appendStep(steps,seq++,"DROP DATABASE cleanup",p,r.getMessage()); std::cout<<"  "<<(p?"[PASS]":"[FAIL]")<<" CQ-"<<(seq-1)<<"\n"; }

        sock.shutdown(asio::ip::tcp::socket::shutdown_both); sock.close();
        ok=std::all_of(steps.begin(),steps.end(),[](auto &s){return s.passed;});
    } catch(std::exception &e){fatal=e.what();ok=false;}
    if(recv)recv->stop();

    double pct=gTotal>0?100.0*gPassed/gTotal:0;
    std::cout<<"\nResults: "<<gPassed<<"/"<<gTotal<<" ("<<pct<<"%)\nOverall: "<<(ok?"PASS":"FAIL")<<"\n";
    if(!ok){std::cout<<"Failed:\n"; for(auto&s:steps)if(!s.passed)std::cout<<"  #"<<s.id<<" "<<s.name<<" - "<<s.detail<<"\n";}
    writeReportLog("ComplexQueryTest",steps);
    return ok?0:1;
}
