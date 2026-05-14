#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

#ifndef SERVER_PROJECT_ROOT
#error SERVER_PROJECT_ROOT is not defined.
#endif

namespace {

constexpr unsigned short TEST_PORT = 19087;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);

struct TestStepResult {
    std::string name;
    bool passed;
    std::string detail;
};

std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength) {
    return {
        static_cast<unsigned char>((messageLength >> 24U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 16U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 8U) & 0xFFU),
        static_cast<unsigned char>(messageLength & 0xFFU)};
}

std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &lengthHeader) {
    return (static_cast<std::uint32_t>(lengthHeader[0]) << 24U)
           | (static_cast<std::uint32_t>(lengthHeader[1]) << 16U)
           | (static_cast<std::uint32_t>(lengthHeader[2]) << 8U)
           | static_cast<std::uint32_t>(lengthHeader[3]);
}

std::filesystem::path getProjectRoot() {
    return std::filesystem::path(SERVER_PROJECT_ROOT);
}

std::filesystem::path getReportPath() {
    return getProjectRoot() / "src" / "test" / "BinderPlanJoinAggTestReport.md";
}

void prepareStorageWorkingDirectory() {
    const std::filesystem::path storageDir = getProjectRoot() / "src" / "storage";
    std::filesystem::create_directories(storageDir / "data");
    std::filesystem::current_path(storageDir);
}

void cleanupDatabaseArtifacts(const std::string &dbName) {
    const std::filesystem::path dbRoot("data");
    const std::filesystem::path dbDir = dbRoot / dbName;
    if (std::filesystem::exists(dbDir)) {
        std::filesystem::remove_all(dbDir);
    }
}

void connectWithRetry(asio::ip::tcp::socket *socket, unsigned short port) {
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
    std::exception_ptr lastException;
    for (int i = 0; i < CONNECT_RETRY_COUNT; ++i) {
        try {
            socket->connect(endpoint);
            return;
        } catch (...) {
            lastException = std::current_exception();
            std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
        }
    }
    if (lastException != nullptr) std::rethrow_exception(lastException);
    throw std::runtime_error("Connect retry failed.");
}

void sendRawMessage(asio::ip::tcp::socket *socket, const std::string &message) {
    const auto header = buildLengthHeader(static_cast<std::uint32_t>(message.size()));
    asio::write(*socket, asio::buffer(header));
    asio::write(*socket, asio::buffer(message));
}

std::string receiveRawMessage(asio::ip::tcp::socket *socket) {
    std::array<unsigned char, 4> lengthHeader {};
    asio::read(*socket, asio::buffer(lengthHeader));
    const std::uint32_t len = parseLengthHeader(lengthHeader);
    std::string message(len, '\0');
    asio::read(*socket, asio::buffer(message.data(), message.size()));
    return message;
}

NetworkTransferData sendRequestAndReceive(asio::ip::tcp::socket *socket, const NetworkTransferData &request) {
    sendRawMessage(socket, request.toJson());
    return NetworkTransferData::fromJson(receiveRawMessage(socket));
}

void appendStep(std::vector<TestStepResult> *results, const std::string &name, bool passed, const std::string &detail) {
    if (results) results->push_back({name, passed, detail});
}

std::string joinVec(const std::vector<std::string> &v, const std::string &sep) {
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) r += sep;
        r += v[i];
    }
    return r;
}

void writeReport(const std::vector<TestStepResult> &stepResults, bool overall, const std::string &fatal) {
    auto path = getReportPath();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.good()) return;
    ofs << "# Binder-Plan Join & Aggregation Test Report\n\n";
    ofs << "- Overall Result: " << (overall ? "PASS" : "FAIL") << "\n";
    ofs << "- Test Scope: Binder semantic binding + Plan execution for JOIN and Aggregation queries\n\n";
    ofs << "## Step Results\n\n| Step | Result | Detail |\n|---|---|---|\n";
    for (const auto &s : stepResults) {
        ofs << "| " << s.name << " | " << (s.passed ? "PASS" : "FAIL") << " | " << s.detail << " |\n";
    }
    if (!fatal.empty()) {
        ofs << "\n## Fatal Error\n\n- " << fatal << "\n";
    }
}

} // namespace

