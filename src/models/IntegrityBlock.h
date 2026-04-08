#pragma once

#include <array>
#include <cstdint>

/**
 * @class IntegrityBlock
 * @brief 完整性约束块类
 * @details 描述作用于字段的完整性约束定义，包括约束名称、目标字段、约束类型及参数内容。
 * 该类用于完整性规则加载、校验策略构建与约束信息持久化。
 * @author NAPH130
 */
class IntegrityBlock
{
public:
    IntegrityBlock();

    const std::array<char, 128> &getName() const;
    void setName(const std::array<char, 128> &name);

    const std::array<char, 128> &getField() const;
    void setField(const std::array<char, 128> &field);

    std::int32_t getType() const;
    void setType(std::int32_t type);

    const std::array<char, 256> &getParam() const;
    void setParam(const std::array<char, 256> &param);

private:
    std::array<char, 128> name;  ///< 约束名称
    std::array<char, 128> field; ///< 约束作用字段名
    std::int32_t type;           ///< 约束类型编码
    std::array<char, 256> param; ///< 约束参数内容
};
