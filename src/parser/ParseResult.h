#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "models/parser/SQLStatement.h"

/**
 * @struct ParseResult
 * @brief Parser 统一输出结果
 * @details 用于承载语法分析是否成功、AST 根节点以及错误定位信息。
 * @author YuzhSong
 */
struct ParseResult
{
    /**
     * @brief 语法分析是否成功
     * @author YuzhSong
     */
    bool success;

    /**
     * @brief 成功时返回的 AST 根节点
     * @author YuzhSong
     */
    std::shared_ptr<SQLStatement> statement;

    /**
     * @brief 失败时的错误信息
     * @author YuzhSong
     */
    std::string errorMessage;

    /**
     * @brief 失败时的错误 token 下标
     * @author YuzhSong
     */
    std::size_t errorTokenIndex;

    /**
     * @brief 创建成功结果
     * @author YuzhSong
     * @param statement 语法分析得到的 AST 根节点
     * @return 成功状态的 ParseResult
     */
    static ParseResult makeSuccess(const std::shared_ptr<SQLStatement> &statement)
    {
        return ParseResult{true, statement, "", 0};
    }

    /**
     * @brief 创建失败结果
     * @author YuzhSong
     * @param errorMessage 错误信息
     * @param errorTokenIndex 出错 token 下标
     * @return 失败状态的 ParseResult
     */
    static ParseResult makeFailure(const std::string &errorMessage, std::size_t errorTokenIndex)
    {
        return ParseResult{false, nullptr, errorMessage, errorTokenIndex};
    }
};
