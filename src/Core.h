#pragma once

#include "network/NetReceiver.h"
#include "network/NetSender.h"

/**
 * @class Core
 * @brief 服务端核心调度类
 * @details 负责统一管理网络接收与发送服务的生命周期
 * @author NAPH130
 */
class Core
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     */
    Core();

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~Core();

    /**
     * @brief 启动核心服务
     * @author NAPH130
     */
    void start();

    /**
     * @brief 停止核心服务
     * @author NAPH130
     */
    void stop();

private:
    NetReceiver netReceiver;
    NetSender netSender;
};
