#pragma once

class Core;

/**
 * @class TableDefManager
 * @brief 表定义管理器
 * @details 负责封装表结构定义相关管理逻辑，当前阶段仅预留类结构。
 * @author NAPH130
 */
class TableDefManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit TableDefManager(Core *core);

private:
    Core *core;
};
