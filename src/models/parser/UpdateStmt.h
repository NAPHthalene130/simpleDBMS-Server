#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ConditionNode.h"
#include "SQLStatement.h"

/**
 * @class UpdateStmt
 * @brief UPDATE 语句数据类
 * @details 封装 UPDATE 语句目标表、SET 字段赋值列表与可选 WHERE 条件树。
 * @author YuzhSong
 */
class UpdateStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     */
    UpdateStmt();

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
     * @brief 获取 SET 左值字段名列表
     * @author YuzhSong
     * @return 左值字段名列表
     */
    const std::vector<std::string> &getColumnNames() const;

    /**
     * @brief 设置 SET 左值字段名列表
     * @author YuzhSong
     * @param columnNames 左值字段名列表
     */
    void setColumnNames(const std::vector<std::string> &columnNames);

    /**
     * @brief 获取 SET 右值列表
     * @author YuzhSong
     * @return 右值列表
     */
    const std::vector<std::string> &getValues() const;

    /**
     * @brief 设置 SET 右值列表
     * @author YuzhSong
     * @param values 右值列表
     */
    void setValues(const std::vector<std::string> &values);

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
    std::vector<std::string> columnNames;
    std::vector<std::string> values;
    std::shared_ptr<ConditionNode> whereCondition;
};
