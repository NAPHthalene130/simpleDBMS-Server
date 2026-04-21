#include "ParserException.h"

ParserException::ParserException(const std::string &message, const std::size_t tokenIndex)
    : message(message), tokenIndex(tokenIndex)
{
}

const char *ParserException::what() const noexcept
{
    return message.c_str();
}

std::size_t ParserException::getTokenIndex() const
{
    return tokenIndex;
}
