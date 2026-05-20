#include "Binder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <unordered_set>

#include "Core.h"
#include "log/LogWriter.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {

/**
 * @brief 将字符串转为大写，用于大小写不敏感比较
 * @author NAPH130
 */
std::string toUpperString(const std::string &value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

/**
 * @brief 聚合函数名集合
 * @author NAPH130
 */
const std::unordered_set<std::string> AGGREGATE_FUNCTIONS = {
    "COUNT", "SUM", "AVG", "MIN", "MAX"
};

} // namespace

Binder::Binder(Core *core) : core(core) {
    if (core != nullptr && core->getStorageManager() != nullptr) {
        databaseManager = core->getStorageManager()->getDatabaseManager();
        systemCatalogManager = core->getStorageManager()->getSystemCatalogManager();
    } else {
        databaseManager = nullptr;
        systemCatalogManager = nullptr;
    }
}

BindResult Binder::bind(const SQLStatement *statement, const std::string &currentDbName) {
    if (statement == nullptr) {
        return BindResult::makeFailure("statement is null");
    }

    if (statement->getStmtType() == ExecutionStatementType::Select) {
        return bindSelect(static_cast<const SelectStmt *>(statement), currentDbName);
    }

    // 非 SELECT 语句无需语义绑定，直接返回成功
    // 作者：NAPH130
    return BindResult::makeSuccess();
}

BindResult Binder::bindSelect(const SelectStmt *selectStmt, const std::string &currentDbName) {
    if (selectStmt == nullptr) {
        return BindResult::makeFailure("selectStmt is null");
    }

    if (currentDbName.empty()) {
        return BindResult::makeFailure("no database selected");
    }

    if (systemCatalogManager == nullptr || databaseManager == nullptr) {
        return BindResult::makeFailure("storage components not initialized");
    }

    const auto dbRootPath = SystemCatalogManager::getDataRootPath();
    const auto dbPath = dbRootPath / currentDbName;

    if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
        return BindResult::makeFailure("database does not exist: " + currentDbName);
    }

    BindResult result = BindResult::makeSuccess();

    // 1. 绑定 FROM 子句主表
    // 作者：NAPH130
    const std::string tableName = selectStmt->getTableName();
    if (tableName.empty()) {
        return BindResult::makeFailure("table name is empty");
    }

    auto schema = loadTableSchema(dbPath, tableName);
    if (schema.name.empty()) {
        return BindResult::makeFailure("table does not exist: " + tableName);
    }

    BoundTableRef mainTableRef;
    mainTableRef.tableName = tableName;
    mainTableRef.alias = tableName;
    mainTableRef.schema = std::move(schema);
    result.tableRefs.push_back(std::move(mainTableRef));

    // 2. 绑定 JOIN 子句
    // 作者：NAPH130
    const auto &joinInfoList = selectStmt->getJoinInfoList();
    LogWriter::info("binder", "Binder", "bindSelect",
                    "JOIN count=" + std::to_string(joinInfoList.size())
                        + " hasJoin=" + std::to_string(selectStmt->hasJoin())
                        + " for table=" + tableName);
    for (const auto &joinInfo : joinInfoList) {
        auto joinSchema = loadTableSchema(dbPath, joinInfo.tableName);
        if (joinSchema.name.empty()) {
            return BindResult::makeFailure("join table does not exist: " + joinInfo.tableName);
        }

        const std::string alias = joinInfo.alias.empty() ? joinInfo.tableName : joinInfo.alias;

        // 检查别名冲突
        // 作者：NAPH130
        for (const auto &existingRef : result.tableRefs) {
            if (existingRef.alias == alias) {
                return BindResult::makeFailure("duplicate table alias: " + alias);
            }
        }

        BoundTableRef joinTableRef;
        joinTableRef.tableName = joinInfo.tableName;
        joinTableRef.alias = alias;
        joinTableRef.schema = std::move(joinSchema);
        result.tableRefs.push_back(std::move(joinTableRef));

        BoundJoinRef boundJoinRef;
        boundJoinRef.joinType = joinInfo.joinType;
        boundJoinRef.leftAlias = result.tableRefs.front().alias;
        if (!joinInfo.leftAlias.empty()) {
            boundJoinRef.leftAlias = joinInfo.leftAlias;
        }
        boundJoinRef.rightAlias = alias;
        boundJoinRef.rightTableName = joinInfo.tableName;
        boundJoinRef.onCondition = joinInfo.onCondition;
        result.joinRefs.push_back(std::move(boundJoinRef));
    }

    // 3. 处理目标字段（含星号展开与聚合函数识别）
    // 作者：NAPH130
    result.selectAllFields = selectStmt->getSelectAllFields();
    if (result.selectAllFields) {
        result.resolvedTargetColumns = expandStarColumns(selectStmt, result.tableRefs);
        if (result.resolvedTargetColumns.empty()) {
            return BindResult::makeFailure("star expansion failed");
        }
    } else {
        const auto &targetFields = selectStmt->getTargetFields();
        parseAggregateExpressions(targetFields, result.aggregateExprs, result.resolvedTargetColumns);
    }

    // 4. 传递条件树
    // 作者：NAPH130
    result.whereCondition = selectStmt->getWhereCondition();
    result.groupByColumns = selectStmt->getGroupByColumns();
    result.havingCondition = selectStmt->getHavingCondition();

    LogWriter::info("binder", "Binder", "bindSelect",
                    "Bind succeeded: " + std::to_string(result.tableRefs.size()) + " table(s), "
                        + std::to_string(result.resolvedTargetColumns.size()) + " column(s), "
                        + std::to_string(result.aggregateExprs.size()) + " aggregate(s).");
    return result;
}

