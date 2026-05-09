/**
 * @file DbLogFullTest.cpp
 * @brief 数据库日志模块完整测试
 * @details 模拟 NetReceiver 网络传入，覆盖 CRUD 全流程 SQL 操作，
 *          同时验证 DbLogManager 的日志记录与 dbRecover 恢复功能。
 * @author NAPH130
 */

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
#include "dbLog/DbLogManager.h"
#include "models/dbLog/LogBlock.h"
#include "models/network/NetworkTransferData.h"
#include "network/NetReceiver.h"

// ──────────────────────────────────────────────
// 测试常量
// ──────────────────────────────────────────────

namespace {

constexpr unsigned short TEST_PORT = 19087;               ///< 测试监听端口（独立避免冲突）
constexpr int CONNECT_RETRY_COUNT = 40;                 ///< 连接重试次数
constexpr auto CONNECT_RETRY_INTERVAL = std::chrono::milliseconds(100); ///< 重试间隔

const std::string TEST_DB = "test_dblog_db";             ///< 测试数据库名
const std::string TEST_TBL = "employees";                ///< 测试表名
const std::string TEST_UID = "DbLogTester";              ///< 测试用户标识

// ──────────────────────────────────────────────
// 测试步骤结果
// ──────────────────────────────────────────────

struct TestStepResult {
    std::string name;
    bool passed;
    std::string detail;
};

// ──────────────────────────────────────────────
// 网络层辅助函数（模拟 NetReceiver 协议）
// ──────────────────────────────────────────────

std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength)
{
    return {
        static_cast<unsigned char>((messageLength >> 24U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 16U) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 8U) & 0xFFU),
        static_cast<unsigned char>(messageLength & 0xFFU)};
}

std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &h)
{
    return (static_cast<std::uint32_t>(h[0]) << 24U)
           | (static_cast<std::uint32_t>(h[1]) << 16U)
           | (static_cast<std::uint32_t>(h[2]) << 8U)
           | static_cast<std::uint32_t>(h[3]);
}

void sendRawMessage(asio::ip::tcp::socket *sock, const std::string &msg)
{
    const auto header = buildLengthHeader(static_cast<std::uint32_t>(msg.size()));
    asio::write(*sock, asio::buffer(header));
    asio::write(*sock, asio::buffer(msg));
}

std::string receiveRawMessage(asio::ip::tcp::socket *sock)
{
    std::array<unsigned char, 4> header{};
    asio::read(*sock, asio::buffer(header));
    const std::uint32_t len = parseLengthHeader(header);
    std::string msg(len, '\0');
    asio::read(*sock, asio::buffer(msg.data(), msg.size()));
    return msg;
}

NetworkTransferData sendRecv(asio::ip::tcp::socket *sock, const NetworkTransferData &req)
{
    sendRawMessage(sock, req.toJson());
    return NetworkTransferData::fromJson(receiveRawMessage(sock));
}

void connectWithRetry(asio::ip::tcp::socket *sock, unsigned short port)
{
    asio::ip::tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);
    for (int i = 0; i < CONNECT_RETRY_COUNT; ++i) {
        std::error_code ec;
        sock->connect(ep, ec);
        if (!ec) {
            return;
        }
        std::this_thread::sleep_for(CONNECT_RETRY_INTERVAL);
    }
    throw std::runtime_error("Failed to connect to server port " + std::to_string(port));
}

// ──────────────────────────────────────────────
// 报告输出
// ──────────────────────────────────────────────

void appendStep(std::vector<TestStepResult> *results,
                const std::string &name,
                bool passed,
                const std::string &detail)
{
    if (results) {
        results->push_back({name, passed, detail});
    }
}

