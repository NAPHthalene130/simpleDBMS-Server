#pragma once

#include <memory>
#include <string>

#include "PlanNode.h"
#include "models/binder/BindResult.h"

class Core;

/**
 * @class Planner
 * @brief 执行计划生成器
 * @details 将 Binder 输出的 Bound 语义信息转换为可执行的 PlanNode 计划树。
 * @author NAPH130
 */
class Planner {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit Planner(Core *core);

    /**
     * @brief 根据绑定结果生成 SELECT 查询计划树
     * @author NAPH130
     * @param bindResult Binder 输出的绑定结果
     * @param dbName 当前数据库名
     * @return 计划树根节点
     */
    std::shared_ptr<PlanNode> planSelect(const BindResult &bindResult, const std::string &dbName);

private:
    /**
     * @brief 构建全表扫描节点
     * @author NAPH130
     * @param dbName 数据库名
     * @param tableName 表名
     * @param alias 表别名
     * @return SeqScan 计划节点
     */
    std::shared_ptr<SeqScanPlanNode> buildSeqScan(const std::string &dbName,
                                                    const std::string &tableName,
                                                    const std::string &alias);

    /**
     * @brief 构建 JOIN 计划子树
     * @author NAPH130
     * @param leftChild 左侧子计划（基础表扫描等）
     * @param joinRef JOIN 绑定信息
     * @param dbName 数据库名
     * @param rightSchema 右侧表 schema
     * @return JOIN 计划子树根节点
     */
    std::shared_ptr<NestedLoopJoinPlanNode> buildJoin(
        std::shared_ptr<PlanNode> leftChild,
        const BoundJoinRef &joinRef,
        const std::string &dbName,
        const storage::TableSchema &rightSchema);

    Core *core;
};
