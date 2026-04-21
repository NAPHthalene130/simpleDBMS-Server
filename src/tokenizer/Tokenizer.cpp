#include "Tokenizer.h"

#include <cctype>

Tokenizer::Tokenizer()
    : currentPosition(0)
{
    initializeKeywords();
}

Tokenizer::Tokenizer(const std::string &sqlText)
    : sqlText(sqlText),
      currentPosition(0)
{
    initializeKeywords();
}

void Tokenizer::reset(const std::string &sqlText)
{
    this->sqlText = sqlText;
    currentPosition = 0;
}

std::size_t Tokenizer::getCurrentPosition() const
{
    return currentPosition;
}

bool Tokenizer::hasMoreTokens() const
{
    return currentPosition < sqlText.size();
}

Token Tokenizer::nextToken()
{
    skipIgnoredCharacters();
    if (isAtEnd()) {
        return Token(TokenType::EndOfFile, "");
    }

    if (isIdentifierStartChar(currentChar())) {
        return buildIdentifierOrKeywordToken();
    }
    if (isDigitChar(currentChar())) {
        return buildNumberToken();
    }
    if (currentChar() == '\'' || currentChar() == '"') {
        return buildStringToken();
    }
    return buildOperatorOrSymbolToken();
}

Token Tokenizer::peekToken()
{
    const std::size_t backupPosition = currentPosition;
    Token token = nextToken();
    currentPosition = backupPosition;
    return token;
}

std::vector<Token> Tokenizer::tokenize()
{
    std::vector<Token> tokens;
    Token token = nextToken();

    while (token.getType() != TokenType::EndOfFile) {
        tokens.push_back(token);
        token = nextToken();
    }

    tokens.push_back(token);
    return tokens;
}

void Tokenizer::initializeKeywords()
{
    keywords = {
        "CREATE",
        "DATABASE",
        "TABLE",
        "INSERT",
        "INTO",
        "VALUES",
        "SELECT",
        "FROM",
        "WHERE",
        "AND",
        "OR",
        "NOT",
        "PRIMARY",
        "KEY",
        "UNIQUE",
        "NULL",
        "INT",
        "INTEGER",
        "FLOAT",
        "DOUBLE",
        "CHAR",
        "VARCHAR",
        "BOOL",
        "BOOLEAN",
        "DATE",
        "DATETIME",
        "USE"
    };
}

void Tokenizer::skipIgnoredCharacters()
{
    while (!isAtEnd()) {
        if (isWhitespaceChar(currentChar())) {
            advance();
            continue;
        }

        if (currentChar() == '-' && peekChar() == '-') {
            skipSingleLineComment();
            continue;
        }

        if (currentChar() == '/' && peekChar() == '*') {
            skipMultiLineComment();
            continue;
        }

        break;
    }
}

void Tokenizer::skipSingleLineComment()
{
    advance(2);
    while (!isAtEnd() && currentChar() != '\n') {
        advance();
    }
}

void Tokenizer::skipMultiLineComment()
{
    advance(2);
    while (!isAtEnd()) {
        if (currentChar() == '*' && peekChar() == '/') {
            advance(2);
            return;
        }
        advance();
    }
}

bool Tokenizer::isAtEnd() const
{
    return currentPosition >= sqlText.size();
}

char Tokenizer::currentChar() const
{
    return isAtEnd() ? '\0' : sqlText[currentPosition];
}

char Tokenizer::peekChar(std::size_t offset) const
{
    const std::size_t targetIndex = currentPosition + offset;
    return targetIndex < sqlText.size() ? sqlText[targetIndex] : '\0';
}

void Tokenizer::advance(std::size_t count)
{
    currentPosition += count;
    if (currentPosition > sqlText.size()) {
        currentPosition = sqlText.size();
    }
}

bool Tokenizer::isIdentifierStartChar(char value)
{
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

bool Tokenizer::isIdentifierChar(char value)
{
    return isIdentifierStartChar(value) || isDigitChar(value);
}

bool Tokenizer::isDigitChar(char value)
{
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

bool Tokenizer::isWhitespaceChar(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

std::string Tokenizer::toUpperString(const std::string &value)
{
    std::string upperValue = value;
    for (char &character : upperValue) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return upperValue;
}

Token Tokenizer::buildIdentifierOrKeywordToken()
{
    const std::size_t startIndex = currentPosition;
    while (!isAtEnd() && isIdentifierChar(currentChar())) {
        advance();
    }

    const std::string text = sqlText.substr(startIndex, currentPosition - startIndex);
    const std::string upperText = toUpperString(text);
    if (keywords.find(upperText) != keywords.end()) {
        return Token(TokenType::Keyword, upperText);
    }
    return Token(TokenType::Identifier, text);
}

Token Tokenizer::buildNumberToken()
{
    const std::size_t startIndex = currentPosition;
    bool hasDecimalPoint = false;

    while (!isAtEnd()) {
        if (isDigitChar(currentChar())) {
            advance();
            continue;
        }

        if (!hasDecimalPoint && currentChar() == '.') {
            hasDecimalPoint = true;
            advance();
            continue;
        }

        break;
    }

    return Token(TokenType::Number, sqlText.substr(startIndex, currentPosition - startIndex));
}

Token Tokenizer::buildStringToken()
{
    const char quoteChar = currentChar();
    advance();

    std::string text;
    while (!isAtEnd()) {
        if (currentChar() == '\\' && peekChar() == quoteChar) {
            text.push_back(quoteChar);
            advance(2);
            continue;
        }

        if (currentChar() == quoteChar) {
            // 支持 SQL 中重复引号转义，例如 'it''s'
            if (peekChar() == quoteChar) {
                text.push_back(quoteChar);
                advance(2);
                continue;
            }

            advance();
            return Token(TokenType::String, text);
        }

        text.push_back(currentChar());
        advance();
    }

    // 未闭合字符串按 Unknown 处理，避免后续解析器误判。
    return Token(TokenType::Unknown, text);
}

Token Tokenizer::buildOperatorOrSymbolToken()
{
    if (isAtEnd()) {
        return Token(TokenType::EndOfFile, "");
    }

    const char first = currentChar();
    const char second = peekChar();
    if ((first == '<' && second == '=') || (first == '>' && second == '=') || (first == '<' && second == '>')
        || (first == '!' && second == '=') || (first == '|' && second == '|')
        || (first == '&' && second == '&')) {
        std::string value;
        value.push_back(first);
        value.push_back(second);
        advance(2);
        return Token(TokenType::Operator, value);
    }

    if (first == '=' || first == '<' || first == '>' || first == '+' || first == '-' || first == '*'
        || first == '/' || first == '%') {
        advance();
        return Token(TokenType::Operator, std::string(1, first));
    }

    if (first == '(' || first == ')' || first == ',' || first == ';' || first == '.') {
        advance();
        return Token(TokenType::Symbol, std::string(1, first));
    }

    advance();
    return Token(TokenType::Unknown, std::string(1, first));
}
