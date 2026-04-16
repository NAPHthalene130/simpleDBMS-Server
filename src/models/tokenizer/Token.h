#pragma once

#include <string>

/**
 * @enum TokenType
 * @brief 词法单元类型枚举
 * @details 用于标识 Token 在 SQL 文本中的语义类别。
 * @author NAPH130
 */
enum class TokenType
{
    Keyword,
    Identifier,
    Number,
    Operator,
    String,
    Symbol,
    EndOfFile,
    Unknown
};

/**
 * @class Token
 * @brief 词法单元类
 * @details 封装 SQL 字符串切分后的最小语义单元，供词法分析模块输出与语法分析模块消费。
 * @author NAPH130
 */
class Token
{
public:
    Token();
    Token(TokenType type, const std::string &value);

    TokenType getType() const;
    void setType(TokenType type);

    const std::string &getValue() const;
    void setValue(const std::string &value);

private:
    TokenType type;
    std::string value;
};
