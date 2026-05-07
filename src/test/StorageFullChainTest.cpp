#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
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
#include "models/storage/FieldBlock.h"
#include "network/NetReceiver.h"

#ifndef SERVER_PROJECT_ROOT
#error SERVER_PROJECT_ROOT is not defined.
#endif

namespace {

constexpr unsigned short TEST_PORT = 19086;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);

struct TestStepResult {
    std::string name;
    bool passed;
    std::string detail;
};

std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength)
{
    return {
        static_cast<unsigned char>((messageLength >> 24U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 16U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 8U) & 0xFFU),
        static_cast<unsigned char>(messageLength & 0xFFU)};
}

std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &lengthHeader)
{
    return (static_cast<std::uint32_t>(lengthHeader[0]) << 24U)
           | (static_cast<std::uint32_t>(lengthHeader[1]) << 16U)
           | (static_cast<std::uint32_t>(lengthHeader[2]) << 8U)
           | static_cast<std::uint32_t>(lengthHeader[3]);
}

std::filesystem::path getProjectRoot()
{
    return std::filesystem::path(SERVER_PROJECT_ROOT);
}

std::filesystem::path getStorageDir()
{
    return getProjectRoot() / "src" / "storage";
}

std::filesystem::path getReportPath()
{
    return getProjectRoot() / "src" / "test" / "StorageFullChainTestReport.md";
}

void appendStepResult(std::vector<TestStepResult> *stepResults,
                      const std::string &name,
                      bool passed,
                      const std::string &detail)
{
    if (stepResults == nullptr) {
        return;
    }
    stepResults->push_back({name, passed, detail});
}

void ensure(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void prepareStorageWorkingDirectory()
{
    const std::filesystem::path storageDir = getStorageDir();
    std::filesystem::create_directories(storageDir / "data");
    std::filesystem::current_path(storageDir);
}

void cleanupDatabaseArtifacts(const std::string &dbName)
{
    const std::filesystem::path dbRoot("data");
    const std::filesystem::path dbDir = dbRoot / dbName;
    const std::filesystem::path catalogFile = dbRoot / "database.db";

    if (std::filesystem::exists(dbDir)) {
        std::filesystem::remove_all(dbDir);
    }

    if (std::filesystem::exists(catalogFile)) {
        std::ifstream ifs(catalogFile);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        ifs.close();

        std::ofstream ofs(catalogFile, std::ios::trunc);
        for (const auto &entry : lines) {
            if (entry == dbName) {
                continue;
            }
            ofs << entry << '\n';
        }
    }
}

void sendRawMessage(asio::ip::tcp::socket *socket, const std::string &message)
{
    ensure(socket != nullptr, "Client socket is null.");
    const std::array<unsigned char, 4> lengthHeader = buildLengthHeader(static_cast<std::uint32_t>(message.size()));
    asio::write(*socket, asio::buffer(lengthHeader));
    asio::write(*socket, asio::buffer(message));
}

std::string receiveRawMessage(asio::ip::tcp::socket *socket)
{
    ensure(socket != nullptr, "Client socket is null.");
    std::array<unsigned char, 4> lengthHeader {};
    asio::read(*socket, asio::buffer(lengthHeader));
    const std::uint32_t messageLength = parseLengthHeader(lengthHeader);
    std::string message(messageLength, '\0');
    asio::read(*socket, asio::buffer(message.data(), message.size()));
    return message;
}

NetworkTransferData sendRequestAndReceive(asio::ip::tcp::socket *socket, const NetworkTransferData &requestData)
{
    sendRawMessage(socket, requestData.toJson());
    return NetworkTransferData::fromJson(receiveRawMessage(socket));
}

void connectWithRetry(asio::ip::tcp::socket *socket, unsigned short port)
{
    ensure(socket != nullptr, "Client socket is null.");
    asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
    std::exception_ptr lastException;

    for (int i = 0; i < CONNECT_RETRY_COUNT; ++i) {
        std::error_code errorCode;
        socket->connect(endpoint, errorCode);
        if (!errorCode) {
            return;
        }
        try {
            throw std::runtime_error(errorCode.message());
        } catch (...) {
            lastException = std::current_exception();
        }
        std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
    }

    if (lastException != nullptr) {
        std::rethrow_exception(lastException);
    }
    throw std::runtime_error("Connect retry failed without exception.");
}

std::string joinStrings(const std::vector<std::string> &values, const std::string &separator)
{
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
}

void writeReport(const std::vector<TestStepResult> &stepResults,
                 bool overallPassed,
                 const std::string &fatalMessage)
{
    const std::filesystem::path reportPath = getReportPath();
    std::filesystem::create_directories(reportPath.parent_path());
    std::ofstream reportFile(reportPath, std::ios::trunc);
    if (!reportFile.good()) {
        return;
    }

    reportFile << "# Storage Full Chain Test Report\n\n";
    reportFile << "- Overall Result: " << (overallPassed ? "PASS" : "FAIL") << "\n";
    reportFile << "- Test Scope: CREATE DATABASE / CREATE TABLE / INSERT full chain\n";
    reportFile << "- Report File: `" << reportPath.string() << "`\n\n";

    reportFile << "## Step Results\n\n";
    reportFile << "| Step | Result | Detail |\n";
    reportFile << "|---|---|---|\n";
    for (const TestStepResult &stepResult : stepResults) {
        reportFile << "| " << stepResult.name
                   << " | " << (stepResult.passed ? "PASS" : "FAIL")
                   << " | " << stepResult.detail << " |\n";
    }

    if (!fatalMessage.empty()) {
        reportFile << "\n## Fatal Error\n\n";
        reportFile << "```\n" << fatalMessage << "\n```\n";
    }

    std::cout << "[REPORT] " << reportPath.string()
              << " Overall: " << (overallPassed ? "PASS" : "FAIL") << std::endl;
    for (const TestStepResult &stepResult : stepResults) {
        std::cout << "  " << stepResult.name << ": "
                  << (stepResult.passed ? "PASS" : "FAIL") << std::endl;
    }
}

} // namespace

