#include "Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================
// 内部日志工具（仅此文件使用）
// 格式：[YYYY-MM-DD HH:MM:SS][LEVEL] Tokenizer::<msg>
// ============================================================
namespace {

/**
 * @brief 获取当前时间戳字符串
 * @return 格式为 "YYYY-MM-DD HH:MM:SS" 的字符串
 */
std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void logDebug(const std::string& msg) {
    std::cout << "[" << currentTimestamp() << "][DEBUG] " << msg << "\n";
}

void logWarning(const std::string& msg) {
    std::cerr << "[" << currentTimestamp() << "][WARNING] " << msg << "\n";
}

void logError(const std::string& msg) {
    std::cerr << "[" << currentTimestamp() << "][ERROR] " << msg << "\n";
}

} // namespace

// ============================================================
// 关键字集合（静态定义）
// 所有关键字统一以大写形式存储，匹配时将输入转为大写后查表
// ============================================================
const std::unordered_map<std::string, bool> Tokenizer::KEYWORD_SET =
    Tokenizer::buildKeywordSet();

// ============================================================
// Tokenizer 实现
// ============================================================

Tokenizer::Tokenizer(std::string sql)
    : m_sql(std::move(sql)),
      m_pos(0),
      m_line(1),
      m_col(1),
      m_tokenStartLine(1),
      m_tokenStartCol(1),
      m_hasError(false) {
    logDebug("Tokenizer::Tokenizer() - start tokenizing, input length = "
             + std::to_string(m_sql.size()));
    tokenize();
    logDebug("Tokenizer::Tokenizer() - done, token count = "
             + std::to_string(m_tokens.size()));
}

const std::vector<Token>& Tokenizer::getTokens() const {
    return m_tokens;
}

bool Tokenizer::hasError() const {
    return m_hasError;
}

const std::vector<std::string>& Tokenizer::getErrors() const {
    return m_errors;
}

// ---- 字符读取辅助 ----

char Tokenizer::peek() const {
    if (isAtEnd()) return '\0';
    return m_sql[m_pos];
}

char Tokenizer::peekNext() const {
    if (m_pos + 1 >= m_sql.size()) return '\0';
    return m_sql[m_pos + 1];
}

char Tokenizer::advance() {
    char c = m_sql[m_pos++];
    if (c == '\n') {
        ++m_line;
        m_col = 1;
    } else {
        ++m_col;
    }
    return c;
}

bool Tokenizer::match(char expected) {
    if (isAtEnd()) return false;
    if (m_sql[m_pos] != expected) return false;
    advance();
    return true;
}

bool Tokenizer::isAtEnd() const {
    return m_pos >= m_sql.size();
}

// ---- 跳过空白与注释 ----

void Tokenizer::skipWhitespace() {
    while (!isAtEnd() && std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
    }
}