void writeReport(const std::vector<TestStepResult> &steps,
                 bool overall,
                 const std::string &fatal)
{
    const std::filesystem::path reportPath = "DbLogFullTestReport.md";
    std::ofstream ofs(reportPath, std::ios::trunc);
    if (!ofs.good()) {
        return;
    }
    ofs << "# DbLog Full Test Report\n\n"
        << "- **Overall**: " << (overall ? "PASS" : "FAIL") << "\n"
        << "- **Report**: `" << reportPath.string() << "`\n\n"
        << "## Steps\n\n"
        << "| Step | Result | Detail |\n"
        << "|---|---|---|\n";
    for (const auto &s : steps) {
        ofs << "| " << s.name << " | " << (s.passed ? "PASS" : "FAIL")
            << " | " << s.detail << " |\n";
    }
    if (!fatal.empty()) {
        ofs << "\n## Fatal\n```\n" << fatal << "\n```\n";
    }
}

} // namespace

// ──────────────────────────────────────────────
// 主测试入口
// ──────────────────────────────────────────────

int main()
{
    bool overall = false;
    std::string fatalMsg;
    std::vector<TestStepResult> steps;

    Core core;
    std::unique_ptr<NetReceiver> receiver;

    try {
        // ── 启动网络接收器 ──
        receiver = std::make_unique<NetReceiver>(&core, TEST_PORT);
        receiver->start();

        asio::io_context ctx;
        asio::ip::tcp::socket sock(ctx);
        connectWithRetry(&sock, TEST_PORT);

        // ══════════════════════════════════════════════
        // 阶段一：DDL 操作（建库 / 建表）
        // ══════════════════════════════════════════════

        // 步骤 1：CREATE DATABASE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setSql("CREATE DATABASE " + TEST_DB + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                      && resp.getSuccess();
            appendStep(&steps, "1-CREATE DATABASE", ok, resp.getMessage());
        }

        // 步骤 2：USE DATABASE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("USE DATABASE " + TEST_DB + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess();
            appendStep(&steps, "2-USE DATABASE", ok, resp.getMessage());
        }

        // 步骤 3：CREATE TABLE（含主键、NOT NULL、默认值）
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("CREATE TABLE " + TEST_TBL
                       + " (id INT PRIMARY KEY, name CHAR(20) NOT NULL, age INT DEFAULT 18, dept CHAR(30));");
            auto resp = sendRecv(&sock, req);
            auto dir = std::filesystem::path("data") / TEST_DB;
            bool ok = resp.getSuccess()
                      && std::filesystem::exists(dir / (TEST_TBL + ".tdf"))
                      && std::filesystem::exists(dir / (TEST_TBL + ".trd"));
            appendStep(&steps, "3-CREATE TABLE", ok, resp.getMessage());
        }

        // 步骤 4：SHOW DATABASES
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setSql("SHOW DATABASES;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getType() == NetworkTransferData::SQL_QUERY_RESPONSE
                      && resp.getSuccess()
                      && !resp.getRows().empty();
            appendStep(&steps, "4-SHOW DATABASES", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // 步骤 5：SHOW TABLES
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SHOW TABLES;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getType() == NetworkTransferData::SQL_QUERY_RESPONSE
                      && resp.getSuccess()
                      && !resp.getRows().empty();
            appendStep(&steps, "5-SHOW TABLES", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // ══════════════════════════════════════════════
        // 阶段二：DML 操作（插入 / 查询 / 更新 / 删除）
        // ══════════════════════════════════════════════

        // 步骤 6：INSERT 完整列
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("INSERT INTO " + TEST_TBL + " VALUES (1, 'Alice', 25, 'Engineering');");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getType() == NetworkTransferData::SQL_EXEC_RESPONSE
                      && resp.getSuccess();
            appendStep(&steps, "6-INSERT full columns", ok, resp.getMessage());
        }

        // 步骤 7：INSERT 多行
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("INSERT INTO " + TEST_TBL + " VALUES (2, 'Bob', 30, 'Marketing');");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess();
            appendStep(&steps, "7-INSERT row 2", ok, resp.getMessage());
        }

        // 步骤 8：INSERT 部分列（使用默认值）
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("INSERT INTO " + TEST_TBL + " (id, name, dept) VALUES (3, 'Carol', 'Sales');");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess();
            appendStep(&steps, "8-INSERT partial (default age)", ok, resp.getMessage());
        }

        // 步骤 9：INSERT NOT NULL 违规
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("INSERT INTO " + TEST_TBL + " (id, dept) VALUES (4, 'HR');");
            auto resp = sendRecv(&sock, req);
            bool ok = !resp.getSuccess(); // 应该失败
            appendStep(&steps, "9-INSERT NOT NULL violation", ok, resp.getMessage());
        }

        // 步骤 10：INSERT 重复主键
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("INSERT INTO " + TEST_TBL + " VALUES (1, 'Dup', 99, 'Test');");
            auto resp = sendRecv(&sock, req);
            bool ok = !resp.getSuccess(); // 应该失败
            appendStep(&steps, "10-INSERT duplicate PK", ok, resp.getMessage());
        }

        // 步骤 11：SELECT 全表
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SELECT * FROM " + TEST_TBL + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getType() == NetworkTransferData::SQL_QUERY_RESPONSE
                      && resp.getSuccess()
                      && resp.getRows().size() == 3; // 应有 3 行
            appendStep(&steps, "11-SELECT * (3 rows)", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // 步骤 12：SELECT 条件查询
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SELECT * FROM " + TEST_TBL + " WHERE age > 20;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess()
                      && resp.getRows().size() >= 2; // Alice(25), Bob(30)
            appendStep(&steps, "12-SELECT WHERE age > 20", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // 步骤 13：UPDATE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("UPDATE " + TEST_TBL + " SET age = 26 WHERE id = 1;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess() && resp.getAffectedRows() == 1;
            appendStep(&steps, "13-UPDATE age", ok,
                       "affected=" + std::to_string(resp.getAffectedRows()));
        }

        // 步骤 14：验证 UPDATE 结果
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SELECT * FROM " + TEST_TBL + " WHERE id = 1;");
            auto resp = sendRecv(&sock, req);
            // 验证 age 已变为 26
            bool ok = resp.getSuccess() && !resp.getRows().empty()
                      && resp.getRows().size() >= 1
                      && resp.getRows()[0].size() >= 3
                      && resp.getRows()[0][2] == "26";
            appendStep(&steps, "14-VERIFY UPDATE result", ok,
                       "age=" + (resp.getRows().empty() ? "N/A" : resp.getRows()[0][2]));
        }

        // 步骤 15：DELETE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("DELETE FROM " + TEST_TBL + " WHERE id = 3;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess() && resp.getAffectedRows() == 1;
            appendStep(&steps, "15-DELETE by id", ok,
                       "affected=" + std::to_string(resp.getAffectedRows()));
        }

        // 步骤 16：验证 DELETE 结果
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SELECT * FROM " + TEST_TBL + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess() && resp.getRows().size() == 2; // 剩下 Alice 和 Bob
            appendStep(&steps, "16-VERIFY DELETE (2 rows left)", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // ══════════════════════════════════════════════
        // 阶段三：DDL 删除操作（DROP TABLE / DROP DATABASE）
        // ══════════════════════════════════════════════

        // 步骤 17：DROP TABLE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("DROP TABLE " + TEST_TBL + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess();
            appendStep(&steps, "17-DROP TABLE", ok, resp.getMessage());
        }

        // 步骤 18：SHOW TABLES 确认表已删除
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setDbName(TEST_DB);
            req.setSql("SHOW TABLES;");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess() && resp.getRows().empty();
            appendStep(&steps, "18-SHOW TABLES after drop", ok,
                       "rows=" + std::to_string(resp.getRows().size()));
        }

        // 步骤 19：DROP DATABASE
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setSql("DROP DATABASE " + TEST_DB + ";");
            auto resp = sendRecv(&sock, req);
            bool ok = resp.getSuccess();
            appendStep(&steps, "19-DROP DATABASE", ok, resp.getMessage());
        }

        // 步骤 20：SHOW DATABASES 确认库已删除
        {
            auto req = NetworkTransferData(NetworkTransferData::SQL_EXEC_REQUEST, TEST_UID);
            req.setSql("SHOW DATABASES;");
            auto resp = sendRecv(&sock, req);
            // TEST_DB 不应再出现
            bool found = false;
            for (const auto &row : resp.getRows()) {
                if (!row.empty() && row[0] == TEST_DB) {
                    found = true;
                    break;
                }
            }
            bool ok = resp.getSuccess() && !found;
            appendStep(&steps, "20-SHOW DATABASES after drop", ok,
                       "db_found=" + std::string(found ? "true" : "false"));
        }

        // ══════════════════════════════════════════════
        // 阶段四：DbLog 模块验证
        // ══════════════════════════════════════════════

        DbLogManager *dbLog = core.getDbLogManager();

        // 步骤 21：DbLogManager 实例存在
        {
            bool ok = (dbLog != nullptr);
            appendStep(&steps, "21-DbLogManager instance exists", ok,
                       ok ? "present" : "nullptr");
        }

        // 步骤 22：日志文件存在
        {
            const std::filesystem::path logPath =
                std::filesystem::path("src") / "dbLog" / "dbOperation.log";
            bool ok = std::filesystem::exists(logPath);
            appendStep(&steps, "22-Log file exists", ok,
                       ok ? logPath.string() : "NOT FOUND");
        }

        // 步骤 23：操作计数 > 0
        {
            std::int64_t opCount = dbLog != nullptr ? dbLog->getCurrentOperationId() : 0;
            // 预期至少：CREATE_DB + CREATE_TABLE + INSERTx3 + UPDATE + DELETE
            //          + DROP_TABLE + DROP_DB = 8
            bool ok = (opCount >= 8);
            appendStep(&steps, "23-Operation count >= 8", ok,
                       "count=" + std::to_string(opCount));
        }

        // 步骤 24：getLogsForDatabase 返回正确数量的日志
        {
            std::vector<LogBlock> logs;
            if (dbLog != nullptr) {
                logs = dbLog->getLogsForDatabase(TEST_DB);
            }
            // 预期至少 8 条
            bool ok = (logs.size() >= 8);
            appendStep(&steps, "24-getLogsForDatabase count >= 8", ok,
                       "log_count=" + std::to_string(logs.size()));
        }

        // 步骤 25：验证日志包含 CREATE DATABASE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::CreateDatabase) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "25-Log contains CreateDatabase", found,
                       found ? "yes" : "no");
        }

        // 步骤 26：验证日志包含 CREATE TABLE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::CreateTable) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "26-Log contains CreateTable", found,
                       found ? "yes" : "no");
        }

        // 步骤 27：验证日志包含 INSERT 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::Insert) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "27-Log contains Insert", found,
                       found ? "yes" : "no");
        }

        // 步骤 28：验证日志包含 UPDATE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::Update) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "28-Log contains Update", found,
                       found ? "yes" : "no");
        }

        // 步骤 29：验证日志包含 DELETE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::Delete) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "29-Log contains Delete", found,
                       found ? "yes" : "no");
        }

        // 步骤 30：验证日志包含 DROP TABLE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::DropTable) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "30-Log contains DropTable", found,
                       found ? "yes" : "no");
        }

        // 步骤 31：验证日志包含 DROP DATABASE 操作
        {
            bool found = false;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (const auto &log : logs) {
                    if (log.getOperationType() == DbLogOperationType::DropDatabase) {
                        found = true;
                        break;
                    }
                }
            }
            appendStep(&steps, "31-Log contains DropDatabase", found,
                       found ? "yes" : "no");
        }

        // 步骤 32：LOG 日志按时间升序排列
        {
            bool ordered = true;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase(TEST_DB);
                for (std::size_t i = 1; i < logs.size(); ++i) {
                    // 比较时间戳：前一条应 <= 后一条
                    const auto &prev = logs[i - 1].getTimestamp();
                    const auto &curr = logs[i].getTimestamp();
                    if (prev.getYear() > curr.getYear()
                        || (prev.getYear() == curr.getYear() && prev.getMonth() > curr.getMonth())
                        || (prev.getYear() == curr.getYear() && prev.getMonth() == curr.getMonth()
                            && prev.getDay() > curr.getDay())) {
                        ordered = false;
                        break;
                    }
                }
            }
            appendStep(&steps, "32-Logs sorted by timestamp", ordered,
                       ordered ? "yes" : "no");
        }

        // 步骤 33：dbRecover 方法调用成功
        {
            bool ok = false;
            if (dbLog != nullptr) {
                DateTime targetTime;
                // 设为将来时间，确保覆盖所有日志
                targetTime.setYear(2099);
                targetTime.setMonth(12);
                targetTime.setDay(31);
                targetTime.setHour(23);
                targetTime.setMinute(59);
                targetTime.setSecond(59);
                ok = dbLog->dbRecover(TEST_DB, targetTime);
            }
            appendStep(&steps, "33-dbRecover succeeds", ok,
                       ok ? "yes" : "no");
        }

        // 步骤 34：LogBlock JSON 序列化/反序列化
        {
            LogBlock original;
            original.setOperationId(99999);
            original.setDatabaseName("test_json_db");
            original.setTableName("test_json_tbl");
            original.setOperationType(DbLogOperationType::Insert);
            original.setAfterData("{\"id\":1,\"name\":\"Test\"}");
            original.setSqlText("INSERT INTO test_json_tbl VALUES (1, 'Test');");

            const std::string jsonStr = original.toJsonString();
            LogBlock restored;
            bool ok = LogBlock::fromJsonString(jsonStr, restored)
                      && restored.getOperationId() == 99999
                      && restored.getDatabaseName() == "test_json_db"
                      && restored.getTableName() == "test_json_tbl"
                      && restored.getOperationType() == DbLogOperationType::Insert
                      && restored.getAfterData() == "{\"id\":1,\"name\":\"Test\"}";
            appendStep(&steps, "34-LogBlock JSON round-trip", ok,
                       ok ? "yes" : "no");
        }

        // 步骤 35：空数据库无日志不报错
        {
            bool ok = true;
            if (dbLog != nullptr) {
                auto logs = dbLog->getLogsForDatabase("__nonexistent_db__");
                ok = logs.empty();
            }
            appendStep(&steps, "35-Empty logs for unknown DB", ok,
                       ok ? "empty" : "not empty");
        }

        // ── 关闭连接 ──
        sock.shutdown(asio::ip::tcp::socket::shutdown_both);
        sock.close();

        // ── 汇总结果 ──
        overall = std::all_of(steps.begin(), steps.end(),
                              [](const TestStepResult &s) { return s.passed; });

    } catch (const std::exception &e) {
        fatalMsg = e.what();
        overall = false;
    }

    // ── 清理 ──
    if (receiver) {
        receiver->stop();
    }
    writeReport(steps, overall, fatalMsg);

    // ── 控制台输出 ──
    std::cout << "\n========== DbLog Full Test Results ==========\n";
    for (const auto &s : steps) {
        std::cout << "  " << (s.passed ? "[PASS]" : "[FAIL]")
                  << " " << s.name << " - " << s.detail << "\n";
    }
    std::cout << "==============================================\n";
    std::cout << "Overall: " << (overall ? "PASS" : "FAIL") << "\n";
    if (!fatalMsg.empty()) {
        std::cerr << "FATAL: " << fatalMsg << "\n";
    }

    return overall ? 0 : 1;
}
