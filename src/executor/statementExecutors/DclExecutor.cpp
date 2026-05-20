#include "DclExecutor.h"

#include <exception>
#include <string>
#include <vector>

#include "log/LogWriter.h"

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
                                   const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}
} // namespace

DclExecutor::DclExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType DclExecutor::getSupportedType() const
{
    return ExecutionStatementType::Dcl;
}

ExecutionResult DclExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "DclExecutor", "execute", "DCL input pointer is invalid.");
        return buildFailureResult("DclExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "DclExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("DclExecutor received mismatched statement type.");
    }

    const auto *dclStmt = static_cast<const DclStmt *>(statement);
    const std::string userName = dclStmt->getUserName();

    if (userName.empty()) {
        return buildFailureResult("DCL statement requires a non-empty user name.");
    }

    if (databaseManager == nullptr) {
        return buildFailureResult("Database manager is not initialized.");
    }

    try {
        if (dclStmt->getOperationType() == DclOperationType::Grant) {
            const std::string password = dclStmt->getPassword();
            if (password.empty()) {
                return buildFailureResult("GRANT requires a password (IDENTIFIED BY <password>).");
            }

            // 检查用户是否已存在
            // 作者：NAPH130
            const std::vector<std::string> systemColumns = {"id", "password"};
            const std::vector<storage::Table::WhereCondition> whereConditions = [](
                const std::string &uname) {
                storage::Table::WhereCondition wc;
                wc.column = "id";
                wc.op = storage::Table::CompareOp::EQ;
                wc.value = uname;
                return std::vector<storage::Table::WhereCondition>{wc};
            }(userName);
            const auto existingRows = databaseManager->selectRows("system", "user",
                                                                  systemColumns, whereConditions);
            if (!existingRows.empty()) {
                return buildFailureResult("User already exists: " + userName);
            }

            const std::vector<std::string> values = {userName, password};
            if (!databaseManager->insertRow("system", "user", values)) {
                return buildFailureResult("Failed to create user: " + userName);
            }

            LogWriter::info("executor", "DclExecutor", "execute",
                            "User granted: " + userName);
            return buildSuccessResult("User created successfully: " + userName);
        }

        // REVOKE：删除用户
        // 作者：NAPH130
        const std::vector<std::string> systemColumns = {"id", "password"};
        const std::vector<storage::Table::WhereCondition> whereConditions = [](
            const std::string &uname) {
            storage::Table::WhereCondition wc;
            wc.column = "id";
            wc.op = storage::Table::CompareOp::EQ;
            wc.value = uname;
            return std::vector<storage::Table::WhereCondition>{wc};
        }(userName);
        const auto existingRows = databaseManager->selectRows("system", "user",
                                                              systemColumns, whereConditions);
        if (existingRows.empty()) {
            return buildFailureResult("User does not exist: " + userName);
        }

        if (!databaseManager->deleteRowByPrimaryKey("system", "user", userName)) {
            return buildFailureResult("Failed to delete user: " + userName);
        }

        LogWriter::info("executor", "DclExecutor", "execute",
                        "User revoked: " + userName);
        return buildSuccessResult("User deleted successfully: " + userName);
    } catch (const std::exception &e) {
        LogWriter::error("executor", "DclExecutor", "execute",
                         std::string("DCL operation failed: ") + e.what());
        return buildFailureResult(std::string("DCL operation failed: ") + e.what());
    }
}
