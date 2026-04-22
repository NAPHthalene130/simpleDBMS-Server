#pragma once

#include <memory>
#include <string>

#include "ConditionNode.h"
#include "SQLStatement.h"

/**
 * @class DeleteStmt
 * @brief DELETE 语句数据类
 * @details 封装 DELETE FROM 语句中的目标表与可选 WHERE 条件树。
 * @author YuzhSong
 */
class DeleteStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     */
    DeleteStmt();

    /**
     * @brief 获取目标表名
     * @author YuzhSong
     * @return 目标表名
     */
    const std::string &getTableName() const;

    /**
     * @brief 设置目标表名
     * @author YuzhSong
     * @param tableName 目标表名
     */
    void setTableName(const std::string &tableName);

    /**
     * @brief 获取 WHERE 条件树
     * @author YuzhSong
     * @return WHERE 条件树根节点
     */
    const std::shared_ptr<ConditionNode> &getWhereCondition() const;

    /**
     * @brief 设置 WHERE 条件树
     * @author YuzhSong
     * @param whereCondition WHERE 条件树根节点
     */
    void setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition);

private:
    std::string tableName;
    std::shared_ptr<ConditionNode> whereCondition;
};
