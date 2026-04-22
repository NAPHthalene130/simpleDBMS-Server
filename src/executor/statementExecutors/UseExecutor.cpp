#include "UseExecutor.h"

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

UseExecutor::UseExecutor(Core *core)
    : StatementExecutor(core)
{
}

ExecutionStatementType UseExecutor::getSupportedType() const
{
    return ExecutionStatementType::Use;
}

ExecutionResult UseExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        return buildFailureResult("UseExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("UseExecutor received mismatched statement type.");
    }

    const auto *useStmt = static_cast<const UseStmt *>(statement);
    if (useStmt->getDbName().empty()) {
        return buildFailureResult("USE statement requires a non-empty database name.");
    }

    executionContext->setCurrentDbName(useStmt->getDbName());

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage("USE statement accepted in stub mode.");
    executionResult.setAffectedRows(0);
    return executionResult;
}
