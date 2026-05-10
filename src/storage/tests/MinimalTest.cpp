#include <iostream>
#include <filesystem>
#include "storage/object/Table.h"
#include "storage/object/StorageCommon.h"
int main() {
    try {
        auto t = storage::Table::create("data", "test", {"A","B"});
        t.insert({"hello","world"});
        auto rows = t.select({"*"});
        std::cout << "rows=" << rows.size() << std::endl;
        for(auto& r : rows) {
            for(auto& v : r.values) std::cout << v << " ";
            std::cout << std::endl;
        }
        auto t2 = storage::Table::load("data","test");
        auto rows2 = t2.select({"*"});
        std::cout << "reload rows=" << rows2.size() << std::endl;
        t2.updateByPrimaryKey("hello", {"hello","updated"});
        auto rows3 = t2.select({"*"});
        for(auto& r : rows3) {
            for(auto& v : r.values) std::cout << v << " ";
            std::cout << std::endl;
        }
        std::cout << "PASS" << std::endl;
    } catch(const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
