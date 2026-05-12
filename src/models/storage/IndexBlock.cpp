#include "IndexBlock.h"

#include <algorithm>
#include <cstring>

namespace {

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

} // namespace

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

std::string IndexBlock::toDescriptorLine(const std::string &prefix, const std::string &suffix) const
{
    const std::string idxFile = arrayToString(indexFile);

    if (prefix == "index") {
        const std::string indexName = arrayToString(name);
        return prefix + "=" + indexName + ":" + idxFile;
    }
    if (prefix == "index_definitions") {
        const std::string indexName = arrayToString(name);
        const std::string fld = arrayToString(fields[0]);
        return prefix + "=" + indexName + "(" + fld + "):" + suffix + idxFile;
    }
    // index_reserved
    const std::string fld = arrayToString(fields[0]);
    return prefix + "=" + fld + ":" + suffix + idxFile;
}
