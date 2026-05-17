#include "InsertExecutor.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "Core.h"
#include "dbLog/DbLogManager.h"
#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

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
                                   const std::string &dbName,
                                   const std::string &tableName)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    executionResult.setAffectedRows(1);
    return executionResult;
}

/**
 * @brief 根据目标表列顺序将指定列的值重排为全列值数组
 * @author NAPH130
 * @param tableColumns 目标表全部列名
 * @param schema Meta schema including column metas
 * @param specifiedColumns 用户指定的列名
 * @param specifiedValues 用户指定的值
 * @return 按表列顺序排列的完整值数组，未指定列填充空字符串或用默认值
 */
std::vector<std::string> buildFullValues(const storage::TableSchema &schema,
                                         const std::vector<std::string> &specifiedColumns,
                                         const std::vector<std::string> &specifiedValues)
{
    std::vector<std::string> fullValues(schema.columns.size());

    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        auto it = std::find(specifiedColumns.begin(), specifiedColumns.end(), schema.columns[i]);
        if (it != specifiedColumns.end()) {
            const std::size_t idx = static_cast<std::size_t>(std::distance(specifiedColumns.begin(), it));
            fullValues[i] = specifiedValues[idx];
        } else if (i < schema.columnMetas.size()
                   && (schema.columnMetas[i].integrities & 8) != 0) {
            // AUTO_INCREMENT 列且未指定值 → 留空，由 Table::normalize 自动生成
            // 作者：NAPH130
        } else if (i < schema.columnMetas.size() && !schema.columnMetas[i].defaultValue.empty()) {
            fullValues[i] = schema.columnMetas[i].defaultValue;
        }
    }

    return fullValues;
}

/**
 * @brief 校验 NOT NULL 约束
 * @author NAPH130
 * @return 校验通过返回空字符串，否则返回错误信息
 */
std::string validateNotNull(const storage::TableSchema &schema, const std::vector<std::string> &values)
{
    for (std::size_t i = 0; i < values.size() && i < schema.columnMetas.size(); ++i) {
        if ((schema.columnMetas[i].integrities & 1) != 0 && values[i].empty()) {
            return "Column '" + schema.columns[i] + "' cannot be NULL.";
        }
    }
    return "";
}
} // namespace

InsertExecutor::InsertExecutor(Core *core, DatabaseManager *databaseManager, TableDefManager *tableDefManager)
    : StatementExecutor(core), databaseManager(databaseManager), tableDefManager(tableDefManager)
{
}

ExecutionStatementType InsertExecutor::getSupportedType() const
{
    return ExecutionStatementType::Insert;
}

ExecutionResult InsertExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "InsertExecutor", "execute", "Insert input pointer is invalid.");
        return buildFailureResult("InsertExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "InsertExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("InsertExecutor received mismatched statement type.");
    }

    return executeInsert(static_cast<const InsertStmt *>(statement), executionContext);
}

ExecutionResult InsertExecutor::executeInsert(const InsertStmt *insertStmt, ExecutionContext *executionContext)
{
    const std::string dbName = executionContext != nullptr ? executionContext->getCurrentDbName() : "";
    const std::string tableName = insertStmt != nullptr ? insertStmt->getTableName() : "";

    if (!validateInsertStmt(insertStmt)) {
        LogWriter::warning("executor", "InsertExecutor", "executeInsert",
                           "Insert statement columns and values do not match.");
        return buildFailureResult("Insert statement columns and values do not match.", dbName, tableName);
    }

    if (dbName.empty()) {
        LogWriter::warning("executor", "InsertExecutor", "executeInsert", "No database selected.");
        return buildFailureResult("No database selected. Use USE <database> first.", dbName, tableName);
    }

    if (databaseManager == nullptr) {
        LogWriter::error("executor", "InsertExecutor", "executeInsert", "Database manager is not initialized.");
        return buildFailureResult("Database manager is not initialized.", dbName, tableName);
    }

    const std::vector<std::string> &columnNames = insertStmt->getColumnNames();

    // 获取所有待插入的值行
    // 作者：NAPH130
    std::vector<std::vector<std::string>> allValueRows;
    const auto &multiValues = insertStmt->getMultiValues();
    if (!multiValues.empty()) {
        allValueRows = multiValues;
    } else {
        allValueRows.push_back(insertStmt->getValues());
    }

    std::int32_t totalInserted = 0;

    for (const auto &values : allValueRows) {
        if (columnNames.empty()) {
            if (!databaseManager->insertRow(dbName, tableName, values)) {
                LogWriter::error("executor", "InsertExecutor", "executeInsert",
                                 "Failed to insert row into " + dbName + "." + tableName + ".");
                return buildFailureResult("Insert failed at row " + std::to_string(totalInserted + 1) + ".", dbName, tableName);
            }
        } else {
            try {
                const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
                const auto dbPath = dbRootPath / dbName;

                if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
                    return buildFailureResult("Database does not exist.", dbName, tableName);
                }

                auto table = storage::Table::load(dbPath, tableName);
                const auto &schema = table.schema();

                const std::vector<std::string> fullValues = buildFullValues(schema, columnNames, values);

                const std::string notNullError = validateNotNull(schema, fullValues);
                if (!notNullError.empty()) {
                    return buildFailureResult(notNullError, dbName, tableName);
                }

                table.insert(fullValues);

                // 逐行记录插入日志 — NAPH130
                if (core != nullptr && core->getDbLogManager() != nullptr) {
                    nlohmann::json insertSnapshot;
                    insertSnapshot["__primary_key__"] = fullValues.front();
                    insertSnapshot["values"] = fullValues;
                    if (!columnNames.empty()) {
                        nlohmann::json columnMap;
                        for (std::size_t i = 0; i < columnNames.size() && i < values.size(); ++i) {
                            columnMap[columnNames[i]] = values[i];
                        }
                        insertSnapshot["columns"] = columnMap;
                    }
                    core->getDbLogManager()->logInsert(
                        dbName, tableName,
                        insertSnapshot.dump(),
                        "INSERT INTO " + tableName + " VALUES (...)"
                    );
                }
            } catch (const std::exception &exception) {
                LogWriter::error("executor", "InsertExecutor", "executeInsert",
                                 std::string("Insert into ") + dbName + "." + tableName + " failed: " + exception.what());
                return buildFailureResult(std::string("Insert failed: ") + exception.what(), dbName, tableName);
            }
        }
        ++totalInserted;
    }

    LogWriter::info("executor", "InsertExecutor", "executeInsert",
                    "Inserted " + std::to_string(totalInserted) + " row(s) into " + dbName + "." + tableName + ".");
    ExecutionResult result = buildSuccessResult("Insert succeeded.", dbName, tableName);
    result.setAffectedRows(totalInserted);
    return result;
}

bool InsertExecutor::validateInsertStmt(const InsertStmt *insertStmt) const
{
    if (insertStmt == nullptr) {
        return false;
    }

    return insertStmt->getColumnNames().empty()
           || insertStmt->getColumnNames().size() == insertStmt->getValues().size();
}