#include "Planner.h"

#include "Core.h"
#include "log/LogWriter.h"

Planner::Planner(Core *core) : core(core) {
}

std::shared_ptr<PlanNode> Planner::planSelect(const BindResult &bindResult, const std::string &dbName) {
    if (!bindResult.success) {
        return nullptr;
    }

    // 1. 构建 FROM 主表扫描节点
    // 作者：NAPH130
    if (bindResult.tableRefs.empty()) {
        return nullptr;
    }

    const auto &mainTable = bindResult.tableRefs.front();
    std::shared_ptr<PlanNode> currentRoot = buildSeqScan(dbName, mainTable.tableName, mainTable.alias);

    // 2. 逐层叠加 JOIN 节点
    // 作者：NAPH130
    for (std::size_t i = 0; i < bindResult.joinRefs.size(); ++i) {
        const auto &joinRef = bindResult.joinRefs[i];
        // 确定右侧表的 schema（从 tableRefs 中查找，索引为 i+1）
        // 作者：NAPH130
        storage::TableSchema rightSchema;
        if (i + 1 < bindResult.tableRefs.size()) {
            rightSchema = bindResult.tableRefs[i + 1].schema;
        }

        currentRoot = buildJoin(currentRoot, joinRef, dbName, rightSchema);
    }

    // 3. 叠加 WHERE 过滤节点
    // 作者：NAPH130
    if (bindResult.whereCondition != nullptr) {
        auto filterNode = std::make_shared<FilterPlanNode>();
        filterNode->condition = bindResult.whereCondition;
        filterNode->addChild(currentRoot);
        currentRoot = filterNode;
    }

    // 4. 叠加聚合节点
    // 作者：NAPH130
    if (!bindResult.aggregateExprs.empty()) {
        auto aggregationNode = std::make_shared<AggregationPlanNode>();
        aggregationNode->aggregateExprs = bindResult.aggregateExprs;
        aggregationNode->groupByColumns = bindResult.groupByColumns;
        aggregationNode->outputColumns = bindResult.resolvedTargetColumns;
        aggregationNode->havingCondition = bindResult.havingCondition;
        aggregationNode->addChild(currentRoot);
        currentRoot = aggregationNode;
        LogWriter::info("plan", "Planner", "planSelect",
                        "Aggregation plan node created with " +
                            std::to_string(bindResult.aggregateExprs.size()) + " aggregate(s).");
    }

    // 5. 叠加投影节点
    // 作者：NAPH130
    if (!bindResult.resolvedTargetColumns.empty()) {
        auto projectionNode = std::make_shared<ProjectionPlanNode>();
        projectionNode->projectedColumns = bindResult.resolvedTargetColumns;
        projectionNode->addChild(currentRoot);
        currentRoot = projectionNode;
    }

    LogWriter::info("plan", "Planner", "planSelect", "Plan tree built successfully.");
    return currentRoot;
}

std::shared_ptr<SeqScanPlanNode> Planner::buildSeqScan(const std::string &dbName,
                                                         const std::string &tableName,
                                                         const std::string &alias) {
    auto node = std::make_shared<SeqScanPlanNode>();
    node->dbName = dbName;
    node->tableName = tableName;
    node->tableAlias = alias;
    return node;
}

std::shared_ptr<NestedLoopJoinPlanNode> Planner::buildJoin(
    std::shared_ptr<PlanNode> leftChild,
    const BoundJoinRef &joinRef,
    const std::string &dbName,
    const storage::TableSchema &rightSchema) {
    auto joinNode = std::make_shared<NestedLoopJoinPlanNode>();
    joinNode->joinType = joinRef.joinType;
    joinNode->leftAlias = joinRef.leftAlias;
    joinNode->rightAlias = joinRef.rightAlias;
    joinNode->rightTableName = joinRef.rightTableName;
    joinNode->dbName = dbName;
    joinNode->onCondition = joinRef.onCondition;
    joinNode->rightSchema = std::move(rightSchema);
    joinNode->addChild(leftChild);
    return joinNode;
}
