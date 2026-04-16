#include "NetData.h"

NetData::NetData() = default;

NetData::NetData(const std::string &type, const std::string &content)
    : type(type), content(content)
{
}

const std::string &NetData::getType() const
{
    return type;
}

void NetData::setType(const std::string &type)
{
    this->type = type;
}

const std::string &NetData::getContent() const
{
    return content;
}

void NetData::setContent(const std::string &content)
{
    this->content = content;
}