std::vector<std::string> Binder::expandStarColumns(const SelectStmt *selectStmt,
                                                     const std::vector<BoundTableRef> &tableRefs) {
    // 元数据查询（FROM DATABASE／FROM TABLE）不展开
    // 作者：NAPH130
    const std::string tableNameUpper = toUpperString(selectStmt->getTableName());
    if (tableNameUpper == "DATABASE" || tableNameUpper == "DATABASES"
        || tableNameUpper == "TABLE" || tableNameUpper == "TABLES") {
        return {"*"};
    }

    std::vector<std::string> allColumns;
    for (const auto &ref : tableRefs) {
        for (const auto &col : ref.schema.columns) {
            // 多表时加表别名前缀以避免歧义
            // 作者：NAPH130
            if (tableRefs.size() > 1) {
                allColumns.push_back(ref.alias + "." + col);
            } else {
                allColumns.push_back(col);
            }
        }
    }
    return allColumns;
}

void Binder::parseAggregateExpressions(const std::vector<std::string> &targetFields,
                                        std::vector<storage::Table::AggregateExpr> &aggregateExprs,
                                        std::vector<std::string> &resolvedColumns) {
    // 匹配聚合函数模式：FUNC(arg) 或 FUNC(*)
    // 作者：NAPH130
    const std::regex aggregateRegex(R"(^\s*(\w+)\s*\(\s*(\*|\w+)\s*\)\s*$)",
                                    std::regex_constants::icase);

    for (const auto &field : targetFields) {
        std::smatch match;
        if (std::regex_match(field, match, aggregateRegex)) {
            std::string funcName = toUpperString(match[1].str());
            std::string columnArg = match[2].str();

            if (AGGREGATE_FUNCTIONS.find(funcName) != AGGREGATE_FUNCTIONS.end()) {
                storage::Table::AggregateExpr expr;
                if (funcName == "COUNT") {
                    expr.op = storage::Table::AggregateOp::COUNT;
                } else if (funcName == "SUM") {
                    expr.op = storage::Table::AggregateOp::SUM;
                } else if (funcName == "AVG") {
                    expr.op = storage::Table::AggregateOp::AVG;
                } else if (funcName == "MIN") {
                    expr.op = storage::Table::AggregateOp::MIN;
                } else if (funcName == "MAX") {
                    expr.op = storage::Table::AggregateOp::MAX;
                }
                expr.column = columnArg;
                aggregateExprs.push_back(expr);

                // 将聚合函数名作为输出列名
                // 作者：NAPH130
                resolvedColumns.push_back(field);
            } else {
                resolvedColumns.push_back(field);
            }
        } else {
            resolvedColumns.push_back(field);
        }
    }
}

bool Binder::validateColumnExists(const std::string &columnName,
                                   const std::vector<BoundTableRef> &tableRefs) const {
    // 列名可能带表别名前缀：alias.column
    // 作者：NAPH130
    const auto dotPos = columnName.find('.');
    if (dotPos != std::string::npos) {
        const std::string alias = columnName.substr(0, dotPos);
        const std::string colName = columnName.substr(dotPos + 1);

        for (const auto &ref : tableRefs) {
            if (ref.alias == alias) {
                const auto &cols = ref.schema.columns;
                return std::find(cols.begin(), cols.end(), colName) != cols.end();
            }
        }
        return false;
    }

    // 无别名前缀时查找任意表
    // 作者：NAPH130
    for (const auto &ref : tableRefs) {
        const auto &cols = ref.schema.columns;
        if (std::find(cols.begin(), cols.end(), columnName) != cols.end()) {
            return true;
        }
    }
    return false;
}

storage::TableSchema Binder::loadTableSchema(const std::filesystem::path &dbPath,
                                               const std::string &tableName) {
    try {
        const auto metaPath = dbPath / (tableName + ".tdf");
        if (!std::filesystem::exists(metaPath)) {
            return {};
        }
        auto table = storage::Table::load(dbPath, tableName);
        return table.schema();
    } catch (const std::exception &e) {
        LogWriter::warning("binder", "Binder", "loadTableSchema",
                           "Failed to load schema for " + tableName + ": " + e.what());
        return {};
    }
}

storage::Table::CompareOp Binder::mapCompareOp(const std::string &opStr) {
    if (opStr == "=") {
        return storage::Table::CompareOp::EQ;
    }
    if (opStr == "<>" || opStr == "!=") {
        return storage::Table::CompareOp::NE;
    }
    if (opStr == ">") {
        return storage::Table::CompareOp::GT;
    }
    if (opStr == ">=") {
        return storage::Table::CompareOp::GE;
    }
    if (opStr == "<") {
        return storage::Table::CompareOp::LT;
    }
    if (opStr == "<=") {
        return storage::Table::CompareOp::LE;
    }
    if (opStr == "LIKE") {
        return storage::Table::CompareOp::LIKE;
    }
    return storage::Table::CompareOp::EQ;
}
