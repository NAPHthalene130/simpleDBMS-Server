#include "DatabaseBlock.h"

DatabaseBlock::DatabaseBlock()
    : name{},
      type(false),
      fileName{},
      createTime()
{
}

const std::array<char, 128> &DatabaseBlock::getName() const
{
    return name;
}

void DatabaseBlock::setName(const std::array<char, 128> &name)
{
    this->name = name;
}

bool DatabaseBlock::getType() const
{
    return type;
}

void DatabaseBlock::setType(bool type)
{
    this->type = type;
}

const std::array<char, 256> &DatabaseBlock::getFileName() const
{
    return fileName;
}

void DatabaseBlock::setFileName(const std::array<char, 256> &fileName)
{
    this->fileName = fileName;
}

const DateTime &DatabaseBlock::getCreateTime() const
{
    return createTime;
}

void DatabaseBlock::setCreateTime(const DateTime &createTime)
{
    this->createTime = createTime;
}
