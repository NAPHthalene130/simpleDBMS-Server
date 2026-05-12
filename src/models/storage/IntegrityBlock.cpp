#include "IntegrityBlock.h"

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

} // namespace

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

std::string IntegrityBlock::toDescriptorLine() const
{
    const std::string fieldStr = arrayToString(field);
    const std::string paramStr = arrayToString(param);

    switch (type) {
        case TYPE_NOT_NULL:
            return "constraint=NOT_NULL(" + fieldStr + ")";
        case TYPE_UNIQUE:
            return "constraint=UNIQUE(" + fieldStr + ")";
        case TYPE_DEFAULT:
            return "constraint=DEFAULT(" + fieldStr + "|" + paramStr + ")";
        case TYPE_CHECK:
            return "constraint=CHECK(" + fieldStr + "|" + paramStr + ")";
        case TYPE_PRIMARY_KEY:
            return "constraint=PRIMARY_KEY(" + fieldStr + ")";
        default:
            return "";
    }
}

bool IntegrityBlock::fromDescriptorLine(const std::string &line, IntegrityBlock &outBlock)
{
    if (line.empty()) return false;

    if (line.rfind("constraint=NOT_NULL(", 0) == 0 && line.back() == ')') {
        outBlock.setField(stringToArray<128>(line.substr(20, line.size() - 21)));
        outBlock.setType(TYPE_NOT_NULL);
        return true;
    }
    if (line.rfind("constraint=UNIQUE(", 0) == 0 && line.back() == ')') {
        outBlock.setField(stringToArray<128>(line.substr(18, line.size() - 19)));
        outBlock.setType(TYPE_UNIQUE);
        return true;
    }
    if (line.rfind("constraint=DEFAULT(", 0) == 0 && line.back() == ')') {
        const std::string body = line.substr(19, line.size() - 20);
        const auto sep = body.find('|');
        if (sep == std::string::npos || sep == 0) return false;
        outBlock.setField(stringToArray<128>(body.substr(0, sep)));
        outBlock.setType(TYPE_DEFAULT);
        outBlock.setParam(stringToArray<256>(body.substr(sep + 1)));
        return true;
    }
    if (line.rfind("constraint=CHECK(", 0) == 0 && line.back() == ')') {
        const std::string body = line.substr(17, line.size() - 18);
        const auto sep = body.find('|');
        if (sep == std::string::npos || sep == 0) return false;
        outBlock.setField(stringToArray<128>(body.substr(0, sep)));
        outBlock.setType(TYPE_CHECK);
        outBlock.setParam(stringToArray<256>(body.substr(sep + 1)));
        return true;
    }
    if (line.rfind("constraint=PRIMARY_KEY(", 0) == 0 && line.back() == ')') {
        outBlock.setField(stringToArray<128>(line.substr(23, line.size() - 24)));
        outBlock.setType(TYPE_PRIMARY_KEY);
        return true;
    }
    return false;
}
