#include "TokenStream.h"

#include <algorithm>
#include <cctype>

namespace
{
/**
 * @brief 比较两个字符串是否相等
 * @author YuzhSong
 * @param lhs 左值
 * @param rhs 右值
 * @param caseInsensitive 是否忽略大小写
 * @return 相等返回 true，否则返回 false
 */
bool equalsString(const std::string &lhs, const std::string &rhs, const bool caseInsensitive)
{
    if (!caseInsensitive) {
        return lhs == rhs;
    }

    if (lhs.size() != rhs.size()) {
        return false;
    }

    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const char leftChar, const char rightChar) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(leftChar))) ==
               static_cast<char>(std::tolower(static_cast<unsigned char>(rightChar)));
    });
}
} // namespace

TokenStream::TokenStream(const std::vector<Token> &tokens)
    : tokens(tokens), cursor(0), eofToken(TokenType::EndOfFile, "")
{
}

std::size_t TokenStream::position() const
{
    return cursor;
}

bool TokenStream::isAtEnd() const
{
    return cursor >= tokens.size() || peek().getType() == TokenType::EndOfFile;
}

const Token &TokenStream::peek(const std::size_t offset) const
{
    const std::size_t targetIndex = cursor + offset;
    if (targetIndex >= tokens.size()) {
        return eofToken;
    }

    return tokens[targetIndex];
}

const Token &TokenStream::advance()
{
    const Token &currentToken = peek();
    if (!isAtEnd()) {
        ++cursor;
    }

    return currentToken;
}

bool TokenStream::match(const TokenType type)
{
    if (peek().getType() != type) {
        return false;
    }

    advance();
    return true;
}

bool TokenStream::match(const TokenType type, const std::string &value, const bool caseInsensitive)
{
    const Token &currentToken = peek();
    if (currentToken.getType() != type) {
        return false;
    }

    if (!equalsString(currentToken.getValue(), value, caseInsensitive)) {
        return false;
    }

    advance();
    return true;
}

bool TokenStream::consumeOptional(const TokenType type)
{
    return match(type);
}

bool TokenStream::consumeOptional(const TokenType type, const std::string &value, const bool caseInsensitive)
{
    return match(type, value, caseInsensitive);
}

const Token &TokenStream::expect(const TokenType type, const std::string &message)
{
    if (peek().getType() != type) {
        throw ParserException(message, cursor);
    }

    return advance();
}

const Token &TokenStream::expect(
    const TokenType type,
    const std::string &value,
    const std::string &message,
    const bool caseInsensitive)
{
    const Token &currentToken = peek();
    if (currentToken.getType() != type || !equalsString(currentToken.getValue(), value, caseInsensitive)) {
        throw ParserException(message, cursor);
    }

    return advance();
}
