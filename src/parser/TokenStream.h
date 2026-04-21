#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "models/tokenizer/Token.h"
#include "ParserException.h"

/**
 * @class TokenStream
 * @brief Token 流游标封装器
 * @details 提供 peek、advance、match、expect、consumeOptional 等通用游标能力，供 Parser 语法分析阶段复用。
 * @author YuzhSong
 */
class TokenStream
{
public:
    /**
     * @brief 构造 TokenStream
     * @author YuzhSong
     * @param tokens 输入 token 序列
     */
    explicit TokenStream(const std::vector<Token> &tokens);

    /**
     * @brief 获取当前游标位置
     * @author YuzhSong
     * @return 当前 token 下标
     */
    std::size_t position() const;

    /**
     * @brief 判断是否已经到达流末尾
     * @author YuzhSong
     * @return 若到达末尾则返回 true
     */
    bool isAtEnd() const;

    /**
     * @brief 预览当前或后续 token
     * @author YuzhSong
     * @param offset 相对当前游标的偏移量
     * @return 对应位置的 token 引用；越界时返回 EndOfFile 哨兵 token
     */
    const Token &peek(std::size_t offset = 0) const;

    /**
     * @brief 消费当前 token 并推进游标
     * @author YuzhSong
     * @return 被消费的 token；若已到末尾则返回 EndOfFile 哨兵 token
     */
    const Token &advance();

    /**
     * @brief 若当前 token 类型匹配则消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @return 匹配并消费成功返回 true，否则返回 false
     */
    bool match(TokenType type);

    /**
     * @brief 若当前 token 类型和值都匹配则消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @param value 目标 token 值
     * @param caseInsensitive 是否大小写不敏感比较
     * @return 匹配并消费成功返回 true，否则返回 false
     */
    bool match(TokenType type, const std::string &value, bool caseInsensitive = true);

    /**
     * @brief 可选消费：若当前 token 类型匹配则消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @return 匹配并消费成功返回 true，否则返回 false
     */
    bool consumeOptional(TokenType type);

    /**
     * @brief 可选消费：若当前 token 类型和值都匹配则消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @param value 目标 token 值
     * @param caseInsensitive 是否大小写不敏感比较
     * @return 匹配并消费成功返回 true，否则返回 false
     */
    bool consumeOptional(TokenType type, const std::string &value, bool caseInsensitive = true);

    /**
     * @brief 断言当前 token 类型必须匹配并消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @param message 不匹配时的异常信息
     * @return 被消费的 token 引用
     * @throw ParserException 当 token 不匹配时抛出
     */
    const Token &expect(TokenType type, const std::string &message);

    /**
     * @brief 断言当前 token 类型和值必须匹配并消费
     * @author YuzhSong
     * @param type 目标 token 类型
     * @param value 目标 token 值
     * @param message 不匹配时的异常信息
     * @param caseInsensitive 是否大小写不敏感比较
     * @return 被消费的 token 引用
     * @throw ParserException 当 token 不匹配时抛出
     */
    const Token &expect(
        TokenType type,
        const std::string &value,
        const std::string &message,
        bool caseInsensitive = true);

private:
    const std::vector<Token> &tokens;
    std::size_t cursor;
    Token eofToken;
};
