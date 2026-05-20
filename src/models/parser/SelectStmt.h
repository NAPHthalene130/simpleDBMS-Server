#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ConditionNode.h"
#include "SQLStatement.h"

/**
 * @struct JoinInfo
 * @brief 单条 JOIN 子句的解析结果
 * @details 包含连接类型、表名、别名与 ON 条件树
 * @author NAPH130
 */
struct JoinInfo {
    std::string joinType;                              ///< "INNER", "LEFT", "RIGHT"
    std::string tableName;                             ///< 被连接的表名
    std::string alias;                                 ///< 表别名（可为空）
    std::string leftAlias;                             ///< 左侧表别名（可为空）
    std::shared_ptr<ConditionNode> onCondition;        ///< ON 条件树
};

/**
 * @class SelectStmt
 * @brief 查询语句数据类
 * @details 封装 SELECT 语句解析后的目标表、目标字段集合、JOIN 信息与 WHERE 条件树。
 * @author NAPH130
 */
class SelectStmt : public SQLStatement
{
public:
    SelectStmt();

    const std::string &getTableName() const;
    void setTableName(const std::string &tableName);

    const std::vector<std::string> &getTargetFields() const;
    void setTargetFields(const std::vector<std::string> &targetFields);

    bool getSelectAllFields() const;
    void setSelectAllFields(bool selectAllFields);

    const std::shared_ptr<ConditionNode> &getWhereCondition() const;
    void setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition);

    const std::vector<std::string> &getGroupByColumns() const;
    void setGroupByColumns(const std::vector<std::string> &groupByColumns);

    const std::shared_ptr<ConditionNode> &getHavingCondition() const;
    void setHavingCondition(const std::shared_ptr<ConditionNode> &havingCondition);

    /**
     * @brief 获取 JOIN 子句列表
     * @author NAPH130
     * @return JOIN 子句列表
     */
    const std::vector<JoinInfo> &getJoinInfoList() const;
    void addJoinInfo(const JoinInfo &joinInfo);

    /**
     * @brief 是否存在 JOIN 子句
     * @author NAPH130
     * @return 是否存在
     */
    bool hasJoin() const;

    const std::string &getOrderByColumn() const;
    void setOrderByColumn(const std::string &orderByColumn);

    bool getOrderByDesc() const;
    void setOrderByDesc(bool orderByDesc);

    bool getHasLimit() const;
    void setHasLimit(bool hasLimit);

    std::size_t getLimitCount() const;
    void setLimitCount(std::size_t limitCount);

private:
    std::string tableName;
    std::vector<std::string> targetFields;
    bool selectAllFields;
    std::shared_ptr<ConditionNode> whereCondition;
    std::vector<std::string> groupByColumns;
    std::shared_ptr<ConditionNode> havingCondition;
    std::vector<JoinInfo> joinInfoList;
    std::string orderByColumn;
    bool orderByDesc = false;
    bool hasLimit = false;
    std::size_t limitCount = 0;
};
