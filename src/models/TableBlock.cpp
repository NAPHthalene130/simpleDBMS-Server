#include "TableBlock.h"

TableBlock::TableBlock()
    : name{},
      recordNum(0),
      fieldNum(0),
      tdf{},
      tic{},
      trd{},
      tid{},
      createTime(),
      modifyTime()
{
}

const std::array<char, 128> &TableBlock::getName() const
{
    return name;
}

void TableBlock::setName(const std::array<char, 128> &name)
{
    this->name = name;
}

std::int32_t TableBlock::getRecordNum() const
{
    return recordNum;
}

void TableBlock::setRecordNum(std::int32_t recordNum)
{
    this->recordNum = recordNum;
}

std::int32_t TableBlock::getFieldNum() const
{
    return fieldNum;
}

void TableBlock::setFieldNum(std::int32_t fieldNum)
{
    this->fieldNum = fieldNum;
}

const std::array<char, 256> &TableBlock::getTdf() const
{
    return tdf;
}

void TableBlock::setTdf(const std::array<char, 256> &tdf)
{
    this->tdf = tdf;
}

const std::array<char, 256> &TableBlock::getTic() const
{
    return tic;
}

void TableBlock::setTic(const std::array<char, 256> &tic)
{
    this->tic = tic;
}

const std::array<char, 256> &TableBlock::getTrd() const
{
    return trd;
}

void TableBlock::setTrd(const std::array<char, 256> &trd)
{
    this->trd = trd;
}

const std::array<char, 256> &TableBlock::getTid() const
{
    return tid;
}

void TableBlock::setTid(const std::array<char, 256> &tid)
{
    this->tid = tid;
}

const DateTime &TableBlock::getCreateTime() const
{
    return createTime;
}

void TableBlock::setCreateTime(const DateTime &createTime)
{
    this->createTime = createTime;
}

const DateTime &TableBlock::getModifyTime() const
{
    return modifyTime;
}

void TableBlock::setModifyTime(const DateTime &modifyTime)
{
    this->modifyTime = modifyTime;
}
