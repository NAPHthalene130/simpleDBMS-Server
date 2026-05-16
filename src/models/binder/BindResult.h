#pragma once

#include <memory>
#include <string>
#include <vector>

#include "models/parser/ConditionNode.h"
#include "models/parser/SelectStmt.h"
#include "storage/object/Table.h"

/**
 * @struct BoundTableRef
 * @brief 绑定后的表引用
 * @details 包含表名、别名与解析后的 schema 信息
 * @author NAPH130
 */
struct BoundTableRef {
    std::string tableName;
    std::string alias;
    storage::TableSchema schema;
};

/**
 * @struct BoundJoinRef
 * @brief 绑定后的 JOIN 子句
 * @details 包含连接类型、左右表别名与 ON 条件
 * @author NAPH130
 */
struct BoundJoinRef {
    std::string joinType;         ///< "INNER", "LEFT", "RIGHT"
    std::string leftAlias;        ///< 左侧表别名
    std::string rightAlias;       ///< 右侧表别名（即 joined table）
    std::string rightTableName;   ///< 右侧表实际名称
    std::shared_ptr<ConditionNode> onCondition;
};

/**
 * @struct BindResult
 * @brief Binder 绑定操作的统一返回结果
 * @details 包含绑定成功标志、错误信息与绑定后的查询信息
 * @author NAPH130
 */
struct BindResult {
    bool success = false;
    std::string errorMessage;

    /* --- 绑定后的表信息 --- */
    std::vector<BoundTableRef> tableRefs;
    std::vector<BoundJoinRef> joinRefs;

    /* --- 绑定后的列与表达式 --- */
    std::vector<std::string> resolvedTargetColumns;
    std::vector<storage::Table::AggregateExpr> aggregateExprs;
    bool selectAllFields = false;

    /* --- 条件与分组 --- */
    std::shared_ptr<ConditionNode> whereCondition;
    std::vector<std::string> groupByColumns;
    std::shared_ptr<ConditionNode> havingCondition;

    /**
     * @brief 静态工厂方法：构建成功结果
     * @author NAPH130
     * @return 成功结果对象
     */
    static BindResult makeSuccess()
    {
        BindResult result;
        result.success = true;
        return result;
    }

    /**
     * @brief 静态工厂方法：构建失败结果
     * @author NAPH130
     * @param errorMessage 错误信息
     * @return 失败结果对象
     */
    static BindResult makeFailure(const std::string &errorMessage)
    {
        BindResult result;
        result.success = false;
        result.errorMessage = errorMessage;
        return result;
    }
};
