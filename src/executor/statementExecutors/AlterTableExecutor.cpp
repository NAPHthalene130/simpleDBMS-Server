#include "AlterTableExecutor.h"

#include <exception>
#include <filesystem>
#include <string>

#include "dbLog/DbLogManager.h"
#include "dbLog/DbLogSnapshotUtils.h"
#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message,
                                   const std::string &dbName = "",
                                   const std::string &tableName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    return executionResult;
}

ExecutionResult buildSuccessResult(const std::string &message,
                                   const std::string &dbName = "",
                                   const std::string &tableName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    return executionResult;
}
} // namespace

AlterTableExecutor::AlterTableExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType AlterTableExecutor::getSupportedType() const
{
    return ExecutionStatementType::AlterTable;
}

ExecutionResult AlterTableExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "AlterTableExecutor", "execute",
                         "Alter table input pointer is invalid.");
        return buildFailureResult("AlterTableExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "AlterTableExecutor", "execute",
                         "Received mismatched statement type.");
        return buildFailureResult("AlterTableExecutor received mismatched statement type.");
    }

    const auto *alterStmt = static_cast<const AlterTableStmt *>(statement);
    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = alterStmt->getTableName();

    if (dbName.empty()) {
        return buildFailureResult("No database selected. Use USE <database> first.",
                                  dbName, tableName);
    }

    if (tableName.empty()) {
        return buildFailureResult("ALTER TABLE requires a non-empty table name.",
                                  dbName, tableName);
    }

    if (databaseManager == nullptr) {
        return buildFailureResult("Database manager is not initialized.",
                                  dbName, tableName);
    }

    try {
        nlohmann::json beforeSnapshot;
        const bool shouldLog = core != nullptr && core->getDbLogManager() != nullptr;
        if (shouldLog) {
            try {
                const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
                if (std::filesystem::exists(dbPath / (tableName + ".tdf"))) {
                    beforeSnapshot = dblog_snapshot::buildTableSnapshot(dbPath, tableName);
                }
            } catch (...) {
                beforeSnapshot = nlohmann::json();
            }
        }

        auto buildSuccessWithLog = [&](const std::string &message, const std::string &sqlText) {
            if (shouldLog && !beforeSnapshot.is_null()) {
                try {
                    const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
                    const nlohmann::json afterSnapshot = dblog_snapshot::buildTableSnapshot(dbPath, tableName);
                    core->getDbLogManager()->logAlterTable(
                        dbName,
                        tableName,
                        beforeSnapshot.dump(),
                        afterSnapshot.dump(),
                        sqlText
                    );
                } catch (...) {
                }
            }
            return buildSuccessResult(message, dbName, tableName);
        };

        switch (alterStmt->getTargetType()) {
        case AlterTableTargetType::AddColumn: {
            const std::string &colName = alterStmt->getColumnName();
            if (colName.empty()) {
                return buildFailureResult("Column name cannot be empty.", dbName, tableName);
            }
            const storage::DataType dataType = mapColumnType(alterStmt->getColumnType());
            if (!databaseManager->addColumn(dbName, tableName, colName, dataType,
                                            alterStmt->getVarcharLen(),
                                            alterStmt->getDefaultValue())) {
                return buildFailureResult(
                    "Failed to add column " + colName + " to table " + tableName + ".",
                    dbName, tableName);
            }
            LogWriter::info("executor", "AlterTableExecutor", "execute",
                            "Column added: " + dbName + "." + tableName + "." + colName);
            return buildSuccessWithLog("Add column succeeded.", "ALTER TABLE " + tableName + " ADD COLUMN " + colName);
        }

        case AlterTableTargetType::DropColumn: {
            const std::string &colName = alterStmt->getColumnName();
            if (colName.empty()) {
                return buildFailureResult("Column name cannot be empty.", dbName, tableName);
            }
            if (!databaseManager->dropColumn(dbName, tableName, colName)) {
                return buildFailureResult(
                    "Failed to drop column " + colName + " from table " + tableName + ".",
                    dbName, tableName);
            }
            LogWriter::info("executor", "AlterTableExecutor", "execute",
                            "Column dropped: " + dbName + "." + tableName + "." + colName);
            return buildSuccessWithLog("Drop column succeeded.", "ALTER TABLE " + tableName + " DROP COLUMN " + colName);
        }

        case AlterTableTargetType::RenameColumn: {
            const std::string &oldName = alterStmt->getColumnName();
            const std::string &newName = alterStmt->getNewColumnName();
            if (oldName.empty() || newName.empty()) {
                return buildFailureResult("Column names cannot be empty.", dbName, tableName);
            }
            if (!databaseManager->renameColumn(dbName, tableName, oldName, newName)) {
                return buildFailureResult(
                    "Failed to rename column from " + oldName + " to " + newName + ".",
                    dbName, tableName);
            }
            LogWriter::info("executor", "AlterTableExecutor", "execute",
                            "Column renamed: " + dbName + "." + tableName + "."
                                + oldName + " -> " + newName);
            return buildSuccessWithLog("Rename column succeeded.",
                                       "ALTER TABLE " + tableName + " RENAME COLUMN " + oldName + " TO " + newName);
        }

        case AlterTableTargetType::AlterColumnType: {
            const std::string &colName = alterStmt->getColumnName();
            if (colName.empty()) {
                return buildFailureResult("Column name cannot be empty.", dbName, tableName);
            }
            const storage::DataType newType = mapColumnType(alterStmt->getColumnType());
            if (!databaseManager->alterColumnType(dbName, tableName, colName, newType,
                                                  alterStmt->getVarcharLen())) {
                return buildFailureResult(
                    "Failed to alter column type for " + colName + ".",
                    dbName, tableName);
            }
            LogWriter::info("executor", "AlterTableExecutor", "execute",
                            "Column type altered: " + dbName + "." + tableName + "." + colName);
            return buildSuccessWithLog("Alter column type succeeded.",
                                       "ALTER TABLE " + tableName + " MODIFY COLUMN " + colName);
        }

        default:
            return buildFailureResult("Unknown ALTER TABLE operation.", dbName, tableName);
        }
    } catch (const std::exception &e) {
        LogWriter::error("executor", "AlterTableExecutor", "execute",
                         std::string("Alter table failed: ") + e.what());
        return buildFailureResult(std::string("Alter table failed: ") + e.what(),
                                  dbName, tableName);
    }
}

storage::DataType AlterTableExecutor::mapColumnType(const std::string &typeStr) const
{
    if (typeStr == "INT" || typeStr == "INTEGER" || typeStr == "BOOL" || typeStr == "BOOLEAN") {
        return storage::DataType::INT;
    }
    if (typeStr == "FLOAT" || typeStr == "DOUBLE" || typeStr == "DECIMAL") {
        return storage::DataType::FLOAT;
    }
    if (typeStr == "VARCHAR" || typeStr == "CHAR") {
        return storage::DataType::VARCHAR;
    }
    if (typeStr == "DATE" || typeStr == "DATETIME") {
        return storage::DataType::INT;
    }
    return storage::DataType::TEXT;
}
