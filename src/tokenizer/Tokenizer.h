#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "models/tokenizer/Token.h"

class Core;

/**
 * @class Tokenizer
 * @brief SQL 词法分析器
 * @details 负责将原始 SQL 文本切分为 Token 序列，供后续 Parser 模块消费。
 * @author Qi
 */
class Tokenizer
{
public:
    /**
     * @brief 默认构造函数
     * @author Qi
     */
    explicit Tokenizer(Core *core = nullptr);

    /**
     * @brief 使用 SQL 文本初始化词法分析器
     * @author Qi
     * @param sqlText SQL 原始文本
     */
    Tokenizer(Core *core, const std::string &sqlText);

    /**
     * @brief 重置输入 SQL 文本并回到起始位置
     * @author Qi
     * @param sqlText SQL 原始文本
     */
    void reset(const std::string &sqlText);

    /**
     * @brief 获取当前游标位置
     * @author Qi
     * @return 当前游标偏移量
     */
    std::size_t getCurrentPosition() const;

    /**
     * @brief 判断是否还有可读取字符
     * @author Qi
     * @return 仍有可读取字符返回 true，否则返回 false
     */
    bool hasMoreTokens() const;

    /**
     * @brief 获取下一个 Token（会移动游标）
     * @author Qi
     * @return 解析得到的 Token
     */
    Token nextToken();

    /**
     * @brief 预读下一个 Token（不移动游标）
     * @author Qi
     * @return 解析得到的 Token
     */
    Token peekToken();

    /**
     * @brief 一次性切分全部 Token
     * @author Qi
     * @return Token 序列，最后一个元素为 EndOfFile
     */
    std::vector<Token> tokenize();

private:
    Core *core;
    std::string sqlText;
    std::size_t currentPosition;

    std::unordered_set<std::string> keywords;

    void initializeKeywords();
    void skipIgnoredCharacters();
    void skipSingleLineComment();
    void skipMultiLineComment();

    bool isAtEnd() const;
    char currentChar() const;
    char peekChar(std::size_t offset = 1) const;
    void advance(std::size_t count = 1);

    static bool isIdentifierStartChar(char value);
    static bool isIdentifierChar(char value);
    static bool isDigitChar(char value);
    static bool isWhitespaceChar(char value);

    static std::string toUpperString(const std::string &value);

    Token buildIdentifierOrKeywordToken();
    Token buildNumberToken();
    Token buildStringToken();
    Token buildOperatorOrSymbolToken();
};
