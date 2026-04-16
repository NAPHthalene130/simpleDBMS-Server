#include "IntegrityBlock.h"

IntegrityBlock::IntegrityBlock()
    : name{},
      field{},
      type(0),
      param{}
{
}

const std::array<char, 128> &IntegrityBlock::getName() const
{
    return name;
}

void IntegrityBlock::setName(const std::array<char, 128> &name)
{
    this->name = name;
}

const std::array<char, 128> &IntegrityBlock::getField() const
{
    return field;
}

void IntegrityBlock::setField(const std::array<char, 128> &field)
{
    this->field = field;
}

std::int32_t IntegrityBlock::getType() const
{
    return type;
}

void IntegrityBlock::setType(std::int32_t type)
{
    this->type = type;
}

const std::array<char, 256> &IntegrityBlock::getParam() const
{
    return param;
}

void IntegrityBlock::setParam(const std::array<char, 256> &param)
{
    this->param = param;
}
