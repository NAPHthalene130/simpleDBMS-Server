#include "DatabaseManager.h"

#include <iostream>

int main() {
    try {
        storage::DatabaseManager dbm("./data");

        dbm.createDatabase("school");
        dbm.useDatabase("school");

        dbm.createTable("student", {"id", "name", "age"});
        dbm.insert("student", {"1001", "Alice", "20"});
        dbm.insert("student", {"1002", "Bob", "21"});

        std::cout << "done\n";
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