int main()
{
    const std::string testDbName = "test_fullchain_db";
    const std::string testTableName = "users";
    const std::string testUserId = "test_user";
    bool overallPassed = false;
    std::string fatalMessage;
    std::vector<TestStepResult> stepResults;

    prepareStorageWorkingDirectory();
    cleanupDatabaseArtifacts(testDbName);

    Core core;
    std::unique_ptr<NetReceiver> testReceiver;

    try {
        testReceiver = std::make_unique<NetReceiver>(&core, TEST_PORT);
        testReceiver->start();

        asio::io_context clientContext;
        asio::ip::tcp::socket clientSocket(clientContext);
        connectWithRetry(&clientSocket, TEST_PORT);

        // ========== CREATE DATABASE ==========

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setSql("CREATE DATABASE " + testDbName + ";");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess()
                                && response.getDbName() == testDbName
                                && std::filesystem::exists(std::filesystem::path("data") / testDbName);
            appendStepResult(&stepResults, "CREATE DATABASE (basic)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setSql("CREATE DATABASE " + testDbName + ";");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && !response.getSuccess();
            appendStepResult(&stepResults, "CREATE DATABASE (duplicate)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setSql("USE DATABASE " + testDbName + ";");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);
            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess();
            appendStepResult(&stepResults, "USE DATABASE",
                             passed,
                             "dbName=" + response.getDbName()
                                 + ", message=" + response.getMessage());
        }

        // ========== CREATE TABLE ==========

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("CREATE TABLE " + testTableName
                           + " (id INT PRIMARY KEY, name CHAR(20) NOT NULL, age INT DEFAULT 0);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const auto tableDir = std::filesystem::path("data") / testDbName;
            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess()
                                && std::filesystem::exists(tableDir / (testTableName + ".tdf"))
                                && std::filesystem::exists(tableDir / (testTableName + ".trd"));
            appendStepResult(&stepResults, "CREATE TABLE (with PK + NOT NULL + DEFAULT)",
                             passed,
                             "message=" + response.getMessage()
                                 + ", tdfExists=" + (std::filesystem::exists(tableDir / (testTableName + ".tdf")) ? "true" : "false")
                                 + ", trdExists=" + (std::filesystem::exists(tableDir / (testTableName + ".trd")) ? "true" : "false"));
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("CREATE TABLE " + testTableName + " (col INT);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "CREATE TABLE (duplicate table)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            asio::ip::tcp::socket freshSocket(clientContext);
            connectWithRetry(&freshSocket, TEST_PORT);

            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setSql("CREATE TABLE no_db_table (col INT);");
            const NetworkTransferData response = sendRequestAndReceive(&freshSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "CREATE TABLE (no database)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());

            freshSocket.shutdown(asio::ip::tcp::socket::shutdown_both);
            freshSocket.close();
        }

        // ========== INSERT ==========

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO " + testTableName + " VALUES (1, 'Alice', 25);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess()
                                && response.getAffectedRows() == 1;
            appendStepResult(&stepResults, "INSERT (full columns)",
                             passed,
                             "message=" + response.getMessage()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", affectedRows=" + std::to_string(response.getAffectedRows()));
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO " + testTableName + " (id, name) VALUES (2, 'Bob');");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess()
                                && response.getAffectedRows() == 1;
            appendStepResult(&stepResults, "INSERT (partial columns)",
                             passed,
                             "message=" + response.getMessage()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", affectedRows=" + std::to_string(response.getAffectedRows()));
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO " + testTableName + " VALUES (1, 'Duplicate', 30);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "INSERT (duplicate primary key)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO " + testTableName + " VALUES (3);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "INSERT (column count mismatch)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO nonexistent_table VALUES (1, 'a', 1);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "INSERT (non-existent table)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setSql("INSERT INTO no_db_users VALUES (1);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = !response.getSuccess();
            appendStepResult(&stepResults, "INSERT (no database selected)",
                             passed,
                             "type=" + response.getType()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", message=" + response.getMessage());
        }

        {
            NetworkTransferData request(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
            request.setDbName(testDbName);
            request.setSql("INSERT INTO " + testTableName
                           + " (id, name, age) VALUES (3, 'Carol', 28);");
            const NetworkTransferData response = sendRequestAndReceive(&clientSocket, request);

            const bool passed = response.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                                && response.getSuccess()
                                && response.getAffectedRows() == 1;
            appendStepResult(&stepResults, "INSERT (all columns specified)",
                             passed,
                             "message=" + response.getMessage()
                                 + ", success=" + (response.getSuccess() ? "true" : "false")
                                 + ", affectedRows=" + std::to_string(response.getAffectedRows()));
        }

        // ========== TEARDOWN ==========

        clientSocket.shutdown(asio::ip::tcp::socket::shutdown_both);
        clientSocket.close();

        overallPassed = std::all_of(stepResults.begin(),
                                     stepResults.end(),
                                     [](const TestStepResult &stepResult) {
                                         return stepResult.passed;
                                     });
    } catch (const std::exception &exception) {
        fatalMessage = exception.what();
        overallPassed = false;
    }

    if (testReceiver != nullptr) {
        testReceiver->stop();
    }
    cleanupDatabaseArtifacts(testDbName);
    writeReport(stepResults, overallPassed, fatalMessage);

    return overallPassed ? 0 : 1;
}
