#pragma once

class Core;
class Binder;

/**
 * @class BinderManager
 * @brief 语义绑定模块管理器
 * @details 统一管理 Binder 组件的生命周期，作为模块对外的唯一入口。
 * @author NAPH130
 */
class BinderManager {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit BinderManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~BinderManager();

    /**
     * @brief 获取 Binder 实例
     * @author NAPH130
     * @return Binder 对象指针
     */
    Binder *getBinder() const;

private:
    Core *core;
    Binder *binder;
};
