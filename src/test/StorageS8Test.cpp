#include <iostream>
#include <filesystem>
#include "storage/object/Table.h"

int main() {
    try {
        std::filesystem::create_directories("data");
        std::filesystem::remove("data/cp.tdf"); std::filesystem::remove("data/cp.trd");
        std::filesystem::remove("data/cp.tic"); std::filesystem::remove("data/cp.tid");

        auto t = storage::Table::create("data", "cp", std::vector<std::string>{"A","B"});
        for (int i = 1; i <= 5; ++i) {
            auto tb = storage::Table::load("data", "cp");
            tb.insert({std::to_string(i), "v" + std::to_string(i)});
        }

        auto szBefore = std::filesystem::file_size("data/cp.trd");
        std::cerr << "before delete: " << szBefore << " bytes" << std::endl;

        // Delete 3 rows
        for (int i = 1; i <= 3; ++i) {
            auto td = storage::Table::load("data", "cp");
            td.deleteByPrimaryKey(std::to_string(i));
        }

        auto szAfter = std::filesystem::file_size("data/cp.trd");
        std::cerr << "after delete: " << szAfter << " bytes" << std::endl;

        // Compact
        auto tc = storage::Table::load("data", "cp");
        std::size_t removed = tc.compact();
        std::cerr << "compact removed " << removed << " slots" << std::endl;

        auto szCompact = std::filesystem::file_size("data/cp.trd");
        std::cerr << "after compact: " << szCompact << " bytes" << std::endl;

        auto loaded = storage::Table::load("data", "cp");
        auto all = loaded.select({"*"});
        std::cerr << "remaining rows: " << all.size() << std::endl;
        for (auto& r : all) std::cerr << "  " << r.values[0] << "|" << r.values[1] << std::endl;

        bool ok = (all.size() == 2) && (szCompact <= szAfter);
        std::filesystem::remove("data/cp.tdf"); std::filesystem::remove("data/cp.trd");
        std::filesystem::remove("data/cp.tic"); std::filesystem::remove("data/cp.tid");
        std::cout << (ok ? "S8 PASSED" : "S8 FAILED") << std::endl;
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
}
