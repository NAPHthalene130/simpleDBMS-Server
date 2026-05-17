#include "DropExecutor.h"

#include <filesystem>

#include "Core.h"
#include "dbLog/DbLogManager.h"
#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message,
                                   const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}

ExecutionResult buildSuccessResult(const std::string &message,
                                   const std::string &dbName)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}
} // namespace

DropExecutor::DropExecutor(Core *core, SystemCatalogManager *systemCatalogManager, DatabaseManager *databaseManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager), databaseManager(databaseManager)
{
}

ExecutionStatementType DropExecutor::getSupportedType() const
{
    return ExecutionStatementType::Drop;
}

ExecutionResult DropExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "DropExecutor", "execute", "Drop input pointer is invalid.");
        return buildFailureResult("DropExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "DropExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("DropExecutor received mismatched statement type.");
    }

    const auto *dropStmt = static_cast<const DropStmt *>(statement);
    const std::string targetName = dropStmt->getTargetName();
    const std::string dbName = executionContext->getCurrentDbName();

    if (targetName.empty()) {
        LogWriter::warning("executor", "DropExecutor", "execute", "DROP target name is empty.");
        return buildFailureResult("DROP statement requires a non-empty target name.");
    }

    switch (dropStmt->getTargetType()) {
    case DropTargetType::Database: {
        if (systemCatalogManager == nullptr) {
            return buildFailureResult("System catalog manager is not initialized.");
        }
        if (!systemCatalogManager->checkDbExists(targetName)) {
            LogWriter::warning("executor", "DropExecutor", "execute",
                               "Database does not exist: " + targetName + ".");
            return buildFailureResult("Database does not exist.", targetName);
        }

        // 先记录日志（此时文件夹仍存在），再执行删除
        // 避免日志系统在文件夹删除后通过 create_directories 重建，
        // 导致后续 CREATE DATABASE 误判文件夹已存在。
        // 作者：NAPH130
        if (core != nullptr && core->getDbLogManager() != nullptr) {
            core->getDbLogManager()->logDropDatabase(
                targetName,
                "Database metadata snapshot for: " + targetName,
                "DROP DATABASE " + targetName
            );
        }

        if (!systemCatalogManager->dropDatabase(targetName)) {
            LogWriter::error("executor", "DropExecutor", "execute",
                             "Failed to drop database: " + targetName + ".");
            return buildFailureResult("Failed to drop database.", targetName);
        }

        LogWriter::info("executor", "DropExecutor", "execute",
                        "Database dropped successfully: " + targetName + ".");
        return buildSuccessResult("Drop database succeeded.", "");
    }

    case DropTargetType::Table: {
        if (dbName.empty()) {
            return buildFailureResult("No database selected.", dbName);
        }
        if (databaseManager == nullptr) {
            return buildFailureResult("Database manager is not initialized.", dbName);
        }

        // 删除前记录日志，包含真实表结构快照以便恢复 — NAPH130
        if (core != nullptr && core->getDbLogManager() != nullptr) {
            try {
                const auto dbRootPath = SystemCatalogManager::getDataRootPath();
                const auto dbPath = dbRootPath / dbName;
                if (std::filesystem::exists(dbPath / (targetName + ".tdf"))) {
                    auto table = storage::Table::load(dbPath, targetName);
                    const auto &schema = table.schema();
                    nlohmann::json tableSnapshot;
                    tableSnapshot["table_name"] = targetName;
                    tableSnapshot["columns"] = schema.columns;
                    tableSnapshot["create_sql"] = "CREATE TABLE " + targetName + " (...)";
                    tableSnapshot["column_count"] = schema.columns.size();
                    core->getDbLogManager()->logDropTable(
                        dbName, targetName,
                        tableSnapshot.dump(),
                        "DROP TABLE " + targetName
                    );
                }
            } catch (...) {
                // 如果读取失败，回退到简单记录
                core->getDbLogManager()->logDropTable(
                    dbName, targetName,
                    R"({"table_name":")" + targetName + R"("})",
                    "DROP TABLE " + targetName
                );
            }
        }

        if (!databaseManager->dropTable(targetName)) {
            LogWriter::error("executor", "DropExecutor", "execute",
                             "Failed to drop table " + targetName + " in " + dbName + ".");
            return buildFailureResult("Failed to drop table.", dbName);
        }

        LogWriter::info("executor", "DropExecutor", "execute",
                        "Table dropped successfully: " + dbName + "." + targetName + ".");
        return buildSuccessResult("Drop table succeeded.", dbName);
    }

    default:
        return buildFailureResult("DROP target type is invalid.");
    }
}