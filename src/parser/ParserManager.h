#pragma once

class Core;
class Parser;

/**
 * @class ParserManager
 * @brief 语法分析模块管理器
 * @details 管理 Parser 实例的生命周期，提供统一的语法分析入口。
 * @author YuzhSong
 */
class ParserManager
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     */
    explicit ParserManager(Core *core);

    /**
     * @brief 析构函数
     * @author YuzhSong
     */
    ~ParserManager();

    /**
     * @brief 获取 Parser 实例
     * @author YuzhSong
     * @return Parser 实例指针
     */
    Parser *getParser() const;

private:
    Core *core;
    Parser *parser;
};
