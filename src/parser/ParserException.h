#pragma once

#include <cstddef>
#include <exception>
#include <string>

/**
 * @class ParserException
 * @brief Parser 语法分析异常
 * @details 在语法分析阶段出现不可继续恢复的错误时抛出，并携带 token 下标。
 * @author YuzhSong
 */
class ParserException : public std::exception
{
public:
    /**
     * @brief 构造 ParserException
     * @author YuzhSong
     * @param message 错误描述信息
     * @param tokenIndex 出错 token 下标
     */
    ParserException(const std::string &message, std::size_t tokenIndex);

    /**
     * @brief 获取错误描述
     * @author YuzhSong
     * @return C 风格错误消息字符串
     */
    const char *what() const noexcept override;

    /**
     * @brief 获取出错 token 下标
     * @author YuzhSong
     * @return token 下标
     */
    std::size_t getTokenIndex() const;

private:
    std::string message;   ///< 错误消息
    std::size_t tokenIndex; ///< 出错 token 下标
};
