#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "models/storage/DatabaseBlock.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {

template <std::size_t N>
std::array<char, N> toArray(const std::string &text)
{
    std::array<char, N> out{};
    const auto copyLen = std::min<std::size_t>(text.size(), N - 1);
    std::copy_n(text.data(), copyLen, out.data());
    return out;
}

void ensure(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

class Core {
public:
    Core()
        : storageManager(new StorageManager(this))
    {
    }

    ~Core()
    {
        delete storageManager;
        storageManager = nullptr;
    }

    StorageManager *getStorageManager()
    {
        return storageManager;
    }

private:
    StorageManager *storageManager;
};

int main()
{
    const std::filesystem::path storageDir = std::filesystem::current_path() / "src" / "storage";
    ensure(std::filesystem::exists(storageDir) && std::filesystem::is_directory(storageDir),
           "storage directory not found: " + storageDir.string());
    std::filesystem::current_path(storageDir);

    const std::string dbName = "StartaleDB";
    const std::string tableName = "StartaleTB";
    const std::filesystem::path dbRoot("data");
    const std::filesystem::path dbDir = dbRoot / dbName;
    const std::filesystem::path dbFile = dbRoot / (dbName + ".db");
    const std::filesystem::path tdfFile = dbDir / (tableName + ".tdf");
    const std::filesystem::path trdFile = dbDir / (tableName + ".trd");
    const std::filesystem::path ticFile = dbDir / (tableName + ".tic");
    const std::filesystem::path tidFile = dbDir / (tableName + ".tid");

    if (std::filesystem::exists(dbDir)) {
        std::filesystem::remove_all(dbDir);
    }
    if (std::filesystem::exists(dbFile)) {
        std::filesystem::remove(dbFile);
    }

    Core core;

    auto *systemCatalogManager = core.getStorageManager()->getSystemCatalogManager();
    auto *databaseManager = core.getStorageManager()  ->getDatabaseManager();
    ensure(systemCatalogManager != nullptr, "systemCatalogManager is null");
    ensure(databaseManager != nullptr, "databaseManager is null");

    DatabaseBlock dbInfo;
    dbInfo.setName(toArray<128>(dbName));
    ensure(systemCatalogManager->createDatabase(dbInfo), "createDatabase failed");

    ensure(databaseManager->createTable(dbName, tableName, {"A", "B", "C"}), "createTable failed");
    ensure(databaseManager->insertRow(dbName, tableName, {"v1", "v2", "v3"}), "insertRow failed");
    ensure(databaseManager->insertRow(dbName, tableName, {"v5", "v2", "v3"}), "insertRow failed");

    auto loadedTable = storage::Table::load(dbDir, tableName);
    const auto selectedRows = loadedTable.select(
        {"A", "C"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::EQ, "v1"}}
    );
    ensure(selectedRows.size() == 1, "select result row count mismatch");
    ensure(selectedRows.front().values.size() == 2, "select projected column count mismatch");
    ensure(selectedRows.front().values[0] == "v1", "select value A mismatch");
    ensure(selectedRows.front().values[1] == "v3", "select value C mismatch");

    const auto likeBoth = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "%1%"}}
    );
    ensure(likeBoth.size() == 1, "select like %field% mismatch");

    const auto likeSuffix = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "%5"}}
    );
    ensure(likeSuffix.size() == 1, "select like %field mismatch");

    const auto likePrefix = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "v%"}}
    );
    ensure(likePrefix.size() == 2, "select like field% mismatch");
    ensure(std::filesystem::exists(dbFile), ".db file not found");
    ensure(std::filesystem::exists(tdfFile), ".tdf file not found");
    ensure(std::filesystem::exists(trdFile), ".trd file not found");
    ensure(std::filesystem::exists(ticFile), ".tic file not found");
    ensure(std::filesystem::exists(tidFile), ".tid file not found");

    std::ifstream tdf(tdfFile);
    std::ifstream trd(trdFile);
    std::ifstream tic(ticFile);
    std::ifstream tid(tidFile);
    bool foundSchemaVersion = false;
    bool foundTable = false;
    bool foundColumns = false;
    bool foundPrimaryIndexDef = false;
    bool foundPrimaryConstraint = false;
    bool foundRow = false;
    bool foundTidHeader = false;
    bool foundTidRootPage = false;
    bool foundTidKeyEntry = false;
    std::string line;
    while (std::getline(tdf, line)) {
        if (line == "schema_version=2") foundSchemaVersion = true;
        if (line == "table=" + tableName) foundTable = true;
        if (line == "columns=A:TEXT|B:TEXT|C:TEXT") foundColumns = true;
        if (line == "index_definitions=PRIMARY(A):BTREE:" + tableName + ".tid") foundPrimaryIndexDef = true;
    }
    while (std::getline(tic, line)) {
        if (line == "constraint=PRIMARY_KEY(A)") foundPrimaryConstraint = true;
    }
    while (std::getline(trd, line)) {
        if (line == "ROW|v1|v2|v3") {
            foundRow = true;
        }
    }
    while (std::getline(tid, line)) {
        if (line == "TID_PAGED_V1") foundTidHeader = true;
        if (line == "root_page=1") foundTidRootPage = true;
        if (line.rfind("ENTRY|v1|", 0) == 0) foundTidKeyEntry = true;
    }

    ensure(foundSchemaVersion, "tdf schema_version missing");
    ensure(foundTable, "tdf table line mismatch");
    ensure(foundColumns, "tdf columns line mismatch");
    ensure(foundPrimaryIndexDef, "tdf index definition missing");
    ensure(foundPrimaryConstraint, "tic primary key missing");
    ensure(foundRow, "trd row line mismatch");
    ensure(foundTidHeader, "tid header missing");
    ensure(foundTidRootPage, "tid root page missing");
    ensure(foundTidKeyEntry, "tid key entry missing");

    std::cout << "StorageCoreChainTest passed." << std::endl;
    return 0;
}
