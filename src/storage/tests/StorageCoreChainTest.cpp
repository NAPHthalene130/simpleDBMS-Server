#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "models/storage/DatabaseBlock.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {

constexpr const char *kCatalogBlockSeparator = "---DB_BLOCK---";

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

void removeDatabaseFromCatalog(const std::filesystem::path &catalogFile, const std::string &dbName)
{
    if (!std::filesystem::exists(catalogFile)) {
        return;
    }
    std::ifstream ifs(catalogFile);
    if (!ifs.good()) {
        return;
    }

    std::vector<std::vector<std::string>> blocks;
    std::vector<std::string> current;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line == kCatalogBlockSeparator) {
            if (!current.empty()) {
                blocks.push_back(current);
            }
            current.clear();
            continue;
        }
        if (!line.empty()) {
            current.push_back(line);
        }
    }
    if (!current.empty()) {
        blocks.push_back(current);
    }

    std::ofstream ofs(catalogFile, std::ios::trunc);
    if (!ofs.good()) {
        return;
    }
    for (const auto &block : blocks) {
        bool removeBlock = false;
        for (const auto &item : block) {
            if (item == "name=" + dbName || item == dbName) {
                removeBlock = true;
                break;
            }
        }
        if (removeBlock) {
            continue;
        }
        for (const auto &item : block) {
            ofs << item << '\n';
        }
        ofs << kCatalogBlockSeparator << '\n';
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
    const std::filesystem::path tbFile = dbDir / (dbName + ".tb");
    const std::filesystem::path catalogFile = dbRoot / "database.db";
    const std::filesystem::path tdfFile = dbDir / (tableName + ".tdf");
    const std::filesystem::path trdFile = dbDir / (tableName + ".trd");
    const std::filesystem::path ticFile = dbDir / (tableName + ".tic");
    const std::filesystem::path tidFile = dbDir / (tableName + ".tid");

    if (std::filesystem::exists(dbDir)) {
        std::filesystem::remove_all(dbDir);
    }
    removeDatabaseFromCatalog(catalogFile, dbName);

    Core core;

    auto *systemCatalogManager = core.getStorageManager()->getSystemCatalogManager();
    auto *databaseManager = core.getStorageManager()->getDatabaseManager();
    ensure(systemCatalogManager != nullptr, "systemCatalogManager is null");
    ensure(databaseManager != nullptr, "databaseManager is null");

    DatabaseBlock dbInfo;
    dbInfo.setName(toArray<128>(dbName));
    ensure(systemCatalogManager->createDatabase(dbInfo), "createDatabase failed");

    ensure(databaseManager->createTable(dbName, tableName, {"A", "B", "C"}), "createTable failed");
    {
        std::ifstream tb(tbFile);
        bool foundTableInTb = false;
        std::string line;
        while (std::getline(tb, line)) {
            if (line == "name=" + tableName || line == "table=" + tableName || line == tableName) {
                foundTableInTb = true;
                break;
            }
        }
        ensure(foundTableInTb, ".tb missing table name after createTable");
    }

    ensure(databaseManager->insertRow(dbName, tableName, {"v1", "v2", "v3"}), "insertRow failed");
    ensure(databaseManager->insertRow(dbName, tableName, {"v5", "v2", "v3"}), "insertRow failed");
    ensure(databaseManager->updateRowByPrimaryKey(dbName, tableName, "v1", {"v1", "v9", "v8"}),
           "updateRowByPrimaryKey failed");
    ensure(databaseManager->deleteRowByPrimaryKey(dbName, tableName, "v5"), "deleteRowByPrimaryKey failed");
    ensure(databaseManager->insertRow(dbName, tableName, {"v2", "v3", "v4"}), "insertRow failed");
    ensure(databaseManager->insertRow(dbName, tableName, {"v3", "v1", "v5"}), "insertRow failed");

    auto loadedTable = storage::Table::load(dbDir, tableName);
    const auto selectedRows = loadedTable.select(
        {"A", "C"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::EQ, "v1"}}
    );
    ensure(selectedRows.size() == 1, "select result row count mismatch");
    ensure(selectedRows.front().values.size() == 2, "select projected column count mismatch");
    ensure(selectedRows.front().values[0] == "v1", "select value A mismatch");
    ensure(selectedRows.front().values[1] == "v8", "select value C mismatch after update");

    const auto likeBoth = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "%1%"}}
    );
    ensure(likeBoth.size() == 1, "select like %field% mismatch");

    const auto likeSuffix = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "%5"}}
    );
    ensure(likeSuffix.empty(), "select like %field mismatch after delete");

    const auto likePrefix = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LIKE, "v%"}}
    );
    ensure(likePrefix.size() == 3, "select like field% mismatch after update/delete");

    auto betweenCond = storage::Table::WhereCondition {"A", storage::Table::CompareOp::BETWEEN, "v1"};
    betweenCond.secondValue = "v2";
    const auto betweenRows = loadedTable.select({"A"}, {betweenCond});
    ensure(betweenRows.size() == 2, "select BETWEEN mismatch");

    auto inCond = storage::Table::WhereCondition {"A", storage::Table::CompareOp::IN, ""};
    inCond.values = {"v1", "v3"};
    const auto inRows = loadedTable.select({"A"}, {inCond});
    ensure(inRows.size() == 2, "select IN mismatch");

    auto andLeft = std::make_shared<storage::Table::ConditionNode>();
    andLeft->isLeaf = true;
    andLeft->condition = storage::Table::WhereCondition {"B", storage::Table::CompareOp::EQ, "v9"};
    auto andRight = std::make_shared<storage::Table::ConditionNode>();
    andRight->isLeaf = true;
    andRight->condition = storage::Table::WhereCondition {"C", storage::Table::CompareOp::EQ, "v8"};
    auto andTree = std::make_shared<storage::Table::ConditionNode>();
    andTree->isLeaf = false;
    andTree->logicalOp = storage::Table::LogicalOp::AND;
    andTree->left = andLeft;
    andTree->right = andRight;
    const auto andRows = loadedTable.select({"A"}, andTree);
    ensure(andRows.size() == 1 && andRows.front().values.front() == "v1", "select AND tree mismatch");

    auto orLeft = std::make_shared<storage::Table::ConditionNode>();
    orLeft->isLeaf = true;
    orLeft->condition = storage::Table::WhereCondition {"A", storage::Table::CompareOp::EQ, "v3"};
    auto orRight = std::make_shared<storage::Table::ConditionNode>();
    orRight->isLeaf = true;
    orRight->condition = storage::Table::WhereCondition {"A", storage::Table::CompareOp::EQ, "v2"};
    auto orTree = std::make_shared<storage::Table::ConditionNode>();
    orTree->isLeaf = false;
    orTree->logicalOp = storage::Table::LogicalOp::OR;
    orTree->left = orLeft;
    orTree->right = orRight;
    storage::Table::SelectOptions options;
    options.orderByColumn = "A";
    options.orderByDesc = true;
    options.hasLimit = true;
    options.limit = 1;
    const auto orderLimitRows = loadedTable.select({"A"}, orTree, options);
    ensure(orderLimitRows.size() == 1 && orderLimitRows.front().values.front() == "v3",
           "select ORDER BY/LIMIT mismatch");

    ensure(std::filesystem::exists(catalogFile), "database.db file not found");
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
        if (line == "ROW|v1|v9|v8") {
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
