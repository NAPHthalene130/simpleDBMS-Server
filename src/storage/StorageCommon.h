#pragma once

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace storage {

struct Row {
    std::vector<std::string> values;
};

struct TableSchema {
    std::string name;
    std::vector<std::string> columns;
};

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

inline std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

inline std::string serializeRow(const Row& row) {
    return join(row.values, "|");
}

inline Row deserializeRow(const std::string& line) {
    return Row{split(line, '|')};
}

inline void ensure(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace storage
