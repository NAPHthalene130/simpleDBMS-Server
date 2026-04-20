#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "models/tokenizer/Token.h"

/**
 * @class Tokenizer
 * @brief SQL 词法分析器
 * @details 将输入的 SQL 字符串切分为 Token 流，供后续 Parser 消费。
 *          - 忽略空白字符、行注释（-- ...）、块注释（slash-star ... star-slash）
 *          - 关键字识别不区分大小写，统一以大写形式存储在 Token::value 中
 *          - 字符串字面量使用单引号包裹，支持 '' 转义单引号
 *          - 数字字面量支持整数与浮点数
 *          - 标识符支持字母、数字、下划线，首字符不可为数字
 *          - 遇到无法识别的字符时记录错误，不抛出异常
 * @author Qi
 */
class Tokenizer {
public:
    /**
     * @brief 构造函数，传入待分析的 SQL 字符串
     * @author Qi
     * @param sql 待分析的 SQL 字符串
     */
    explicit Tokenizer(std::string sql);

    /**
     * @brief 执行词法分析，返回 Token 列表（含末尾 EndOfFile）
     * @author Qi
     * @return Token 列表的常量引用
     */
    const std::vector<Token>& getTokens() const;

    /**
     * @brief 判断词法分析过程中是否存在错误
     * @author Qi
     * @return 存在无法识别的字符或未闭合字面量时返回 true
     */
    bool hasError() const;

    /**
     * @brief 获取所有词法错误描述列表
     * @author Qi
     * @return 错误信息列表的常量引用
     */
    const std::vector<std::string>& getErrors() const;

private:
    // ---- 词法分析主流程 ----

    /**
     * @brief 词法分析主循环，填充 m_tokens
     * @author Qi
     */
    void tokenize();

    // ---- 字符读取辅助 ----

    /**
     * @brief 返回当前字符（不移动游标），末尾返回 '\0'
     * @author Qi
     * @return 当前字符
     */
    char peek() const;

    /**
     * @brief 返回下一个字符（不移动游标），末尾返回 '\0'
     * @author Qi
     * @return 前看一位字符
     */
    char peekNext() const;

    /**
     * @brief 消费当前字符，游标前进一位，并维护行列号
     * @author Qi
     * @return 被消费的字符
     */
    char advance();

    /**
     * @brief 若当前字符与期望字符匹配则消费并返回 true，否则返回 false
     * @author Qi
     * @param expected 期望字符
     * @return 是否匹配并消费
     */
    bool match(char expected);

    /**
     * @brief 判断游标是否已到达输入末尾
     * @author Qi
     * @return 已到末尾返回 true
     */
    bool isAtEnd() const;

    // ---- 跳过空白与注释 ----

    /**
     * @brief 跳过空白字符，维护行列号
     * @author Qi
     */
    void skipWhitespace();

    /**
     * @brief 跳过行注释（-- 直到行尾），调用前已消费 "--"
     * @author Qi
     */
    void skipLineComment();

    /**
     * @brief 跳过块注释（slash-star ... star-slash），调用前已消费 "slash-star"
     * @author Qi
     * @note 若未闭合则记录错误
     */
    void skipBlockComment();

    // ---- 具体 Token 扫描 ----

    /**
     * @brief 扫描数字字面量（整数或浮点数），并将 Token 加入 m_tokens
     * @author Qi
     * @param startPos 当前 Token 在 m_sql 中的起始下标
     */
    void scanNumber(size_t startPos);

    /**
     * @brief 扫描字符串字面量（单引号包裹，'' 转义），并将 Token 加入 m_tokens
     * @author Qi
     * @param startPos 当前 Token 在 m_sql 中的起始下标（含开头引号）
     */
    void scanString(size_t startPos);

    /**
     * @brief 扫描标识符或关键字，并将 Token 加入 m_tokens
     * @author Qi
     * @param startPos 当前 Token 在 m_sql 中的起始下标
     */
    void scanIdentifierOrKeyword(size_t startPos);

    // ---- Token 生成与错误记录 ----

    /**
     * @brief 向 m_tokens 添加一个 Token
     * @author Qi
     * @param type  Token 类型
     * @param value Token 的值（词素）
     */
    void addToken(TokenType type, std::string value);

    /**
     * @brief 记录一条词法错误
     * @author Qi
     * @param msg 错误描述信息
     */
    void recordError(std::string msg);

    // ---- 静态工具 ----

    /**
     * @brief 将字符串转换为全大写，用于关键字匹配
     * @author Qi
     * @param s 输入字符串
     * @return 大写字符串
     */
    static std::string toUpper(const std::string& s);

    /**
     * @brief 构建关键字集合，用于区分关键字与标识符
     * @author Qi
     * @return 关键字大写字符串集合
     */
    static std::unordered_map<std::string, bool> buildKeywordSet();

    // ---- 成员变量 ----
    std::string              m_sql;            ///< 输入 SQL 字符串
    size_t                   m_pos;            ///< 当前游标位置
    int                      m_line;           ///< 当前行号（从 1 开始）
    int                      m_col;            ///< 当前列号（从 1 开始）
    int                      m_tokenStartLine; ///< 当前 Token 起始行号
    int                      m_tokenStartCol;  ///< 当前 Token 起始列号
    std::vector<Token>       m_tokens;         ///< 输出 Token 列表
    std::vector<std::string> m_errors;         ///< 词法错误列表
    bool                     m_hasError;       ///< 是否存在错误

    /// 关键字集合（静态，所有实例共享）
    static const std::unordered_map<std::string, bool> KEYWORD_SET;
};
