#include "FieldBlock.h"

FieldBlock::FieldBlock()
    : order(0),
      name{},
      type(0),
      param(0),
      modifyTime(),
      integrities(0)
{
}

std::int32_t FieldBlock::getOrder() const
{
    return order;
}

void FieldBlock::setOrder(std::int32_t order)
{
    this->order = order;
}

const std::array<char, 128> &FieldBlock::getName() const
{
    return name;
}

void FieldBlock::setName(const std::array<char, 128> &name)
{
    this->name = name;
}

std::int32_t FieldBlock::getType() const
{
    return type;
}

void FieldBlock::setType(std::int32_t type)
{
    this->type = type;
}

std::int32_t FieldBlock::getParam() const
{
    return param;
}

void FieldBlock::setParam(std::int32_t param)
{
    this->param = param;
}

const DateTime &FieldBlock::getModifyTime() const
{
    return modifyTime;
}

void FieldBlock::setModifyTime(const DateTime &modifyTime)
{
    this->modifyTime = modifyTime;
}

std::int32_t FieldBlock::getIntegrities() const
{
    return integrities;
}

void FieldBlock::setIntegrities(std::int32_t integrities)
{
    this->integrities = integrities;
}

void FieldBlock::addIntegrityFlag(std::int32_t flag)
{
    this->integrities |= flag;
}

const std::string &FieldBlock::getDefaultValue() const
{
    return defaultValue;
}

void FieldBlock::setDefaultValue(const std::string &defaultValue)
{
    this->defaultValue = defaultValue;
}
