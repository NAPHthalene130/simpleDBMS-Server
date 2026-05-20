#pragma once

#include <memory>
#include <string>
#include <vector>

#include "models/binder/BindResult.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/SQLStatement.h"
#include "storage/object/StorageCommon.h"

class Core;
class DatabaseManager;
class SystemCatalogManager;

/**
 * @class Binder
 * @brief SQL 语义绑定器
 * @details 负责将 Parser 输出的抽象语法树中的名字解析为系统内真实对象，
 *          包括表名绑定、列名绑定、星号展开、聚合函数识别与类型检查。
 * @author NAPH130
 */
class Binder {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit Binder(Core *core);

    /**
     * @brief 对任意 SQL 语句执行语义绑定
     * @author NAPH130
     * @param statement Parser 输出的 AST 语句对象
     * @param currentDbName 当前生效的数据库名
     * @return 绑定结果
     */
    BindResult bind(const SQLStatement *statement, const std::string &currentDbName);

    /**
     * @brief 对 SELECT 语句执行语义绑定
     * @author NAPH130
     * @param selectStmt SELECT 语句 AST 节点
     * @param currentDbName 当前生效的数据库名
     * @return 绑定结果
     */
    BindResult bindSelect(const SelectStmt *selectStmt, const std::string &currentDbName);

private:
    /**
     * @brief 展开 SELECT * 为显式列名列表
     * @author NAPH130
     * @param selectStmt SELECT 语句节点
     * @param tableRefs 已绑定的表引用列表
     * @return 展开后的列名列表，空列表表示失败
     */
    std::vector<std::string> expandStarColumns(const SelectStmt *selectStmt,
                                                const std::vector<BoundTableRef> &tableRefs);

    /**
     * @brief 解析目标字段中的聚合函数（如 COUNT(*), SUM(col) 等）
     * @author NAPH130
     * @param targetFields 目标字段名称列表
     * @param aggregateExprs 输出聚合表达式列表
     * @param resolvedColumns 输出解析后的列名列表（非聚合列原样保留）
     */
    void parseAggregateExpressions(const std::vector<std::string> &targetFields,
                                   std::vector<storage::Table::AggregateExpr> &aggregateExprs,
                                   std::vector<std::string> &resolvedColumns);

    /**
     * @brief 校验目标列是否存在于指定表引用中
     * @author NAPH130
     * @param columnName 列名
     * @param tableRefs 表引用列表
     * @return 是否存在该列
     */
    bool validateColumnExists(const std::string &columnName,
                              const std::vector<BoundTableRef> &tableRefs) const;

    /**
     * @brief 获取指定名表对应 db 路径下的 schema
     * @author NAPH130
     * @param dbPath 数据库路径
     * @param tableName 表名
     * @return 表 schema，若不存在则返回空 schema
     */
    storage::TableSchema loadTableSchema(const std::filesystem::path &dbPath, const std::string &tableName);

    /**
     * @brief 识别比较操作符字符串对应的 CompareOp
     * @author NAPH130
     * @param opStr 操作符字符串
     * @return CompareOp 枚举值
     */
    static storage::Table::CompareOp mapCompareOp(const std::string &opStr);

    Core *core;
    DatabaseManager *databaseManager;
    SystemCatalogManager *systemCatalogManager;
};
