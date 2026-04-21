#include "Token.h"

Token::Token()
    : type(SqlTokenType::Unknown)
{
}

Token::Token(SqlTokenType type, const std::string &value)
    : type(type), value(value)
{
}

SqlTokenType Token::getType() const
{
    return type;
}

void Token::setType(SqlTokenType type)
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