int main() {
    const std::string userId = "BinderPlanTester";
    const std::string dbName = "BinderPlanTestDb";
    const std::string leftTable = "employees";
    const std::string rightTable = "departments";

    std::vector<TestStepResult> stepResults;
    std::string fatalMessage;
    bool overallPassed = false;

    prepareStorageWorkingDirectory();
    cleanupDatabaseArtifacts(dbName);

    Core core;
    std::unique_ptr<NetReceiver> testReceiver;

    try {
        testReceiver = std::make_unique<NetReceiver>(&core, TEST_PORT);
        testReceiver->start();

        asio::io_context clientCtx;
        asio::ip::tcp::socket clientSocket(clientCtx);
        connectWithRetry(&clientSocket, TEST_PORT);

        // ===== Setup: CREATE DATABASE, CREATE TABLES, INSERT DATA =====

        // Step 1: CREATE DATABASE
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setSql("CREATE DATABASE " + dbName + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess();
            appendStep(&stepResults, "CREATE DATABASE", ok, resp.getMessage());
        }

        // Step 2: USE DATABASE
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("USE DATABASE " + dbName + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess();
            appendStep(&stepResults, "USE DATABASE", ok, resp.getMessage());
        }

        // Step 3: CREATE TABLE employees (id INT, name CHAR(20), dept_id INT, salary INT)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("CREATE TABLE " + leftTable + " (id INT, name VARCHAR(20), dept_id INT, salary INT);");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess();
            appendStep(&stepResults, "CREATE TABLE employees", ok, resp.getMessage());
        }

        // Step 4: CREATE TABLE departments (id INT, dept_name VARCHAR(30))
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("CREATE TABLE " + rightTable + " (id INT, dept_name VARCHAR(30));");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess();
            appendStep(&stepResults, "CREATE TABLE departments", ok, resp.getMessage());
        }

        // Step 5: INSERT data into employees
        {
            auto execSql = [&](const std::string &sql) {
                NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
                req.setDbName(dbName);
                req.setSql(sql);
                return sendRequestAndReceive(&clientSocket, req);
            };
            execSql("INSERT INTO " + leftTable + " VALUES (1, 'Alice', 1, 5000);");
            execSql("INSERT INTO " + leftTable + " VALUES (2, 'Bob', 2, 6000);");
            execSql("INSERT INTO " + leftTable + " VALUES (3, 'Charlie', 1, 7000);");
            execSql("INSERT INTO " + leftTable + " VALUES (4, 'Diana', 3, 8000);");

            // Verify
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT * FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess() && resp.getRows().size() >= 4;
            appendStep(&stepResults, "INSERT employees", ok,
                       "rowCount=" + std::to_string(resp.getRows().size()));
        }

        // Step 6: INSERT data into departments
        {
            auto execSql = [&](const std::string &sql) {
                NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
                req.setDbName(dbName);
                req.setSql(sql);
                return sendRequestAndReceive(&clientSocket, req);
            };
            execSql("INSERT INTO " + rightTable + " VALUES (1, 'Engineering');");
            execSql("INSERT INTO " + rightTable + " VALUES (2, 'Marketing');");
            execSql("INSERT INTO " + rightTable + " VALUES (4, 'Finance');");

            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT * FROM " + rightTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess() && resp.getRows().size() >= 3;
            appendStep(&stepResults, "INSERT departments", ok,
                       "rowCount=" + std::to_string(resp.getRows().size()));
        }

        // ===== JOIN Tests =====

        // Step 7: INNER JOIN
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT * FROM " + leftTable
                       + " INNER JOIN " + rightTable
                       + " ON " + leftTable + ".dept_id = " + rightTable + ".id;");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // 员工表有4行，部门表有3行(1,2,4)，部门3不在其中
            // INNER JOIN: dept_id匹配的有 1→1(Alice,Charlie), 2→2(Bob), 3→无匹配(Diana排除)
            // 结果应为 3 行
            bool ok = resp.getSuccess() && resp.getRows().size() == 3;
            appendStep(&stepResults, "INNER JOIN", ok,
                       "success=" + std::string(resp.getSuccess() ? "true" : "false")
                       + ", rowCount=" + std::to_string(resp.getRows().size())
                       + ", cols=" + joinVec(resp.getColumns(), ",")
                       + ", msg=" + resp.getMessage());
        }

        // Step 8: LEFT JOIN
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT * FROM " + leftTable
                       + " LEFT JOIN " + rightTable
                       + " ON " + leftTable + ".dept_id = " + rightTable + ".id;");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // LEFT JOIN: Diana(dept_id=3) 保留但右侧为空
            // 结果应为 4 行
            bool ok = resp.getSuccess() && resp.getRows().size() == 4;
            appendStep(&stepResults, "LEFT JOIN", ok,
                       "success=" + std::string(resp.getSuccess() ? "true" : "false")
                       + ", rowCount=" + std::to_string(resp.getRows().size())
                       + ", msg=" + resp.getMessage());
        }

        // Step 9: RIGHT JOIN
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT * FROM " + leftTable
                       + " RIGHT JOIN " + rightTable
                       + " ON " + leftTable + ".dept_id = " + rightTable + ".id;");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // RIGHT JOIN: 右侧有部门1,2,4，左侧dept_id有1,1,2,3
            // 匹配: 1→1(两次), 2→2(一次), 4→无匹配(右侧保留)
            // 结果应为 4 行
            bool ok = resp.getSuccess() && resp.getRows().size() == 4;
            appendStep(&stepResults, "RIGHT JOIN", ok,
                       "success=" + std::string(resp.getSuccess() ? "true" : "false")
                       + ", rowCount=" + std::to_string(resp.getRows().size())
                       + ", msg=" + resp.getMessage());
        }

        // ===== Aggregation Tests =====

        // Step 10: COUNT(*)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT COUNT(*) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "4";
            appendStep(&stepResults, "COUNT(*)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0])
                       + ", cols=" + joinVec(resp.getColumns(), ",")
                       + ", msg=" + resp.getMessage());
        }

        // Step 11: COUNT(column)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT COUNT(id) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "4";
            appendStep(&stepResults, "COUNT(id)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0]));
        }

        // Step 12: SUM(salary)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT SUM(salary) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // 5000+6000+7000+8000=26000
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "26000.000000";
            appendStep(&stepResults, "SUM(salary)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0]));
        }

        // Step 13: AVG(salary)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT AVG(salary) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // 26000/4=6500
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "6500.000000";
            appendStep(&stepResults, "AVG(salary)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0]));
        }

        // Step 14: MIN(salary)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT MIN(salary) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "5000";
            appendStep(&stepResults, "MIN(salary)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0]));
        }

        // Step 15: MAX(salary)
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT MAX(salary) FROM " + leftTable + ";");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            bool ok = resp.getSuccess()
                      && !resp.getRows().empty()
                      && resp.getRows()[0][0] == "8000";
            appendStep(&stepResults, "MAX(salary)", ok,
                       "value=" + (resp.getRows().empty() ? "empty" : resp.getRows()[0][0]));
        }

        // Step 16: GROUP BY + COUNT
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT dept_id, COUNT(*) FROM " + leftTable + " GROUP BY dept_id;");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // dept_id=1: 2, dept_id=2: 1, dept_id=3: 1
            bool ok = resp.getSuccess() && resp.getRows().size() == 3;
            appendStep(&stepResults, "GROUP BY + COUNT", ok,
                       "rowCount=" + std::to_string(resp.getRows().size())
                       + ", cols=" + joinVec(resp.getColumns(), ","));
        }

        // Step 17: COMPLEX — JOIN + AGGREGATION
        {
            NetworkTransferData req(NetworkTransferData::SQL_EXEC_REQUEST, userId);
            req.setDbName(dbName);
            req.setSql("SELECT dept_name, COUNT(*) FROM " + leftTable
                       + " INNER JOIN " + rightTable
                       + " ON " + leftTable + ".dept_id = " + rightTable + ".id"
                       + " GROUP BY dept_name;");
            auto resp = sendRequestAndReceive(&clientSocket, req);
            // Engineering:2 (Alice,Charlie), Marketing:1 (Bob), Finance:0 (no match)
            // INNER JOIN 排除了Finance
            bool ok = resp.getSuccess();
            appendStep(&stepResults, "JOIN + GROUP BY COUNT", ok,
                       "rowCount=" + std::to_string(resp.getRows().size())
                       + ", cols=" + joinVec(resp.getColumns(), ",")
                       + ", msg=" + resp.getMessage());
        }

        clientSocket.shutdown(asio::ip::tcp::socket::shutdown_both);
        clientSocket.close();

        overallPassed = std::all_of(stepResults.begin(), stepResults.end(),
                                    [](const TestStepResult &s) { return s.passed; });
    } catch (const std::exception &e) {
        fatalMessage = e.what();
        overallPassed = false;
    }

    if (testReceiver) testReceiver->stop();
    cleanupDatabaseArtifacts(dbName);
    writeReport(stepResults, overallPassed, fatalMessage);

    if (!fatalMessage.empty()) {
        std::cerr << "Test failed: " << fatalMessage << std::endl;
    }
    for (const auto &s : stepResults) {
        std::cout << s.name << ": " << (s.passed ? "PASS" : "FAIL") << " - " << s.detail << std::endl;
    }
    return overallPassed ? 0 : 1;
}
