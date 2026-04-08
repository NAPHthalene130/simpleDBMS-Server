#pragma once

#include <array>
#include <cstdint>

/**
 * @class IndexBlock
 * @brief 索引描述块类
 * @details 描述单个索引的定义信息，包括索引名称、唯一性、排序方向、参与索引的字段列表、
 * 记录文件路径和索引文件路径，用于索引创建、加载与查询优化过程中的元数据管理。
 * @author NAPH130
 */
class IndexBlock
{
public:
    IndexBlock();

    const std::array<char, 128> &getName() const;
    void setName(const std::array<char, 128> &name);

    bool getIsUnique() const;
    void setIsUnique(bool isUnique);

    bool getAsc() const;
    void setAsc(bool asc);

    std::int32_t getFieldNum() const;
    void setFieldNum(std::int32_t fieldNum);

    const std::array<std::array<char, 128>, 2> &getFields() const;
    void setFields(const std::array<std::array<char, 128>, 2> &fields);

    const std::array<char, 256> &getRecordFile() const;
    void setRecordFile(const std::array<char, 256> &recordFile);

    const std::array<char, 256> &getIndexFile() const;
    void setIndexFile(const std::array<char, 256> &indexFile);

private:
    std::array<char, 128> name;                  ///< 索引名称
    bool isUnique;                               ///< 是否唯一索引
    bool asc;                                    ///< 是否按升序排序
    std::int32_t fieldNum;                       ///< 参与索引的字段数量
    std::array<std::array<char, 128>, 2> fields; ///< 参与索引的字段名列表
    std::array<char, 256> recordFile;            ///< 对应记录文件路径
    std::array<char, 256> indexFile;             ///< 索引数据文件路径
};
