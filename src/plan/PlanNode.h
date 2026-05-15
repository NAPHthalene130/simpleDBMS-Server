#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "models/binder/BindResult.h"
#include "models/parser/ConditionNode.h"
#include "storage/object/Table.h"

/**
 * @enum PlanNodeType
 * @brief 计划节点类型枚举
 * @author NAPH130
 */
enum class PlanNodeType {
    SeqScan,        ///< 全表扫描
    Filter,         ///< 条件过滤
    Projection,     ///< 列投影
    NestedLoopJoin, ///< 嵌套循环连接
    Aggregation,    ///< 聚合计算
    Insert,         ///< 插入
    Delete,         ///< 删除
    Update,         ///< 更新
    CreateDb,       ///< 创建数据库
    CreateTable,    ///< 创建表
    Drop,           ///< 删除对象
    UseDb,          ///< 切换数据库
    Show            ///< 查看元数据
};

/**
 * @class PlanNode
 * @brief 执行计划节点基类
 * @details 描述算子树中的单个算子，子类实现具体执行语义。
 * @author NAPH130
 */
class PlanNode {
public:
    PlanNode(PlanNodeType nodeType) : nodeType(nodeType) {}
    virtual ~PlanNode() = default;

    PlanNodeType getNodeType() const { return nodeType; }

    /**
     * @brief 获取子节点列表
     * @author NAPH130
     * @return 子节点引用列表
     */
    const std::vector<std::shared_ptr<PlanNode>> &getChildren() const { return children; }

    /**
     * @brief 添加子节点
     * @author NAPH130
     * @param child 子计划节点
     */
    void addChild(std::shared_ptr<PlanNode> child) { children.push_back(std::move(child)); }

protected:
    PlanNodeType nodeType;
    std::vector<std::shared_ptr<PlanNode>> children;
};

/**
 * @class SeqScanPlanNode
 * @brief 全表扫描计划节点
 * @author NAPH130
 */
class SeqScanPlanNode : public PlanNode {
public:
    SeqScanPlanNode() : PlanNode(PlanNodeType::SeqScan) {}

    std::string dbName;
    std::string tableName;
    std::string tableAlias;
};

/**
 * @class FilterPlanNode
 * @brief 条件过滤计划节点
 * @author NAPH130
 */
class FilterPlanNode : public PlanNode {
public:
    FilterPlanNode() : PlanNode(PlanNodeType::Filter) {}

    std::shared_ptr<ConditionNode> condition;
};

/**
 * @class ProjectionPlanNode
 * @brief 列投影计划节点
 * @author NAPH130
 */
class ProjectionPlanNode : public PlanNode {
public:
    ProjectionPlanNode() : PlanNode(PlanNodeType::Projection) {}

    std::vector<std::string> projectedColumns;
};

/**
 * @class NestedLoopJoinPlanNode
 * @brief 嵌套循环连接计划节点
 * @author NAPH130
 */
class NestedLoopJoinPlanNode : public PlanNode {
public:
    NestedLoopJoinPlanNode() : PlanNode(PlanNodeType::NestedLoopJoin) {}

    std::string joinType;                              ///< "INNER", "LEFT", "RIGHT"
    std::string leftAlias;
    std::string rightAlias;
    std::string rightTableName;
    std::string dbName;
    std::shared_ptr<ConditionNode> onCondition;
    storage::TableSchema leftSchema;
    storage::TableSchema rightSchema;
};

/**
 * @class AggregationPlanNode
 * @brief 聚合计算计划节点
 * @author NAPH130
 */
class AggregationPlanNode : public PlanNode {
public:
    AggregationPlanNode() : PlanNode(PlanNodeType::Aggregation) {}

    std::vector<storage::Table::AggregateExpr> aggregateExprs;
    std::vector<std::string> groupByColumns;
    std::vector<std::string> outputColumns;
    std::shared_ptr<ConditionNode> havingCondition;
};
