#pragma once

#include <string>
#include <vector>

class Core;

#include "models/storage/TableBlock.h"
#include "storage/object/Table.h"

/**
 * @class DatabaseManager
 * @brief 数据库管理器
 * @details 负责封装当前数据库内的数据表管理逻辑，当前阶段仅预留接口定义。
 * @author NAPH130
 */
class DatabaseManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit DatabaseManager(Core *core);

    /**
     * @brief 创建数据表
     * @author NAPH130
     * @param tbInfo 数据表元信息
     * @return 是否创建成功
     * @note 需要校验表名长度、表是否已存在，并完成相关物理文件与元数据写入
     */
    bool createTable(TableBlock tbInfo);
    bool createTable(const std::string &dbName,
                     const std::string &tableName,
                     const std::vector<std::string> &columns,
                     const std::vector<storage::ColumnMeta> &columnMetas = {});
    bool createTable(const std::string &dbName,
                     const std::string &tableName,
                     const std::vector<storage::Table::ColumnDefinition> &columns);
    bool insertRow(const std::string &dbName,
                   const std::string &tableName,
                   const std::vector<std::string> &values);
    bool updateRowByPrimaryKey(const std::string &dbName,
                               const std::string &tableName,
                               const std::string &primaryKey,
                               const std::vector<std::string> &newValues);
    bool deleteRowByPrimaryKey(const std::string &dbName,
                               const std::string &tableName,
                               const std::string &primaryKey);
    bool addColumnConstraint(const std::string &dbName,
                             const std::string &tableName,
                             const storage::Table::ColumnConstraintSpec &constraint);
    std::vector<storage::Row> selectRows(const std::string &dbName,
                                         const std::string &tableName,
                                         const std::vector<std::string> &targetColumns,
                                         const std::vector<storage::Table::WhereCondition> &whereConditions = {},
                                         const std::vector<storage::Table::QueryConstraint> &queryConstraints = {},
                                         const storage::Table::SelectOptions &options = storage::Table::SelectOptions());

    /**
     * @brief 删除数据表
     * @author NAPH130
     * @param tableName 表名称
     * @return 是否删除成功
     * @note 需要删除表元数据记录及相关物理文件
     */
    bool dropTable(std::string tableName);

    /**
     * @brief 修改数据表信息
     * @author NAPH130
     * @param tableName 原表名称
     * @param newTbInfo 新的数据表元信息
     * @return 是否修改成功
     * @note 需要更新字段数量、记录数量与最后修改时间，并回写到元数据文件
     */
    bool modifyTable(std::string tableName, TableBlock newTbInfo);

    /**
     * @brief 获取指定数据表信息
     * @author NAPH130
     * @param tableName 表名称
     * @return 指定表的元数据对象
     * @note 当前仅预留接口，暂未实现
     */
    TableBlock getTableInfo(std::string tableName);

    /**
     * @brief 获取当前数据库内所有数据表信息
     * @author NAPH130
     * @return 数据表元信息列表
     */
    std::vector<TableBlock> getAllTables();

    /**
     * @brief 获取指定数据库内所有数据表信息
     * @author NAPH130
     * @param dbName 数据库名称
     * @return 数据表元信息列表
     */
    std::vector<TableBlock> getAllTablesForDb(const std::string &dbName);

    /**
     * @brief 获取指定数据表的所有列名
     * @author NAPH130
     * @param dbName 数据库名称
     * @param tableName 表名称
     * @return 列名列表
     */
    std::vector<std::string> getTableColumns(const std::string &dbName, const std::string &tableName);

private:
    Core *core;
};
