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
constexpr std::uint32_t kTidPageSize = 4096;

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

class TestCore {
public:
    TestCore()
        : storageManager(new StorageManager(reinterpret_cast<Core*>(this)))
    {
    }

    ~TestCore()
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
    try {
    const std::filesystem::path storageDir = std::filesystem::current_path() / "src" / "storage";
    ensure(std::filesystem::exists(storageDir) && std::filesystem::is_directory(storageDir),
           "storage directory not found: " + storageDir.string());
    std::filesystem::current_path(storageDir);

    const std::string dbName = "StartaleDB";
    const std::string tableName = "StartaleTB";
    const std::string aggTableName = "AggTB";
    const std::string constraintTableName = "ConstraintTB";
    const std::string joinTableName = "JoinTB";
    const std::filesystem::path dbRoot("data");
    const std::filesystem::path dbDir = dbRoot / dbName;
    const std::filesystem::path tbFile = dbDir / (dbName + ".tb");
    const std::filesystem::path catalogFile = dbRoot / "database.db";
    const std::filesystem::path tdfFile = dbDir / (tableName + ".tdf");
    const std::filesystem::path trdFile = dbDir / (tableName + ".trd");
    const std::filesystem::path ticFile = dbDir / (tableName + ".tic");
    const std::filesystem::path tidFile = dbDir / (tableName + ".tid");
    const std::filesystem::path nidxBFile = dbDir / (tableName + ".B.nidx");
    const std::filesystem::path nidxCFile = dbDir / (tableName + ".C.nidx");

    if (std::filesystem::exists(dbDir)) {
        std::filesystem::remove_all(dbDir);
    }
    removeDatabaseFromCatalog(catalogFile, dbName);

    TestCore core;
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

    ensure(databaseManager->createTable(dbName, joinTableName, {"ARef", "Label", "Weight"}),
           "createTable join failed");
    ensure(databaseManager->insertRow(dbName, joinTableName, {"v1", "alpha", "10"}),
           "insertRow join failed");
    ensure(databaseManager->insertRow(dbName, joinTableName, {"v3", "gamma", "30"}),
           "insertRow join failed");
    ensure(databaseManager->insertRow(dbName, joinTableName, {"v4", "delta", "40"}),
           "insertRow join failed");

    ensure(databaseManager->createTable(dbName, aggTableName, {"ID", "Score", "Qty"}),
           "createTable agg failed");
    ensure(databaseManager->insertRow(dbName, aggTableName, {"k1", "10", "2"}), "insertRow agg failed");
    ensure(databaseManager->insertRow(dbName, aggTableName, {"k2", "20", "3"}), "insertRow agg failed");
    ensure(databaseManager->insertRow(dbName, aggTableName, {"k3", "30", "4"}), "insertRow agg failed");

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

    const auto neRows = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::NE, "v2"}}
    );
    ensure(neRows.size() == 2, "select NE mismatch");

    const auto gtRows = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::GT, "v1"}}
    );
    ensure(gtRows.size() == 2, "select GT mismatch");

    const auto leRows = loadedTable.select(
        {"A"},
        {storage::Table::WhereCondition{"A", storage::Table::CompareOp::LE, "v2"}}
    );
    ensure(leRows.size() == 2, "select LE mismatch");

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

    auto aggTable = storage::Table::load(dbDir, aggTableName);
    const auto aggValues = aggTable.aggregate({
        {storage::Table::AggregateOp::COUNT, "*"},
        {storage::Table::AggregateOp::SUM, "Score"},
        {storage::Table::AggregateOp::AVG, "Score"},
        {storage::Table::AggregateOp::MIN, "Score"},
        {storage::Table::AggregateOp::MAX, "Score"},
    });
    ensure(aggValues.size() == 5, "aggregate result size mismatch");
    ensure(aggValues[0] == "3", "aggregate COUNT mismatch");
    ensure(aggValues[1] == "60", "aggregate SUM mismatch");
    ensure(aggValues[2] == "20", "aggregate AVG mismatch");
    ensure(aggValues[3] == "10", "aggregate MIN mismatch");
    ensure(aggValues[4] == "30", "aggregate MAX mismatch");

    auto aggWhere = storage::Table::WhereCondition {"Score", storage::Table::CompareOp::BETWEEN, "15"};
    aggWhere.secondValue = "30";
    const auto filteredAgg = aggTable.aggregate({
        {storage::Table::AggregateOp::COUNT, "*"},
        {storage::Table::AggregateOp::SUM, "Qty"},
    }, {aggWhere});
    ensure(filteredAgg.size() == 2, "aggregate with where result size mismatch");
    ensure(filteredAgg[0] == "2", "aggregate with where COUNT mismatch");
    ensure(filteredAgg[1] == "7", "aggregate with where SUM mismatch");

    const std::vector<storage::Table::ColumnDefinition> constraintDefs = {
        {"ID", storage::Table::ColumnConstraintSpec{"ID", true, true, false, ""}},
        {"Name", storage::Table::ColumnConstraintSpec{"Name", true, true, false, ""}},
        {"Tag", storage::Table::ColumnConstraintSpec{"Tag", false, false, true, "N/A"}},
    };
    ensure(databaseManager->createTable(dbName, constraintTableName, constraintDefs),
           "createTable with constraints failed");
    ensure(databaseManager->insertRow(dbName, constraintTableName, {"u1", "alice"}),
           "insert default row failed");
    ensure(databaseManager->insertRow(dbName, constraintTableName, {"u2", "bob", "X"}),
           "insert explicit tag row failed");
    ensure(!databaseManager->insertRow(dbName, constraintTableName, {"u3", "alice", "Y"}),
           "UNIQUE constraint should reject duplicate Name");
    ensure(!databaseManager->insertRow(dbName, constraintTableName, {"u4", "", "Z"}),
           "NOT NULL constraint should reject empty Name");

    ensure(databaseManager->addColumnConstraint(
               dbName, constraintTableName, storage::Table::ColumnConstraintSpec{"Tag", true, false, false, ""}),
           "add NOT NULL constraint to Tag failed");
    ensure(!databaseManager->insertRow(dbName, constraintTableName, {"u5", "eve", ""}),
           "NOT NULL(Tag) should reject empty value");

    const auto defaultRows = databaseManager->selectRows(
        dbName,
        constraintTableName,
        {"ID", "Tag"},
        {},
        {storage::Table::QueryConstraint{"Tag", storage::Table::ConstraintType::DEFAULT_VALUE, true}});
    ensure(defaultRows.size() == 1, "query constraint DEFAULT filter mismatch");
    ensure(defaultRows.front().values.size() == 2 && defaultRows.front().values[1] == "N/A",
           "query constraint DEFAULT value mismatch");

    const auto uniqueRows = databaseManager->selectRows(
        dbName,
        constraintTableName,
        {"ID", "Name"},
        {},
        {storage::Table::QueryConstraint{"Name", storage::Table::ConstraintType::UNIQUE, true}});
    ensure(uniqueRows.size() == 2, "query constraint UNIQUE filter mismatch");

    DatabaseManager::JoinQuery joinQuery;
    joinQuery.baseTable = tableName;
    joinQuery.baseAlias = "l";
    DatabaseManager::JoinSpec innerJoin;
    innerJoin.type = DatabaseManager::JoinType::INNER_JOIN;
    innerJoin.tableName = joinTableName;
    innerJoin.alias = "r";
    innerJoin.onConditions.push_back({{"l", "A"}, {"r", "ARef"}, storage::Table::CompareOp::EQ});
    joinQuery.joins.push_back(innerJoin);
    joinQuery.projections = {
        {{"l", "A"}, "leftA"},
        {{"l", "B"}, "leftB"},
        {{"r", "Label"}, "rightLabel"},
        {{"r", "Weight"}, "weight"}
    };
    joinQuery.postFilters.push_back({{"r", "Weight"}, storage::Table::CompareOp::GT, "15", "", {}});
    joinQuery.options.orderByOutput = "leftA";
    const auto joinedRows = databaseManager->selectJoinRows(dbName, joinQuery);
    ensure(joinedRows.columns.size() == 4, "join columns size mismatch");
    ensure(joinedRows.rows.size() == 1, "inner join row count mismatch");
    ensure(joinedRows.rows.front().values[0] == "v3", "inner join key mismatch");
    ensure(joinedRows.rows.front().values[2] == "gamma", "inner join projected value mismatch");

    DatabaseManager::JoinQuery leftJoinQuery;
    leftJoinQuery.baseTable = tableName;
    leftJoinQuery.baseAlias = "l";
    DatabaseManager::JoinSpec leftJoin = innerJoin;
    leftJoin.type = DatabaseManager::JoinType::LEFT_JOIN;
    leftJoinQuery.joins.push_back(leftJoin);
    leftJoinQuery.projections = {
        {{"l", "A"}, "leftA"},
        {{"r", "Label"}, "rightLabel"}
    };
    leftJoinQuery.options.orderByOutput = "leftA";
    const auto leftJoined = databaseManager->selectJoinRows(dbName, leftJoinQuery);
    ensure(leftJoined.rows.size() == 3, "left join row count mismatch");
    ensure(leftJoined.rows[0].values[0] == "v1", "left join order mismatch");
    ensure(leftJoined.rows[1].values[0] == "v2", "left join order mismatch");
    ensure(leftJoined.rows[2].values[0] == "v3", "left join order mismatch");
    ensure(leftJoined.rows[1].values[1].empty(), "left join unmatched row should be empty");

    ensure(std::filesystem::exists(catalogFile), "database.db file not found");
    ensure(std::filesystem::exists(tdfFile), ".tdf file not found");
    ensure(std::filesystem::exists(trdFile), ".trd file not found");
    ensure(std::filesystem::exists(ticFile), ".tic file not found");
    ensure(std::filesystem::exists(tidFile), ".tid file not found");
    ensure(std::filesystem::exists(nidxBFile), ".nidx B reserve file not found");
    ensure(std::filesystem::exists(nidxCFile), ".nidx C reserve file not found");

    std::ifstream tdf(tdfFile);
    std::ifstream trd(trdFile);
    std::ifstream tic(ticFile);
    std::ifstream tid(tidFile);
    bool foundSchemaVersion = false;
    bool foundTable = false;
    bool foundColumns = false;
    bool foundPrimaryIndexDef = false;
    bool foundReservedIndexDefB = false;
    bool foundReservedIndexDefC = false;
    bool foundPrimaryConstraint = false;
    bool foundReservedTicB = false;
    bool foundReservedTicC = false;
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
        if (line == "index_reserved=B:BTREE:" + tableName + ".B.nidx") foundReservedIndexDefB = true;
        if (line == "index_reserved=C:BTREE:" + tableName + ".C.nidx") foundReservedIndexDefC = true;
    }
    while (std::getline(tic, line)) {
        if (line == "constraint=PRIMARY_KEY(A)") foundPrimaryConstraint = true;
        if (line == "index_reserved=B:" + tableName + ".B.nidx") foundReservedTicB = true;
        if (line == "index_reserved=C:" + tableName + ".C.nidx") foundReservedTicC = true;
    }
    while (std::getline(trd, line)) {
        if (line == "ROW|v1|v9|v8") {
            foundRow = true;
        }
    }
    while (std::getline(tid, line)) {
        if (line == "TID_PAGED_V3") foundTidHeader = true;
        if (line.rfind("root_page=", 0) == 0) foundTidRootPage = true;
        if (line.rfind("ENTRY|v1|", 0) == 0) foundTidKeyEntry = true;
    }

    ensure(foundSchemaVersion, "tdf schema_version missing");
    ensure(foundTable, "tdf table line mismatch");
    ensure(foundColumns, "tdf columns line mismatch");
    ensure(foundPrimaryIndexDef, "tdf index definition missing");
    ensure(foundReservedIndexDefB, "tdf reserved index B missing");
    ensure(foundReservedIndexDefC, "tdf reserved index C missing");
    ensure(foundPrimaryConstraint, "tic primary key missing");
    ensure(foundReservedTicB, "tic reserved index B missing");
    ensure(foundReservedTicC, "tic reserved index C missing");
    ensure(foundRow, "trd row line mismatch");
    ensure(foundTidHeader, "tid header missing");
    ensure(foundTidRootPage, "tid root page missing");
    ensure(foundTidKeyEntry, "tid key entry missing");

    const std::string bulkTableName = "BulkTB";
    ensure(databaseManager->createTable(dbName, bulkTableName, {"Key"}), "createTable bulk failed");

    for (int i = 1; i <= 200; ++i) {
        std::string key = std::to_string(i);
        while (key.size() < 4) key = "0" + key;
        ensure(databaseManager->insertRow(dbName, bulkTableName, {key}),
               ("insertRow bulk failed i=" + std::to_string(i)).c_str());
    }

    {
        std::uintmax_t tidSize = std::filesystem::file_size(dbDir / (bulkTableName + ".tid"));
        ensure(tidSize > static_cast<std::uintmax_t>(kTidPageSize * 2),
               "bulk tid too small, expected multiple pages (" + std::to_string(tidSize) + " bytes)");
    }

    auto bulkTable = storage::Table::load(dbDir, bulkTableName);
    auto allBulkRows = bulkTable.select({"*"});
    ensure(allBulkRows.size() == 200,
           "bulk reload row count mismatch (" + std::to_string(allBulkRows.size()) + ")");

    {
        storage::Table::WhereCondition between;
        between.column = "Key";
        between.op = storage::Table::CompareOp::BETWEEN;
        between.value = "0050";
        between.secondValue = "0059";
        auto partialRows = bulkTable.select({"Key"}, {between});
        ensure(partialRows.size() == 10,
               "bulk range query mismatch (" + std::to_string(partialRows.size()) + ")");
    }

    auto firstRow = bulkTable.select(
        {"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "0001"}});
    ensure(firstRow.size() == 1 && firstRow.front().values.front() == "0001", "bulk EQ query mismatch");

    auto midRow = bulkTable.select(
        {"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "0100"}});
    ensure(midRow.size() == 1 && midRow.front().values.front() == "0100", "bulk EQ mid query mismatch");

    auto lastRow = bulkTable.select(
        {"Key"}, {storage::Table::WhereCondition{"Key", storage::Table::CompareOp::EQ, "0200"}});
    ensure(lastRow.size() == 1 && lastRow.front().values.front() == "0200", "bulk EQ last query mismatch");

    const std::string sidxTableName = "SITB";
    ensure(databaseManager->createTable(dbName, sidxTableName, {"ID", "Tag", "Score"}),
           "createTable secondary index failed");
    for (int i = 1; i <= 100; ++i) {
        std::string id = std::to_string(i);
        while (id.size() < 3) id = "0" + id;
        std::string tag = (i <= 50) ? "alpha" : "beta";
        std::string score = std::to_string((i * 7) % 100);
        if (score.size() < 2) score = "0" + score;
        ensure(databaseManager->insertRow(dbName, sidxTableName, {id, tag, score}),
               ("insertRow si failed i=" + std::to_string(i)).c_str());
    }

    auto siTable = storage::Table::load(dbDir, sidxTableName);

    auto tagAlpha = siTable.select(
        {"ID"}, {storage::Table::WhereCondition{"Tag", storage::Table::CompareOp::EQ, "alpha"}});
    ensure(tagAlpha.size() == 50, "secondary index EQ Tag=alpha mismatch (" + std::to_string(tagAlpha.size()) + ")");

    auto tagBeta = siTable.select(
        {"ID"}, {storage::Table::WhereCondition{"Tag", storage::Table::CompareOp::EQ, "beta"}});
    ensure(tagBeta.size() == 50, "secondary index EQ Tag=beta mismatch (" + std::to_string(tagBeta.size()) + ")");

    storage::Table::WhereCondition scoreBetween;
    scoreBetween.column = "Score";
    scoreBetween.op = storage::Table::CompareOp::BETWEEN;
    scoreBetween.value = "20";
    scoreBetween.secondValue = "40";
    auto scoreRange = siTable.select({"ID"}, {scoreBetween});
    ensure(scoreRange.size() > 0, "secondary index Score BETWEEN returned empty");

    auto scoreGT = siTable.select(
        {"ID"}, {storage::Table::WhereCondition{"Score", storage::Table::CompareOp::GT, "80"}});
    ensure(scoreGT.size() > 0, "secondary index Score GT returned empty");

    ensure(std::filesystem::exists(dbDir / (sidxTableName + ".Tag.nidx")),
           ".nidx file for Tag not found");
    ensure(std::filesystem::exists(dbDir / (sidxTableName + ".Score.nidx")),
           ".nidx file for Score not found");

    // ======== Persistence Recovery Test ========
    const std::string persistTableName = "PersistTB";
    ensure(databaseManager->createTable(dbName, persistTableName, {"PK", "Val", "Num"}),
           "createTable persist failed");
    for (int i = 1; i <= 50; ++i) {
        std::string pk = std::to_string(i);
        while (pk.size() < 3) pk = "0" + pk;
        std::string val = (i % 2 == 0) ? "even" : "odd";
        std::string num = std::to_string(i * 10);
        ensure(databaseManager->insertRow(dbName, persistTableName, {pk, val, num}),
               ("insertRow persist failed i=" + std::to_string(i)).c_str());
    }

    auto preReload = storage::Table::load(dbDir, persistTableName);
    auto allPre = preReload.select({"*"});
    ensure(allPre.size() == 50, "pre-reload count mismatch");

    auto reloaded1 = storage::Table::load(dbDir, persistTableName);
    auto all1 = reloaded1.select({"*"});
    ensure(all1.size() == 50, "reload1 row count mismatch (" + std::to_string(all1.size()) + ")");

    auto oddRows = reloaded1.select(
        {"PK"}, {storage::Table::WhereCondition{"Val", storage::Table::CompareOp::EQ, "odd"}});
    ensure(oddRows.size() == 25, "reload1 secondary EQ odd mismatch (" + std::to_string(oddRows.size()) + ")");

    storage::Table::WhereCondition numBetween;
    numBetween.column = "Num";
    numBetween.op = storage::Table::CompareOp::BETWEEN;
    numBetween.value = "100";
    numBetween.secondValue = "200";
    auto numRange = reloaded1.select({"PK"}, {numBetween});
    ensure(numRange.size() == 11, "reload1 secondary BETWEEN mismatch (" + std::to_string(numRange.size()) + ")");

    auto reloaded2 = storage::Table::load(dbDir, persistTableName);
    auto withPK = reloaded2.select(
        {"Val"}, {storage::Table::WhereCondition{"PK", storage::Table::CompareOp::EQ, "025"}});
    ensure(withPK.size() == 1 && withPK.front().values.front() == "odd",
           "reload2 primary EQ mismatch");

    auto withPK2 = reloaded2.select(
        {"Num"}, {storage::Table::WhereCondition{"PK", storage::Table::CompareOp::EQ, "050"}});
    ensure(withPK2.size() == 1 && withPK2.front().values.front() == "500",
           "reload2 primary EQ mismatch");

    std::cout << "StorageCoreChainTest passed." << std::endl;
    return 0;
    } catch (const std::exception &e) {
        std::cerr << "StorageCoreChainTest failed: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "StorageCoreChainTest failed: unknown exception" << std::endl;
        return 1;
    }
}



