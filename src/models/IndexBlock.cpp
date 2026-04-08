#include "IndexBlock.h"

IndexBlock::IndexBlock()
    : name{},
      isUnique(false),
      asc(true),
      fieldNum(0),
      fields{},
      recordFile{},
      indexFile{}
{
}

const std::array<char, 128> &IndexBlock::getName() const
{
    return name;
}

void IndexBlock::setName(const std::array<char, 128> &name)
{
    this->name = name;
}

bool IndexBlock::getIsUnique() const
{
    return isUnique;
}

void IndexBlock::setIsUnique(bool isUnique)
{
    this->isUnique = isUnique;
}

bool IndexBlock::getAsc() const
{
    return asc;
}

void IndexBlock::setAsc(bool asc)
{
    this->asc = asc;
}

std::int32_t IndexBlock::getFieldNum() const
{
    return fieldNum;
}

void IndexBlock::setFieldNum(std::int32_t fieldNum)
{
    this->fieldNum = fieldNum;
}

const std::array<std::array<char, 128>, 2> &IndexBlock::getFields() const
{
    return fields;
}

void IndexBlock::setFields(const std::array<std::array<char, 128>, 2> &fields)
{
    this->fields = fields;
}

const std::array<char, 256> &IndexBlock::getRecordFile() const
{
    return recordFile;
}

void IndexBlock::setRecordFile(const std::array<char, 256> &recordFile)
{
    this->recordFile = recordFile;
}

const std::array<char, 256> &IndexBlock::getIndexFile() const
{
    return indexFile;
}

void IndexBlock::setIndexFile(const std::array<char, 256> &indexFile)
{
    this->indexFile = indexFile;
}
