#pragma once

#include <array>
#include <cstdint>

#include "DateTime.h"

/**
 * @class FieldBlock
 * @brief 字段定义块类
 * @details 描述单个字段的定义信息，包括字段顺序、字段名、数据类型、类型参数、修改时间以及
 * 与完整性约束的关联标识，用于表结构解析与字段校验逻辑。
 * @author NAPH130
 */
class FieldBlock
{
public:
    FieldBlock();

    std::int32_t getOrder() const;
    void setOrder(std::int32_t order);

    const std::array<char, 128> &getName() const;
    void setName(const std::array<char, 128> &name);

    std::int32_t getType() const;
    void setType(std::int32_t type);

    std::int32_t getParam() const;
    void setParam(std::int32_t param);

    const DateTime &getModifyTime() const;
    void setModifyTime(const DateTime &modifyTime);

    std::int32_t getIntegrities() const;
    void setIntegrities(std::int32_t integrities);

private:
    std::int32_t order;         ///< 字段在表中的顺序号
    std::array<char, 128> name; ///< 字段名称
    std::int32_t type;          ///< 字段类型编码
    std::int32_t param;         ///< 字段类型参数（长度/精度等）
    DateTime modifyTime;        ///< 字段定义最近修改时间
    std::int32_t integrities;   ///< 关联的完整性约束标识
};
