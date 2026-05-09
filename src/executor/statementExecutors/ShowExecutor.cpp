#include "ShowExecutor.h"

#include <algorithm>
#include <array>

#include "log/LogWriter.h"

namespace {

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}
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
                                   const std::string &dbName,
                                   const std::vector<std::vector<std::string>> &resultSet,
                                   const std::vector<std::string> &columns = {})
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setResultSet(resultSet);
    executionResult.setColumns(columns);
    return executionResult;
}
} // namespace

ShowExecutor::ShowExecutor(Core *core,
                           SystemCatalogManager *systemCatalogManager,
                           DatabaseManager *databaseManager)
    : StatementExecutor(core),
      systemCatalogManager(systemCatalogManager),
      databaseManager(databaseManager)
{
}

ExecutionStatementType ShowExecutor::getSupportedType() const
{
    return ExecutionStatementType::Show;
}

ExecutionResult ShowExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "ShowExecutor", "execute", "Show input pointer is invalid.");
        return buildFailureResult("ShowExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "ShowExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("ShowExecutor received mismatched statement type.");
    }

    const auto *showStmt = static_cast<const ShowStmt *>(statement);
    const std::string dbName = executionContext->getCurrentDbName();

    LogWriter::info("executor",
                    "ShowExecutor",
                    "execute",
                    std::string("Executing SHOW, targetType=") + std::to_string(static_cast<int>(showStmt->getTargetType())));

    switch (showStmt->getTargetType()) {
    case ShowTargetType::Databases: {
        if (systemCatalogManager == nullptr) {
            return buildFailureResult("System catalog manager is not initialized.");
        }
        const auto databases = systemCatalogManager->getAllDatabases();
        std::vector<std::vector<std::string>> resultSet;
        resultSet.reserve(databases.size());
        for (const auto &db : databases) {
            resultSet.push_back({arrayToString(db.getName())});
        }
        LogWriter::info("executor", "ShowExecutor", "execute",
                        std::string("SHOW DATABASES returned ") + std::to_string(resultSet.size()) + " rows.");
        return buildSuccessResult("SHOW DATABASES succeeded.", "", resultSet, {"DatabaseName"});
    }

    case ShowTargetType::Tables: {
        if (dbName.empty()) {
            return buildFailureResult("No database selected.", dbName);
        }
        if (databaseManager == nullptr) {
            return buildFailureResult("Database manager is not initialized.", dbName);
        }
        const auto tables = databaseManager->getAllTables();
        std::vector<std::vector<std::string>> resultSet;
        resultSet.reserve(tables.size());
        for (const auto &tb : tables) {
            resultSet.push_back({arrayToString(tb.getName())});
        }
        LogWriter::info("executor", "ShowExecutor", "execute",
                        std::string("SHOW TABLES returned ") + std::to_string(resultSet.size()) + " rows.");
        return buildSuccessResult("SHOW TABLES succeeded.", dbName, resultSet, {"TableName"});
    }

    case ShowTargetType::Database: {
        if (systemCatalogManager == nullptr) {
            return buildFailureResult("System catalog manager is not initialized.");
        }
        const std::string &targetName = showStmt->getTargetName();
        const bool exists = systemCatalogManager->checkDbExists(targetName);
        std::vector<std::vector<std::string>> resultSet;
        if (exists) {
            resultSet.push_back({targetName});
        }
        LogWriter::info("executor", "ShowExecutor", "execute",
                        std::string("SHOW DATABASE ") + targetName + " returned "
                            + std::to_string(resultSet.size()) + " rows.");
        return buildSuccessResult("SHOW DATABASE succeeded.", targetName, resultSet, {"DatabaseName"});
    }

    case ShowTargetType::Table: {
        if (dbName.empty()) {
            return buildFailureResult("No database selected.", dbName);
        }
        if (databaseManager == nullptr) {
            return buildFailureResult("Database manager is not initialized.", dbName);
        }
        const std::string &targetName = showStmt->getTargetName();
        const TableBlock tableInfo = databaseManager->getTableInfo(targetName);
        const std::string actualName = arrayToString(tableInfo.getName());
        std::vector<std::vector<std::string>> resultSet;
        if (!actualName.empty()) {
            resultSet.push_back({actualName});
        }
        LogWriter::info("executor", "ShowExecutor", "execute",
                        std::string("SHOW TABLE ") + targetName + " returned "
                            + std::to_string(resultSet.size()) + " rows.");
        return buildSuccessResult("SHOW TABLE succeeded.", dbName, resultSet, {"TableName"});
    }

    default:
        return buildFailureResult("SHOW statement target type is invalid.");
    }
}
