#pragma once

#include <array>
#include <cstdint>

#include "DateTime.h"

/**
 * @class TableBlock
 * @brief 表描述块类
 * @details 封装数据表级别的元数据，包括表名、记录数、字段数、字段定义文件、完整性约束文件、
 * 记录数据文件、索引定义文件以及创建与最近修改时间，用于表对象初始化与目录持久化。
 * @author NAPH130
 */
class TableBlock
{
public:
    TableBlock();

    const std::array<char, 128> &getName() const;
    void setName(const std::array<char, 128> &name);

    std::int32_t getRecordNum() const;
    void setRecordNum(std::int32_t recordNum);

    std::int32_t getFieldNum() const;
    void setFieldNum(std::int32_t fieldNum);

    const std::array<char, 256> &getTdf() const;
    void setTdf(const std::array<char, 256> &tdf);

    const std::array<char, 256> &getTic() const;
    void setTic(const std::array<char, 256> &tic);

    const std::array<char, 256> &getTrd() const;
    void setTrd(const std::array<char, 256> &trd);

    const std::array<char, 256> &getTid() const;
    void setTid(const std::array<char, 256> &tid);

    const DateTime &getCreateTime() const;
    void setCreateTime(const DateTime &createTime);

    const DateTime &getModifyTime() const;
    void setModifyTime(const DateTime &modifyTime);

private:
    std::array<char, 128> name; ///< 表名称
    std::int32_t recordNum;     ///< 当前记录条数
    std::int32_t fieldNum;      ///< 字段总数
    std::array<char, 256> tdf;  ///< 字段定义文件路径
    std::array<char, 256> tic;  ///< 完整性约束文件路径
    std::array<char, 256> trd;  ///< 记录数据文件路径
    std::array<char, 256> tid;  ///< 索引定义文件路径
    DateTime createTime;        ///< 表创建时间
    DateTime modifyTime;        ///< 最近修改时间
};
