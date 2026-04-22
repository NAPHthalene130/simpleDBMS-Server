#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace storage {

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
