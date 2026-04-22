#include <array>
#include <algorithm>

#include <iostream>
#include <string>

#include "storage/manager/DatabaseManager.h"
#include "storage/manager/SystemCatalogManager.h"

namespace {
template <std::size_t N>
std::array<char, N> toArray(const std::string &text)
{
    std::array<char, N> out{};
    const auto len = std::min<std::size_t>(text.size(), N - 1);
    std::copy_n(text.data(), len, out.data());
    return out;
}
} // namespace

int main() {
    try {
        SystemCatalogManager systemCatalogManager(nullptr);
        DatabaseManager databaseManager(nullptr);

        DatabaseBlock dbInfo;
        dbInfo.setName(toArray<128>("school"));
        systemCatalogManager.createDatabase(dbInfo);

        TableBlock tableInfo;
        tableInfo.setName(toArray<128>("student"));
        tableInfo.setFieldNum(3);
        tableInfo.setTdf(toArray<256>("data/school/student.tdf"));
        tableInfo.setTrd(toArray<256>("data/school/student.trd"));
        tableInfo.setTic(toArray<256>("data/school/student.tic"));
        tableInfo.setTid(toArray<256>("data/school/student.tid"));
        databaseManager.createTable(tableInfo);

        std::cout << "done\n";
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
