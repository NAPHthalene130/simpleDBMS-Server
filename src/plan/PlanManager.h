#pragma once

class Core;
class Planner;

/**
 * @class PlanManager
 * @brief 执行计划模块管理器
 * @details 统一管理 Plan 层组件的生命周期，作为模块对外的唯一入口。
 * @author NAPH130
 */
class PlanManager {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit PlanManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~PlanManager();

    /**
     * @brief 获取 Planner 实例
     * @author NAPH130
     * @return Planner 对象指针
     */
    Planner *getPlanner() const;

private:
    Core *core;
    Planner *planner;
};
