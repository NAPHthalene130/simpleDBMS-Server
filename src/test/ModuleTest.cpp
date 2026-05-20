#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Core.h"
#include "TestUtils.h"
#include "binder/BinderManager.h"
#include "binder/Binder.h"
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
#include "models/dbLog/LogBlock.h"
#include "models/executor/ExecutionStatementType.h"
#include "models/network/NetData.h"
#include "models/network/NetworkExecutionContext.h"
#include "models/parser/CreateDbStmt.h"
#include "models/parser/ParseResult.h"
#include "models/storage/TableBlock.h"
#include "models/tokenizer/Token.h"
#include "network/ClientSessionManager.h"
#include "network/NetReceiver.h"
#include "network/NetSender.h"
#include "network/NetworkManager.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "plan/PlanManager.h"
#include "plan/Planner.h"
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
    int testId = 0;
    std::vector<int> passedIds, failedIds;

    auto check = [&](bool condition, const std::string &desc) {
        ++testId;
        if (condition) {
            std::cout << "PASS: " << desc << std::endl;
            ++passCount; passedIds.push_back(testId);
        } else {
            std::cout << "FAIL: " << desc << std::endl;
            ++failCount; failedIds.push_back(testId);
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

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: ParserManager — parse basic SQL
    // ═══════════════════════════════════════════════════════════════════

    Tokenizer *tokenizer = core.getTokenizer();
    ParserManager *parserManager = core.getParserManager();

    check(parserManager->getParser() != nullptr, "ParserManager::getParser() non-null");

    tokenizer->reset("CREATE DATABASE moduletest_db;");
    std::vector<Token> tokens = tokenizer->tokenize();
    check(!tokens.empty(), "Tokenize: CREATE DATABASE produces tokens");
    check(tokens.back().getType() == SqlTokenType::EndOfFile,
          "Tokenize: last token is EndOfFile");

    bool parseOk = false;
    if (!tokens.empty()) {
        Parser *parser = parserManager->getParser();
        ParseResult pr = parser->parse(tokens);
        parseOk = pr.success;
        check(pr.success, "Parser: parse CREATE DATABASE moduletest_db");
        if (pr.success) {
            check(pr.statement != nullptr, "Parser: AST statement non-null");
            check(pr.statement->getStmtType() == ExecutionStatementType::CreateDatabase,
                  "Parser: AST type is CreateDatabase");
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: Tokenizer — tokenize various SQL strings
    // ═══════════════════════════════════════════════════════════════════

    {
        tokenizer->reset("SELECT * FROM t1 WHERE a = 1;");
        auto toks = tokenizer->tokenize();
        check(toks.size() >= 3, "Tokenize: SELECT * FROM ...");
        check(toks[0].getValue() == "SELECT" || toks[0].getValue() == "select",
              "Tokenize: first token is SELECT keyword");
    }

    {
        tokenizer->reset("INSERT INTO items VALUES (1, 'hello', 3.14);");
        auto toks = tokenizer->tokenize();
        check(toks.size() >= 3, "Tokenize: INSERT INTO ...");
        bool hasString = false;
        for (const auto &t : toks) {
            if (t.getType() == SqlTokenType::String) { hasString = true; break; }
        }
        check(hasString, "Tokenize: INSERT string literal found");
    }

    {
        tokenizer->reset("123");
        auto toks = tokenizer->tokenize();
        bool hasNumber = !toks.empty() && toks[0].getType() == SqlTokenType::Number;
        check(hasNumber, "Tokenize: '123' yields Number token");
    }

    {
        tokenizer->reset("'hello world'");
        auto toks = tokenizer->tokenize();
        bool hasString = !toks.empty() && toks[0].getType() == SqlTokenType::String;
        check(hasString, "Tokenize: string literal identified");
    }

    {
        tokenizer->reset("DROP TABLE foo;");
        auto toks = tokenizer->tokenize();
        check(toks.size() >= 3, "Tokenize: DROP TABLE foo");
        check(toks[0].getValue() == "DROP" || toks[0].getValue() == "drop",
              "Tokenize: first token is DROP keyword");
    }

    {
        tokenizer->reset("UPDATE t SET a = 2 WHERE b = 'x';");
        auto toks = tokenizer->tokenize();
        check(toks.size() >= 3, "Tokenize: UPDATE ... SET ...");
    }

    /**
     * @brief 验证 Tokenizer 的 hasMoreTokens 和 nextToken 流式接口
     * @author NAPH130
     */
    {
        tokenizer->reset("USE system;");
        check(tokenizer->hasMoreTokens(), "Tokenize: hasMoreTokens before consumption");
        Token first = tokenizer->nextToken();
        check(first.getType() != SqlTokenType::Unknown, "Tokenize: nextToken() returns valid token");
    }

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: BinderManager — Binder non-null
    // ═══════════════════════════════════════════════════════════════════

    BinderManager *binderManager = core.getBinderManager();
    check(binderManager->getBinder() != nullptr, "BinderManager::getBinder() non-null");

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: PlanManager — Planner non-null
    // ═══════════════════════════════════════════════════════════════════

    PlanManager *planManager = core.getPlanManager();
    check(planManager->getPlanner() != nullptr, "PlanManager::getPlanner() non-null");

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: DbLogManager — non-null and functional
    // ═══════════════════════════════════════════════════════════════════

    DbLogManager *dbLogManager = core.getDbLogManager();
    check(dbLogManager != nullptr, "DbLogManager non-null");

    std::int64_t opId = dbLogManager->getCurrentOperationId();
    check(opId >= 0, "DbLogManager: getCurrentOperationId() >= 0");

    dbLogManager->logCreateDatabase("moduletest_dblog", "CREATE DATABASE moduletest_dblog;");
    std::int64_t opId2 = dbLogManager->getCurrentOperationId();
    check(opId2 >= 0, "DbLogManager: logCreateDatabase does not crash");

    std::vector<LogBlock> logs = dbLogManager->getLogsForDatabase("moduletest_dblog");
    check(!logs.empty(), "DbLogManager: getLogsForDatabase returns entries");

    dbLogManager->logCreateTable("moduletest_dblog", "t1", "{}", "CREATE TABLE t1 (id INT);");
    std::int64_t opId3 = dbLogManager->getCurrentOperationId();
    check(opId3 >= opId2, "DbLogManager: logCreateTable does not crash");

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: FileManager — read/write page operations
    // ═══════════════════════════════════════════════════════════════════

    {
        std::filesystem::path tmpPath = std::filesystem::temp_directory_path() / "moduletest_fm.dat";
        std::string header = "FILE_HEADER_TEST_12345";
        bool writeHdrOk = FileManager::writeHeader(tmpPath, header);
        check(writeHdrOk, "FileManager: writeHeader succeeds");

        std::string readHdr = FileManager::readHeader(tmpPath);
        check(readHdr == header, "FileManager: readHeader matches written data");

        std::string pageContent("PAGE_DATA_HELLO_WORLD");
        bool writePageOk = FileManager::writePage(tmpPath, 1, pageContent);
        check(writePageOk, "FileManager: writePage succeeds");

        std::string readPageContent = FileManager::readPage(tmpPath, 1);
        check(readPageContent == pageContent, "FileManager: readPage matches written data");

        std::string emptyPage = FileManager::readPage(tmpPath, 9999);
        check(emptyPage.empty(), "FileManager: readPage non-existent page returns empty");

        std::string badFile = FileManager::readPage("nonexistent_moduletest.bin", 0);
        check(badFile.empty(), "FileManager: readPage non-existent file returns empty");

        std::filesystem::remove(tmpPath);
    }

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: TableDefManager — validateColumn / validateRename
    // ═══════════════════════════════════════════════════════════════════

    check(TableDefManager::validateColumn("valid_col", storage::DataType::INT, 0),
          "TableDefManager: validateColumn INT valid");
    check(TableDefManager::validateColumn("col2", storage::DataType::VARCHAR, 100),
          "TableDefManager: validateColumn VARCHAR(100) valid");
    check(TableDefManager::validateColumn("col3", storage::DataType::FLOAT, 0),
          "TableDefManager: validateColumn FLOAT valid");
    check(TableDefManager::validateColumn("col4", storage::DataType::TEXT, 0),
          "TableDefManager: validateColumn TEXT valid");
    check(!TableDefManager::validateColumn("", storage::DataType::INT, 0),
          "TableDefManager: validateColumn empty name invalid");

    check(TableDefManager::validateRename("old_name", "new_name"),
          "TableDefManager: validateRename valid");
    check(!TableDefManager::validateRename("", "new_name"),
          "TableDefManager: validateRename empty oldName invalid");
    check(!TableDefManager::validateRename("old_name", ""),
          "TableDefManager: validateRename empty newName invalid");
    check(!TableDefManager::validateRename("same", "same"),
          "TableDefManager: validateRename same name invalid");

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: NetworkManager — sub-components
    // ═══════════════════════════════════════════════════════════════════

    NetworkManager *networkManager = core.getNetworkManager();
    check(networkManager->getNetReceiver() != nullptr, "NetworkManager: NetReceiver non-null");
    check(networkManager->getNetSender() != nullptr, "NetworkManager: NetSender non-null");
    check(networkManager->getClientSessionManager() != nullptr, "NetworkManager: ClientSessionManager non-null");

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: Core — start / stop without crash
    // ═══════════════════════════════════════════════════════════════════

    try {
        core.start();
        check(true, "Core: start() no throw");
    } catch (...) {
        check(false, "Core: start() threw exception");
    }

    try {
        core.stop();
        check(true, "Core: stop() no throw");
    } catch (...) {
        check(false, "Core: stop() threw exception");
    }

    try {
        core.start();
        core.stop();
        check(true, "Core: start/stop cycle no throw");
    } catch (...) {
        check(false, "Core: start/stop cycle threw exception");
    }

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: ExecutorEngine — execute simple "USE DATABASE system;"
    // ═══════════════════════════════════════════════════════════════════

    {
        SqlPipeline *pipeline = core.getSqlPipeline();
        NetworkExecutionContext netCtx;
        netCtx.setConnectionId("module-test");
        netCtx.setCurrentUser("tester");
        netCtx.setIsAuthorized(true);

        std::string req = NetData("sql", "USE DATABASE system;").toJson();
        NetData resp = pipeline->handleRequest(req, &netCtx);
        check(!resp.getContent().empty(), "Execute: USE DATABASE system response non-empty");

        nlohmann::json j = nlohmann::json::parse(resp.getContent());
        check(j.value("success", false) || j.contains("message") || j.contains("resultSet"),
              "Execute: USE DATABASE system produces valid response");
    }

    // ═══════════════════════════════════════════════════════════════════
    // NEW TESTS: verify new executor types (AlterTable, Dcl) exist
    //            via direct executor checks
    // ═══════════════════════════════════════════════════════════════════

    {
        check(core.getExecutorManager()->getAlterTableExecutor() != nullptr,
              "Executor: AlterTableExecutor explicitly non-null (type check)");
        check(core.getExecutorManager()->getDclExecutor() != nullptr,
              "Executor: DclExecutor explicitly non-null (type check)");
    }

    int total = passCount + failCount;
    int pct = total > 0 ? passCount * 100 / total : 0;
    std::cout << "\nSummary: " << passCount << "/" << total << " passed (" << pct << "%)" << std::endl;
    writeReport("ModuleTest", passCount, failCount, passedIds, failedIds);

    return failCount > 0 ? 1 : 0;
}
