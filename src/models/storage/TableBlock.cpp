#include "TableBlock.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

std::string dateTimeToString(const DateTime &dateTime)
{
    std::ostringstream oss;
    oss << dateTime.getYear() << ','
        << dateTime.getMonth() << ','
        << dateTime.getDayOfWeek() << ','
        << dateTime.getDay() << ','
        << dateTime.getHour() << ','
        << dateTime.getMinute() << ','
        << dateTime.getSecond() << ','
        << dateTime.getMilliseconds();
    return oss.str();
}

bool tryParseDateTime(const std::string &text, DateTime &dateTime)
{
    std::istringstream iss(text);
    std::string token;
    std::vector<int> values;
    try {
        while (std::getline(iss, token, ',')) {
            if (token.empty()) {
                return false;
            }
            values.push_back(std::stoi(token));
        }
    } catch (...) {
        return false;
    }
    if (values.size() != 8) {
        return false;
    }
    dateTime.setYear(static_cast<std::uint16_t>(values[0]));
    dateTime.setMonth(static_cast<std::uint16_t>(values[1]));
    dateTime.setDayOfWeek(static_cast<std::uint16_t>(values[2]));
    dateTime.setDay(static_cast<std::uint16_t>(values[3]));
    dateTime.setHour(static_cast<std::uint16_t>(values[4]));
    dateTime.setMinute(static_cast<std::uint16_t>(values[5]));
    dateTime.setSecond(static_cast<std::uint16_t>(values[6]));
    dateTime.setMilliseconds(static_cast<std::uint16_t>(values[7]));
    return true;
}

} // namespace

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

std::vector<std::string> TableBlock::toDescriptorLines() const
{
    return {
        "name=" + arrayToString(name),
        "record_num=" + std::to_string(recordNum),
        "field_num=" + std::to_string(fieldNum),
        "tdf=" + arrayToString(tdf),
        "tic=" + arrayToString(tic),
        "trd=" + arrayToString(trd),
        "tid=" + arrayToString(tid),
        "crtime=" + dateTimeToString(createTime),
        "mtime=" + dateTimeToString(modifyTime)};
}

bool TableBlock::fromDescriptorLines(const std::vector<std::string> &lines, TableBlock &outBlock)
{
    if (lines.empty()) {
        return false;
    }

    std::string nameValue;
    std::int32_t recordNumValue = 0;
    std::int32_t fieldNumValue = 0;
    std::string tdfValue;
    std::string ticValue;
    std::string trdValue;
    std::string tidValue;
    DateTime crtimeValue;
    DateTime mtimeValue;
    bool hasRecordNum = false;
    bool hasFieldNum = false;
    bool hasCrtime = false;
    bool hasMtime = false;

    for (const auto &line : lines) {
        const auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        if (key == "name") {
            nameValue = value;
        } else if (key == "record_num") {
            try {
                recordNumValue = std::stoi(value);
                hasRecordNum = true;
            } catch (...) {
                hasRecordNum = false;
            }
        } else if (key == "field_num") {
            try {
                fieldNumValue = std::stoi(value);
                hasFieldNum = true;
            } catch (...) {
                hasFieldNum = false;
            }
        } else if (key == "tdf") {
            tdfValue = value;
        } else if (key == "tic") {
            ticValue = value;
        } else if (key == "trd") {
            trdValue = value;
        } else if (key == "tid") {
            tidValue = value;
        } else if (key == "crtime") {
            hasCrtime = tryParseDateTime(value, crtimeValue);
        } else if (key == "mtime") {
            hasMtime = tryParseDateTime(value, mtimeValue);
        }
    }

    if (nameValue.empty()) {
        return false;
    }

    TableBlock block;
    block.setName(stringToArray<128>(nameValue));
    block.setRecordNum(hasRecordNum ? std::max(recordNumValue, 0) : 0);
    block.setFieldNum(hasFieldNum ? std::max(fieldNumValue, 0) : 0);
    block.setTdf(stringToArray<256>(tdfValue));
    block.setTic(stringToArray<256>(ticValue));
    block.setTrd(stringToArray<256>(trdValue));
    block.setTid(stringToArray<256>(tidValue));
    if (hasCrtime) {
        block.setCreateTime(crtimeValue);
    }
    if (hasMtime) {
        block.setModifyTime(mtimeValue);
    }
    outBlock = block;
    return true;
}
