#include "ShowExecutor.h"

namespace
{
/**
 * @brief 构建失败执行结果
 * @author YuzhSong
 * @param message 英文失败消息
 * @return 失败执行结果
 */
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
} // namespace

ShowExecutor::ShowExecutor(Core *core)
    : StatementExecutor(core)
{
}

ExecutionStatementType ShowExecutor::getSupportedType() const
{
    return ExecutionStatementType::Show;
}

ExecutionResult ShowExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    (void)executionContext;
    if (statement == nullptr) {
        return buildFailureResult("ShowExecutor received null statement pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("ShowExecutor received mismatched statement type.");
    }

    const auto *showStmt = static_cast<const ShowStmt *>(statement);
    std::vector<std::vector<std::string>> resultSet;
    std::string message;

    switch (showStmt->getTargetType()) {
    case ShowTargetType::Databases:
        message = "SHOW DATABASES executed in stub mode.";
        break;
    case ShowTargetType::Tables:
        message = "SHOW TABLES executed in stub mode.";
        break;
    case ShowTargetType::Database:
        resultSet.push_back({showStmt->getTargetName()});
        message = "SHOW DATABASE <name> executed in stub mode.";
        break;
    case ShowTargetType::Table:
        resultSet.push_back({showStmt->getTargetName()});
        message = "SHOW TABLE <name> executed in stub mode.";
        break;
    default:
        return buildFailureResult("SHOW statement target type is invalid.");
    }

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setAffectedRows(0);
    executionResult.setResultSet(resultSet);
    return executionResult;
}
