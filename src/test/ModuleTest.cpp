#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Core.h"
#include "binder/BinderManager.h"
#include "core/SqlPipeline.h"
#include "dbLog/DbLogManager.h"
#include "executor/ExecutorEngine.h"
#include "executor/ExecutorManager.h"
#include "executor/statementExecutors/AlterTableExecutor.h"
#include "executor/statementExecutors/CreateDbExecutor.h"
#include "executor/statementExecutors/CreateTableExecutor.h"
#include "executor/statementExecutors/DclExecutor.h"
#include "executor/statementExecutors/DeleteExecutor.h"
#include "executor/statementExecutors/DropExecutor.h"
#include "executor/statementExecutors/InsertExecutor.h"
#include "executor/statementExecutors/SelectExecutor.h"
#include "executor/statementExecutors/ShowExecutor.h"
#include "executor/statementExecutors/UpdateExecutor.h"
#include "executor/statementExecutors/UseDbExecutor.h"
#include "executor/statementExecutors/UseExecutor.h"
#include "models/executor/ExecutionStatementType.h"
#include "models/storage/TableBlock.h"
#include "network/NetworkManager.h"
#include "parser/ParserManager.h"
#include "plan/PlanManager.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/FileManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/manager/TableDefManager.h"
#include "tokenizer/Tokenizer.h"

#ifndef SERVER_PROJECT_ROOT
#define SERVER_PROJECT_ROOT "H:/CODE/DBMS/simpleDBMS-Server"
#endif

