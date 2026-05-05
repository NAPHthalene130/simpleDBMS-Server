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
#include "network/NetReceiver.h"

#ifndef SERVER_PROJECT_ROOT
#error SERVER_PROJECT_ROOT is not defined.
#endif

namespace {

constexpr unsigned short TEST_PORT = 19086;
constexpr int CONNECT_RETRY_COUNT = 40;
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100);
constexpr const char *kCatalogBlockSeparator = "---DB_BLOCK---";

struct TestStepResult
{
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
    return getProjectRoot() / "src" / "test" / "NetworkSqlFlowTestReport.md";
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
        std::vector<std::vector<std::string>> blocks;
        std::vector<std::string> current;
        std::string line;
        while (std::getline(ifs, line)) {
            if (line == kCatalogBlockSeparator) {
                if (!current.empty()) {
                    blocks.push_back(current);
                }
                current.clear();
                continue;
            }
            if (!line.empty()) {
                current.push_back(line);
            }
        }
        if (!current.empty()) {
            blocks.push_back(current);
        }
        std::ofstream ofs(catalogFile, std::ios::trunc);
        for (const auto &block : blocks) {
            bool removeBlock = false;
            for (const auto &item : block) {
                if (item == "name=" + dbName || item == dbName) {
                    removeBlock = true;
                    break;
                }
            }
            if (removeBlock) {
                continue;
            }
            for (const auto &item : block) {
                ofs << item << '\n';
            }
            ofs << kCatalogBlockSeparator << '\n';
        }
    }
}

void connectWithRetry(asio::ip::tcp::socket *socket, unsigned short port)
{
    ensure(socket != nullptr, "Client socket is null.");

    asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), port);
    std::exception_ptr lastException;
    for (int retryIndex = 0; retryIndex < CONNECT_RETRY_COUNT; ++retryIndex) {
        try {
            socket->connect(endpoint);
            return;
        } catch (...) {
            lastException = std::current_exception();
            std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
        }
    }

    if (lastException != nullptr) {
        std::rethrow_exception(lastException);
    }

    throw std::runtime_error("Connect retry failed without exception.");
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

bool containsNameRow(const std::vector<std::vector<std::string>> &rows, const std::string &expectedName)
{
    return std::any_of(rows.begin(),
                       rows.end(),
                       [&expectedName](const std::vector<std::string> &row) {
                           return !row.empty() && row[0] == expectedName;
                       });
}

bool containsTableNameInTb(const std::filesystem::path &tbPath, const std::string &tableName)
{
    std::ifstream ifs(tbPath);
    if (!ifs.good()) {
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line == "name=" + tableName || line == "table=" + tableName || line == tableName) {
            return true;
        }
    }
    return false;
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

    reportFile << "# Network SQL Flow Test Report\n\n";
    reportFile << "- Overall Result: " << (overallPassed ? "PASS" : "FAIL") << "\n";
    reportFile << "- Test Scope: NetworkTransferData SQL request/response full flow in server network layer\n";
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
        reportFile << "\n## Fatal Message\n\n";
        reportFile << "- " << fatalMessage << "\n";
    }
}

} // namespace

int main()
{
    const std::string testUserId = "NetworkSqlFlowTester";
    const std::string dbName = "NetworkSqlFlowTestDb";
    const std::string tableName = "NetworkSqlFlowTestTb";

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

        asio::io_context clientContext;
        asio::ip::tcp::socket clientSocket(clientContext);
        connectWithRetry(&clientSocket, TEST_PORT);

        NetworkTransferData createDbRequest(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
        createDbRequest.setSql("CREATE DATABASE " + dbName + ";");
        const NetworkTransferData createDbResponse = sendRequestAndReceive(&clientSocket, createDbRequest);
        const bool createDbPassed =
            createDbResponse.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
            && createDbResponse.getSuccess()
            && createDbResponse.getDbName() == dbName
            && std::filesystem::exists(std::filesystem::path("data") / dbName)
            && std::filesystem::exists(std::filesystem::path("data") / "database.db");
        appendStepResult(&stepResults,
                         "CREATE DATABASE",
                         createDbPassed,
                         "type=" + createDbResponse.getType() + ", message=" + createDbResponse.getMessage());

        NetworkTransferData useDbRequest(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
        useDbRequest.setDbName(dbName);
        useDbRequest.setSql("USE DATABASE " + dbName + ";");
        const NetworkTransferData useDbResponse = sendRequestAndReceive(&clientSocket, useDbRequest);
        const bool useDbPassed =
            useDbResponse.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
            && useDbResponse.getSuccess()
            && useDbResponse.getDbName() == dbName;
        appendStepResult(&stepResults,
                         "USE DATABASE",
                         useDbPassed,
                         "type=" + useDbResponse.getType() + ", message=" + useDbResponse.getMessage());

        NetworkTransferData showDatabaseRequest(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
        showDatabaseRequest.setSql("SHOW DATABASE " + dbName + ";");
        const NetworkTransferData showDatabaseResponse = sendRequestAndReceive(&clientSocket, showDatabaseRequest);
        const bool showDatabasePassed =
            showDatabaseResponse.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
            && showDatabaseResponse.getSuccess()
            && showDatabaseResponse.getColumns() == std::vector<std::string> {"name"}
            && containsNameRow(showDatabaseResponse.getRows(), dbName);
        appendStepResult(&stepResults,
                         "SHOW DATABASE",
                         showDatabasePassed,
                         "type=" + showDatabaseResponse.getType()
                             + ", success=" + (showDatabaseResponse.getSuccess() ? std::string("true") : std::string("false"))
                             + ", message=" + showDatabaseResponse.getMessage()
                             + ", columns=" + joinStrings(showDatabaseResponse.getColumns(), ",")
                             + ", rowCount=" + std::to_string(showDatabaseResponse.getRows().size()));

        NetworkTransferData createTableRequest(NetworkTransferData::SQL_EXEC_REQUEST, testUserId);
        createTableRequest.setDbName(dbName);
        createTableRequest.setSql("CREATE TABLE " + tableName + " (id INT, name CHAR(10));");
        const NetworkTransferData createTableResponse = sendRequestAndReceive(&clientSocket, createTableRequest);
        const std::filesystem::path tableDir = std::filesystem::path("data") / dbName;
        const std::filesystem::path tbFile = tableDir / (dbName + ".tb");
        const bool createTablePassed =
            createTableResponse.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
            && createTableResponse.getSuccess()
            && std::filesystem::exists(tableDir / (tableName + ".tdf"))
            && std::filesystem::exists(tableDir / (tableName + ".trd"))
            && std::filesystem::exists(tableDir / (tableName + ".tic"))
            && std::filesystem::exists(tableDir / (tableName + ".tid"))
            && containsTableNameInTb(tbFile, tableName);
        appendStepResult(&stepResults,
                         "CREATE TABLE",
                         createTablePassed,
                         "type=" + createTableResponse.getType()
                             + ", message=" + createTableResponse.getMessage()
                             + ", tdfExists=" + (std::filesystem::exists(tableDir / (tableName + ".tdf")) ? "true" : "false"));

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
    cleanupDatabaseArtifacts(dbName);
    writeReport(stepResults, overallPassed, fatalMessage);

    if (!fatalMessage.empty()) {
        std::cerr << "NetworkSqlFlowTest failed: " << fatalMessage << std::endl;
    }

    for (const TestStepResult &stepResult : stepResults) {
        std::cout << stepResult.name << ": "
                  << (stepResult.passed ? "PASS" : "FAIL")
                  << " - " << stepResult.detail << std::endl;
    }

    return overallPassed ? 0 : 1;
}
