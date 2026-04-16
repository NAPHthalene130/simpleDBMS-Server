#include "Token.h"

Token::Token()
    : type(TokenType::Unknown)
{
}

Token::Token(TokenType type, const std::string &value)
    : type(type), value(value)
{
}

TokenType Token::getType() const
{
    return type;
}

void Token::setType(TokenType type)
{
    this->type = type;
}

const std::string &Token::getValue() const
{
    return value;
}

void Token::setValue(const std::string &value)
{
    this->value = value;
}
