#include <iostream>
#include <filesystem>
#include "storage/object/Table.h"

int main() {
    try {
        std::filesystem::create_directories("data");
        std::filesystem::remove("data/mp.tdf"); std::filesystem::remove("data/mp.trd");
        std::filesystem::remove("data/mp.tic"); std::filesystem::remove("data/mp.tid");

        auto t = storage::Table::create("data", "mp", std::vector<std::string>{"Key","BigData"});

        // Each row ~2000 bytes → 2 rows per page → 10 rows = 5 pages
        std::string big(2000, 'x');
        const int total = 10;
        for (int i = 1; i <= total; ++i) {
            std::string key = std::to_string(100 + i).substr(1);
            auto tb = storage::Table::load("data", "mp");
            tb.insert({key, big + "_" + key});
            std::cerr << "  inserted " << i << "/" << total << std::endl;
        }

        auto loaded = storage::Table::load("data", "mp");
        auto all = loaded.select({"*"});
        if (all.size() != static_cast<std::size_t>(total)) {
            std::cerr << "FAIL: expected " << total << " rows, got " << all.size() << std::endl; return 1;
        }

        auto sz = std::filesystem::file_size("data/mp.trd");
        int pages = static_cast<int>(sz / 4096);
        if (pages < 3) { std::cerr << "FAIL: only " << pages << " pages, expected >=3" << std::endl; return 1; }
        std::cerr << ".trd: " << sz << " bytes, " << pages << " pages" << std::endl;

        // Verify cross-page query
        auto r1 = loaded.select({"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "01"}});
        auto r5 = loaded.select({"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "05"}});
        auto r10 = loaded.select({"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "10"}});
        if (r1.size() != 1)  { std::cerr << "FAIL row 1" << std::endl; return 1; }
        if (r5.size() != 1)  { std::cerr << "FAIL row 5" << std::endl; return 1; }
        if (r10.size() != 1) { std::cerr << "FAIL row 10" << std::endl; return 1; }

        // Delete from different pages
        auto td = storage::Table::load("data", "mp");
        td.deleteByPrimaryKey("01"); td.deleteByPrimaryKey("05"); td.deleteByPrimaryKey("10");
        auto after = storage::Table::load("data", "mp");
        if (after.select({"*"}).size() != 7) { std::cerr << "FAIL delete" << std::endl; return 1; }

        std::filesystem::remove("data/mp.tdf"); std::filesystem::remove("data/mp.trd");
        std::filesystem::remove("data/mp.tic"); std::filesystem::remove("data/mp.tid");
        std::cout << "S7 PASSED" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << std::endl; return 1;
    }
    return 0;
}
