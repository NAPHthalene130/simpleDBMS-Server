#pragma once

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <cstdint>

namespace storage {

static constexpr std::uint32_t kDataPageSize   = 4096;
static constexpr std::uint32_t kDataPageHeader = 32;
static constexpr std::uint32_t kSlotSize       = 4;

/**
 * @enum DataType
 * @brief 列数据类型
 * @author Startale
 */
enum class DataType : std::uint8_t {
    TEXT    = 0,  // 无界文本（默认兼容旧数据）
    INT     = 1,  // 32 位有符号整数
    FLOAT   = 2,  // 双精度浮点
    VARCHAR = 3,  // 变长字符串，maxLen 在 ColumnMeta::varcharLen
};

/**
 * @struct ColumnMeta
 * @brief 列元数据
 * @author NAPH130
 */
struct ColumnMeta {
    std::int32_t integrities = 0;
    std::string defaultValue;
    DataType     dataType    = DataType::TEXT;
    std::uint16_t varcharLen = 0;  // VARCHAR(n) 的最大长度
};

/**
 * @struct DataPageHeader
 * @brief 数据页头部 (32 bytes)
 * @author Startale
 */
#pragma pack(push, 1)
struct DataPageHeader {
    std::uint32_t pageId      = 0;
    std::uint16_t freeStart   = kDataPageHeader;  // tuple 写入起始
    std::uint16_t freeEnd     = kDataPageSize;     // slot 写入起始 (倒序)
    std::uint16_t slotCount   = 0;
    std::uint16_t flags       = 0;
    std::uint8_t  reserved[20]{};
};
#pragma pack(pop)

/**
 * @struct PageSlot
 * @brief 槽目录项 (4 bytes)
 * @author Startale
 */
#pragma pack(push, 1)
struct PageSlot {
    std::uint16_t offset = 0;  // tuple 在页内偏移
    std::uint16_t flags  = 0;  // 0=有效, 1=已删除
};
#pragma pack(pop)

/**
 * @struct TupleRef
 * @brief 行物理定位引用，pack 为 uint64_t 存入索引文件
 * @author Startale
 */
struct TupleRef {
    std::uint32_t pageId    = 0;
    std::uint32_t slotIndex = 0;

    std::uint64_t pack() const { return (static_cast<std::uint64_t>(pageId) << 32) | slotIndex; }
    static TupleRef unpack(std::uint64_t p) { return {static_cast<std::uint32_t>(p >> 32), static_cast<std::uint32_t>(p & 0xFFFFFFFFU)}; }
};

/**
 * @struct Row
 * @brief 行数据结构
 * @author Startale
 */
struct Row {
    std::vector<std::string> values;
};

/**
 * @struct TableSchema
 * @brief 表结构定义
 * @author Startale
 */
struct TableSchema {
    std::string name;
    std::vector<std::string> columns;
    std::vector<ColumnMeta> columnMetas;
};

/**
 * @brief 使用分隔符拼接字符串数组
 * @author Startale
 * @param items 待拼接字符串列表
 * @param delim 分隔符
 * @return 拼接后的字符串
 */
inline std::string join(const std::vector<std::string>& items, const std::string& delim) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            oss << delim;
        }
        oss << items[i];
    }
    return oss.str();
}

/**
 * @brief 按分隔符拆分字符串
 * @author Startale
 * @param text 原始字符串
 * @param delim 分隔符
 * @return 拆分后的字符串数组
 */
inline std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

/**
 * @brief 将行对象序列化为字符串
 * @author Startale
 * @param row 行对象
 * @return 序列化字符串
 */
inline std::string serializeRow(const Row& row) {
    return join(row.values, "|");
}

/**
 * @brief 将字符串反序列化为行对象
 * @author Startale
 * @param line 行字符串
 * @return 行对象
 */
inline Row deserializeRow(const std::string& line) {
    return Row{split(line, '|')};
}

/**
 * @brief 断言检查
 * @author Startale
 * @param condition 条件
 * @param message 异常信息
 * @note 条件为 false 时抛出 std::runtime_error
 */
inline void ensure(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace storage
