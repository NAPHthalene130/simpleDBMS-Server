#pragma once

class Core;

/**
 * @class FileManager
 * @brief 存储文件管理器
 * @details 负责统一管理底层存储文件相关操作，当前阶段仅预留类结构。
 * @author NAPH130
 */
class FileManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit FileManager(Core *core);

private:
    Core *core;
};