int main()
{
    int passCount = 0;
    int failCount = 0;

    auto check = [&](bool condition, const std::string &desc) {
        if (condition) {
            std::cout << "PASS: " << desc << std::endl;
            ++passCount;
        } else {
            std::cout << "FAIL: " << desc << std::endl;
            ++failCount;
        }
    };

    /**
     * @brief 构造 Core 对象并验证所有一级管理器
     * @author NAPH130
     */
    Core core;

    check(core.getNetworkManager() != nullptr, "NetworkManager");
    check(core.getExecutorManager() != nullptr, "ExecutorManager");
    check(core.getStorageManager() != nullptr, "StorageManager");
    check(core.getTokenizer() != nullptr, "Tokenizer");
    check(core.getParserManager() != nullptr, "ParserManager");
    check(core.getBinderManager() != nullptr, "BinderManager");
    check(core.getPlanManager() != nullptr, "PlanManager");
    check(core.getDbLogManager() != nullptr, "DbLogManager");
    check(core.getSqlPipeline() != nullptr, "SqlPipeline");

    /**
     * @brief 验证 StorageManager 子管理器
     * @author NAPH130
     */
    StorageManager *storageManager = core.getStorageManager();
    check(storageManager != nullptr, "StorageManager access");
    if (storageManager != nullptr) {
        check(storageManager->getSystemCatalogManager() != nullptr, "SystemCatalogManager");
        check(storageManager->getDatabaseManager() != nullptr, "DatabaseManager");
        check(storageManager->getTableDefManager() != nullptr, "TableDefManager");
        check(storageManager->getFileManager() != nullptr, "FileManager");
    }

    /**
     * @brief 验证 ExecutorManager 内各执行器
     * @author NAPH130
     */
    ExecutorManager *executorManager = core.getExecutorManager();
    check(executorManager != nullptr, "ExecutorManager access");
    if (executorManager != nullptr) {
        check(executorManager->getCreateDbExecutor() != nullptr, "CreateDbExecutor");
        check(executorManager->getCreateTableExecutor() != nullptr, "CreateTableExecutor");
        check(executorManager->getInsertExecutor() != nullptr, "InsertExecutor");
        check(executorManager->getSelectExecutor() != nullptr, "SelectExecutor");
        check(executorManager->getUseDbExecutor() != nullptr, "UseDbExecutor");
        check(executorManager->getUseExecutor() != nullptr, "UseExecutor");
        check(executorManager->getShowExecutor() != nullptr, "ShowExecutor");
        check(executorManager->getDropExecutor() != nullptr, "DropExecutor");
        check(executorManager->getDeleteExecutor() != nullptr, "DeleteExecutor");
        check(executorManager->getUpdateExecutor() != nullptr, "UpdateExecutor");
        check(executorManager->getAlterTableExecutor() != nullptr, "AlterTableExecutor");
        check(executorManager->getDclExecutor() != nullptr, "DclExecutor");
    }

    /**
     * @brief 验证 ExecutorEngine 已为全部支持类型注册执行器
     * @author NAPH130
     */
    if (executorManager != nullptr) {
        ExecutorEngine *executorEngine = executorManager->getExecutorEngine();
        check(executorEngine != nullptr, "ExecutorEngine");
        if (executorEngine != nullptr) {
            check(executorEngine->hasExecutor(ExecutionStatementType::CreateDatabase), "Executor: CreateDatabase");
            check(executorEngine->hasExecutor(ExecutionStatementType::CreateTable), "Executor: CreateTable");
            check(executorEngine->hasExecutor(ExecutionStatementType::Insert), "Executor: Insert");
            check(executorEngine->hasExecutor(ExecutionStatementType::Select), "Executor: Select");
            check(executorEngine->hasExecutor(ExecutionStatementType::UseDatabase), "Executor: UseDatabase");
            check(executorEngine->hasExecutor(ExecutionStatementType::Use), "Executor: Use");
            check(executorEngine->hasExecutor(ExecutionStatementType::Show), "Executor: Show");
            check(executorEngine->hasExecutor(ExecutionStatementType::Drop), "Executor: Drop");
            check(executorEngine->hasExecutor(ExecutionStatementType::Delete), "Executor: Delete");
            check(executorEngine->hasExecutor(ExecutionStatementType::Update), "Executor: Update");
            check(executorEngine->hasExecutor(ExecutionStatementType::AlterTable), "Executor: AlterTable");
            check(executorEngine->hasExecutor(ExecutionStatementType::Dcl), "Executor: Dcl");
        }
    }

    /**
     * @brief 验证 system 库与 user 表是否已由 Core 构造时自动创建
     * @author NAPH130
     */
    if (storageManager != nullptr) {
        SystemCatalogManager *sysCatalog = storageManager->getSystemCatalogManager();
        DatabaseManager *dbMgr = storageManager->getDatabaseManager();

        bool systemDbExists = false;
        if (sysCatalog != nullptr) {
            systemDbExists = sysCatalog->checkDbExists("system");
        }
        check(systemDbExists, "System database exists");

        bool userTableExists = false;
        if (dbMgr != nullptr && systemDbExists) {
            const auto tables = dbMgr->getAllTablesForDb("system");
            for (const auto &tb : tables) {
                const auto endIt = std::find(tb.getName().begin(), tb.getName().end(), '\0');
                const std::string existingName(tb.getName().begin(), endIt);
                if (existingName == "user") {
                    userTableExists = true;
                    break;
                }
            }
        }
        check(userTableExists, "User table exists in system database");
    }

    const int total = passCount + failCount;
    const int percent = (total > 0) ? static_cast<int>((static_cast<double>(passCount) / total) * 100.0) : 0;

    /**
     * @brief 写入报告到 report.log
     * @author NAPH130
     */
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    std::ostringstream timeStream;
    timeStream << std::setfill('0')
               << (localTime.tm_year + 1900) << "-"
               << std::setw(2) << (localTime.tm_mon + 1) << "-"
               << std::setw(2) << localTime.tm_mday << " "
               << std::setw(2) << localTime.tm_hour << ":"
               << std::setw(2) << localTime.tm_min << ":"
               << std::setw(2) << localTime.tm_sec;

    const auto logPath = std::filesystem::path(SERVER_PROJECT_ROOT) / "src" / "test" / "report.log";

    if (!std::filesystem::exists(logPath.parent_path())) {
        std::filesystem::create_directories(logPath.parent_path());
    }

    std::ofstream logFile(logPath, std::ios::app);
    if (logFile.is_open()) {
        logFile << "==========\n"
                << "ModuleTest\n"
                << timeStream.str() << "\n"
                << passCount << "/" << total << " " << percent << "%\n";
        logFile.close();
    }

    std::cout << "\nSummary: " << passCount << "/" << total << " passed (" << percent << "%)" << std::endl;

    return failCount > 0 ? 1 : 0;
}