void Tokenizer::skipLineComment() {
    // 已消费 "--"，跳过直到行尾
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

void Tokenizer::skipBlockComment() {
    // 已消费 "/*"，跳过直到 "*/"
    while (!isAtEnd()) {
        if (peek() == '*' && peekNext() == '/') {
            advance(); // 消费 '*'
            advance(); // 消费 '/'
            return;
        }
        advance();
    }
    // 到达末尾仍未找到 "*/"，记录错误
    recordError("Unterminated block comment starting at line "
                + std::to_string(m_tokenStartLine) + ":"
                + std::to_string(m_tokenStartCol));
}

// ---- 具体 Token 扫描 ----

void Tokenizer::scanNumber(size_t startPos) {
    // 继续消费整数部分（第一个数字已被 tokenize() 消费）
    while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    // 判断是否为浮点数
    if (!isAtEnd() && peek() == '.'
        && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance(); // 消费 '.'
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    std::string lexeme = m_sql.substr(startPos, m_pos - startPos);
    addToken(TokenType::Number, lexeme);
}

void Tokenizer::scanString(size_t startPos) {
    // 已消费开头 '\''，扫描直到遇到未转义的结束引号
    while (!isAtEnd()) {
        char c = peek();
        if (c == '\'') {
            advance(); // 消费当前 '\''
            if (!isAtEnd() && peek() == '\'') {
                // '' 是对单引号的转义，继续扫描
                advance();
            } else {
                // 字符串正常结束
                std::string lexeme = m_sql.substr(startPos, m_pos - startPos);
                addToken(TokenType::String, lexeme);
                return;
            }
        } else {
            advance();
        }
    }
    // 到达末尾仍未找到结束引号
    recordError("Unterminated string literal starting at line "
                + std::to_string(m_tokenStartLine) + ":"
                + std::to_string(m_tokenStartCol));
    // 将已扫描内容作为 Unknown 加入，方便 Parser 定位错误
    std::string lexeme = m_sql.substr(startPos, m_pos - startPos);
    addToken(TokenType::Unknown, lexeme);
}

void Tokenizer::scanIdentifierOrKeyword(size_t startPos) {
    // 继续消费标识符字符（第一个字符已被 tokenize() 消费）
    while (!isAtEnd()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            advance();
        } else {
            break;
        }
    }

    std::string lexeme = m_sql.substr(startPos, m_pos - startPos);
    std::string upper  = toUpper(lexeme);

    // 在关键字集合中查找，匹配则为关键字，否则为标识符
    // 关键字统一以大写形式存储在 Token::value 中，便于 Parser 直接比较
    if (KEYWORD_SET.count(upper) > 0) {
        addToken(TokenType::Keyword, upper);
    } else {
        addToken(TokenType::Identifier, lexeme);
    }
}

// ---- Token 生成与错误记录 ----

void Tokenizer::addToken(TokenType type, std::string value) {
    m_tokens.emplace_back(type, std::move(value));
}

void Tokenizer::recordError(std::string msg) {
    m_hasError = true;
    m_errors.push_back(msg);
    logError("Tokenizer::recordError() - " + msg);
}

// ---- 静态工具 ----

std::string Tokenizer::toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::unordered_map<std::string, bool> Tokenizer::buildKeywordSet() {
    std::unordered_map<std::string, bool> set;

    // DML 关键字
    set["SELECT"]     = true;
    set["FROM"]       = true;
    set["WHERE"]      = true;
    set["INSERT"]     = true;
    set["INTO"]       = true;
    set["VALUES"]     = true;
    set["UPDATE"]     = true;
    set["SET"]        = true;
    set["DELETE"]     = true;

    // DDL 关键字
    set["CREATE"]     = true;
    set["DROP"]       = true;
    set["ALTER"]      = true;
    set["TABLE"]      = true;
    set["DATABASE"]   = true;
    set["ADD"]        = true;
    set["COLUMN"]     = true;
    set["MODIFY"]     = true;
    set["INDEX"]      = true;
    set["ON"]         = true;

    // 完整性约束关键字
    set["PRIMARY"]    = true;
    set["KEY"]        = true;
    set["FOREIGN"]    = true;
    set["REFERENCES"] = true;
    set["NOT"]        = true;
    set["NULL"]       = true;
    set["UNIQUE"]     = true;
    set["DEFAULT"]    = true;
    set["CHECK"]      = true;
    set["IDENTITY"]   = true;

    // 条件与逻辑关键字
    set["AND"]        = true;
    set["OR"]         = true;
    set["IN"]         = true;
    set["LIKE"]       = true;
    set["IS"]         = true;
    set["ASC"]        = true;
    set["DESC"]       = true;

    // 排序、分组与其他子句关键字
    set["ORDER"]      = true;
    set["BY"]         = true;
    set["LIMIT"]      = true;
    set["GROUP"]      = true;
    set["HAVING"]     = true;
    set["DISTINCT"]   = true;
    set["AS"]         = true;

    // JOIN 关键字
    set["JOIN"]       = true;
    set["INNER"]      = true;
    set["LEFT"]       = true;
    set["RIGHT"]      = true;
    set["OUTER"]      = true;

    // 聚合函数关键字
    set["COUNT"]      = true;
    set["SUM"]        = true;
    set["AVG"]        = true;
    set["MAX"]        = true;
    set["MIN"]        = true;

    // 数据类型关键字（与需求文档 §3.12.1 对应）
    set["INTEGER"]    = true;
    set["BOOL"]       = true;
    set["DOUBLE"]     = true;
    set["VARCHAR"]    = true;
    set["DATETIME"]   = true;

    // 布尔字面量关键字
    set["TRUE"]       = true;
    set["FALSE"]      = true;

    return set;
}

// ---- 词法分析主循环 ----

/**
 * @brief 词法分析主循环
 * @details 每轮迭代消费一个 Token。复杂 Token（数字、字符串、标识符/关键字）
 *          委托给对应的 scan* 私有方法处理，单字符和双字符 Token 在此直接处理。
 * @author Qi
 */
void Tokenizer::tokenize() {
    while (true) {
        // 跳过空白
        skipWhitespace();
        if (isAtEnd()) break;

        // 跳过行注释 "--"
        if (peek() == '-' && peekNext() == '-') {
            m_tokenStartLine = m_line;
            m_tokenStartCol  = m_col;
            advance(); advance();
            skipLineComment();
            continue;
        }

        // 跳过块注释 "/*"
        if (peek() == '/' && peekNext() == '*') {
            m_tokenStartLine = m_line;
            m_tokenStartCol  = m_col;
            advance(); advance();
            skipBlockComment();
            continue;
        }

        // 记录当前 Token 的起始位置
        m_tokenStartLine = m_line;
        m_tokenStartCol  = m_col;
        size_t startPos  = m_pos;

        char c = advance();

        // ---- 单字符标点符号（Symbol 类别）----
        switch (c) {
            case '(': addToken(TokenType::Symbol, "("); continue;
            case ')': addToken(TokenType::Symbol, ")"); continue;
            case ',': addToken(TokenType::Symbol, ","); continue;
            case ';': addToken(TokenType::Symbol, ";"); continue;
            case '.': addToken(TokenType::Symbol, "."); continue;
            case '+': addToken(TokenType::Operator, "+"); continue;
            case '-': addToken(TokenType::Operator, "-"); continue;
            case '*': addToken(TokenType::Operator, "*"); continue;
            default: break;
        }

        // ---- '/' 单独处理（排除块注释后剩余的单斜线）----
        if (c == '/') {
            addToken(TokenType::Operator, "/");
            continue;
        }

        // ---- 可能是双字符的运算符 ----
        if (c == '=') {
            addToken(TokenType::Operator, "=");
            continue;
        }
        if (c == '!') {
            if (match('=')) {
                addToken(TokenType::Operator, "!=");
            } else {
                recordError("Unexpected character '!' at line "
                            + std::to_string(m_tokenStartLine) + ":"
                            + std::to_string(m_tokenStartCol)
                            + ", expected '!='");
                addToken(TokenType::Unknown, "!");
            }
            continue;
        }
        if (c == '<') {
            if (match('='))      addToken(TokenType::Operator, "<=");
            else if (match('>')) addToken(TokenType::Operator, "<>");
            else                 addToken(TokenType::Operator, "<");
            continue;
        }
        if (c == '>') {
            if (match('=')) addToken(TokenType::Operator, ">=");
            else            addToken(TokenType::Operator, ">");
            continue;
        }

        // ---- 字符串字面量（单引号包裹）----
        if (c == '\'') {
            scanString(startPos);
            continue;
        }

        // ---- 数字字面量 ----
        if (std::isdigit(static_cast<unsigned char>(c))) {
            scanNumber(startPos);
            continue;
        }

        // ---- 标识符或关键字 ----
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            scanIdentifierOrKeyword(startPos);
            continue;
        }

        // ---- 无法识别的字符 ----
        std::string unknown(1, c);
        recordError("Unknown character '" + unknown + "' at line "
                    + std::to_string(m_tokenStartLine) + ":"
                    + std::to_string(m_tokenStartCol));
        addToken(TokenType::Unknown, unknown);
    }

    // 追加 EndOfFile 标记
    addToken(TokenType::EndOfFile, "");

    if (m_hasError) {
        logWarning("Tokenizer::tokenize() - completed with "
                   + std::to_string(m_errors.size()) + " error(s)");
    }
}
