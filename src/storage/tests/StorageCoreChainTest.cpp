#include <algorithm>
#include <array>
#include <cstdint>
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

#ifndef SERVER_PROJECT_ROOT
#error SERVER_PROJECT_ROOT is not defined.
#endif

namespace {

constexpr const char *kCatalogBlockSeparator = "---DB_BLOCK---";

using Matrix = std::vector<std::vector<std::string>>;

struct TestLayout {
    std::filesystem::path storageDir;
    std::filesystem::path dataRoot;
    std::filesystem::path catalogFile;
    std::filesystem::path dbDir;
    std::filesystem::path tableBlockFile;
};

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

TestLayout buildLayout(const std::string &dbName)
{
    const auto storageDir = std::filesystem::path(SERVER_PROJECT_ROOT) / "src" / "storage";
    return {
        storageDir,
        storageDir / "data",
        storageDir / "data" / "database.db",
        storageDir / "data" / dbName,
        storageDir / "data" / dbName / (dbName + ".tb")
    };
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

void expectFileExists(const std::filesystem::path &filePath, const std::string &label)
{
    ensure(std::filesystem::exists(filePath), label + " not found: " + filePath.string());
}

void expectFileContainsLine(const std::filesystem::path &filePath,
                            const std::string &expectedLine,
                            const std::string &label)
{
    std::ifstream ifs(filePath, std::ios::binary);
    ensure(ifs.good(), "failed to open " + label + ": " + filePath.string());

    std::string line;
    while (std::getline(ifs, line)) {
        if (line == expectedLine) {
            return;
        }
    }
    ensure(false, label + " missing line: " + expectedLine);
}

void expectFileContainsPrefix(const std::filesystem::path &filePath,
                              const std::string &prefix,
                              const std::string &label)
{
    std::ifstream ifs(filePath, std::ios::binary);
    ensure(ifs.good(), "failed to open " + label + ": " + filePath.string());

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return;
        }
    }
    ensure(false, label + " missing prefix: " + prefix);
}

Matrix toMatrix(const std::vector<storage::Row> &rows)
{
    Matrix values;
    values.reserve(rows.size());
    for (const auto &row : rows) {
        values.push_back(row.values);
    }
    return values;
}

std::string matrixToText(const Matrix &rows)
{
    std::vector<std::string> lines;
    lines.reserve(rows.size());
    for (const auto &row : rows) {
        lines.push_back("[" + storage::join(row, ", ") + "]");
    }
    return "[" + storage::join(lines, ", ") + "]";
}

void expectRowsEqual(const std::vector<storage::Row> &actualRows,
                     const Matrix &expectedRows,
                     const std::string &label)
{
    const auto actual = toMatrix(actualRows);
    ensure(actual == expectedRows,
           label + " mismatch, expected=" + matrixToText(expectedRows) + ", actual=" + matrixToText(actual));
}

storage::Table::SelectOptions orderedBy(const std::string &column)
{
    storage::Table::SelectOptions options;
    options.orderByColumn = column;
    return options;
}

std::vector<storage::Row> selectRows(DatabaseManager *databaseManager,
                                     const std::string &dbName,
                                     const std::string &tableName,
                                     const std::vector<std::string> &columns,
                                     const std::vector<storage::Table::WhereCondition> &whereConditions = {},
                                     const std::vector<storage::Table::QueryConstraint> &queryConstraints = {},
                                     const storage::Table::SelectOptions &options = storage::Table::SelectOptions())
{
    return databaseManager->selectRows(dbName, tableName, columns, whereConditions, queryConstraints, options);
}

void verifyDatabaseArtifacts(const TestLayout &layout,
                             const std::string &dbName,
                             const std::string &studentTableName)
{
    const auto tdfFile = layout.dbDir / (studentTableName + ".tdf");
    const auto trdFile = layout.dbDir / (studentTableName + ".trd");
    const auto ticFile = layout.dbDir / (studentTableName + ".tic");
    const auto tidFile = layout.dbDir / (studentTableName + ".tid");
    const auto nameIndexFile = layout.dbDir / (studentTableName + ".Name.nidx");
    const auto deptIndexFile = layout.dbDir / (studentTableName + ".Dept.nidx");

    expectFileExists(layout.catalogFile, "catalog file");
    expectFileExists(layout.tableBlockFile, "table block file");
    expectFileExists(tdfFile, "student .tdf");
    expectFileExists(trdFile, "student .trd");
    expectFileExists(ticFile, "student .tic");
    expectFileExists(tidFile, "student .tid");
    expectFileExists(nameIndexFile, "student Name index");
    expectFileExists(deptIndexFile, "student Dept index");

    expectFileContainsLine(layout.catalogFile, "name=" + dbName, "catalog file");
    expectFileContainsLine(layout.tableBlockFile, "name=" + studentTableName, "table block file");

    expectFileContainsLine(tdfFile, "schema_version=2", "student .tdf");
    expectFileContainsLine(tdfFile, "table=" + studentTableName, "student .tdf");
    expectFileContainsLine(tdfFile, "columns=ID:TEXT|Name:TEXT|Dept:TEXT", "student .tdf");
    expectFileContainsLine(tdfFile,
                           "index_definitions=PRIMARY(ID):BTREE:" + studentTableName + ".tid",
                           "student .tdf");
    expectFileContainsLine(tdfFile,
                           "index_reserved=Name:BTREE:" + studentTableName + ".Name.nidx",
                           "student .tdf");
    expectFileContainsLine(tdfFile,
                           "index_reserved=Dept:BTREE:" + studentTableName + ".Dept.nidx",
                           "student .tdf");

    expectFileContainsLine(ticFile, "constraint=PRIMARY_KEY(ID)", "student .tic");
    expectFileContainsLine(ticFile, "constraint=UNIQUE(Name)", "student .tic");
    expectFileContainsLine(ticFile, "constraint=NOT_NULL(Dept)", "student .tic");
    expectFileContainsLine(ticFile, "index_reserved=Name:" + studentTableName + ".Name.nidx", "student .tic");
    expectFileContainsLine(ticFile, "index_reserved=Dept:" + studentTableName + ".Dept.nidx", "student .tic");

    expectFileContainsLine(tidFile, "TID_PAGED_V3", "student .tid");
    expectFileContainsPrefix(tidFile, "root_page=", "student .tid");
    expectFileContainsPrefix(tidFile, "ENTRY|S001|", "student .tid");
}

void runStudentCrudChain(DatabaseManager *databaseManager,
                         const TestLayout &layout,
                         const std::string &dbName,
                         const std::string &studentTableName)
{
    ensure(databaseManager->createTable(dbName, studentTableName, {"ID", "Name", "Dept"}),
           "create student table failed");

    ensure(databaseManager->insertRow(dbName, studentTableName, {"S001", "Alice", "CS"}),
           "insert S001 failed");
    ensure(databaseManager->insertRow(dbName, studentTableName, {"S002", "Bob", "Math"}),
           "insert S002 failed");
    ensure(databaseManager->insertRow(dbName, studentTableName, {"S003", "Carol", "CS"}),
           "insert S003 failed");

    expectRowsEqual(
        selectRows(databaseManager, dbName, studentTableName, {"ID", "Name", "Dept"}, {}, {}, orderedBy("ID")),
        {{"S001", "Alice", "CS"}, {"S002", "Bob", "Math"}, {"S003", "Carol", "CS"}},
        "student initial query");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   studentTableName,
                   {"ID", "Name"},
                   {storage::Table::WhereCondition{"Dept", storage::Table::CompareOp::EQ, "CS"}},
                   {},
                   orderedBy("ID")),
        {{"S001", "Alice"}, {"S003", "Carol"}},
        "student equality query");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   studentTableName,
                   {"ID", "Name"},
                   {storage::Table::WhereCondition{"Name", storage::Table::CompareOp::LIKE, "A%"}},
                   {},
                   orderedBy("ID")),
        {{"S001", "Alice"}},
        "student LIKE query");

    auto inCondition = storage::Table::WhereCondition{"ID", storage::Table::CompareOp::IN, ""};
    inCondition.values = {"S001", "S003"};
    expectRowsEqual(
        selectRows(databaseManager, dbName, studentTableName, {"ID"}, {inCondition}, {}, orderedBy("ID")),
        {{"S001"}, {"S003"}},
        "student IN query");

    ensure(databaseManager->addColumnConstraint(
               dbName,
               studentTableName,
               storage::Table::ColumnConstraintSpec{"Name", false, true, false, ""}),
           "add UNIQUE(Name) failed");
    ensure(databaseManager->addColumnConstraint(
               dbName,
               studentTableName,
               storage::Table::ColumnConstraintSpec{"Dept", true, false, false, ""}),
           "add NOT NULL(Dept) failed");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   studentTableName,
                   {"ID", "Name"},
                   {},
                   {storage::Table::QueryConstraint{"Name", storage::Table::ConstraintType::UNIQUE, true}},
                   orderedBy("ID")),
        {{"S001", "Alice"}, {"S002", "Bob"}, {"S003", "Carol"}},
        "student UNIQUE constraint query");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   studentTableName,
                   {"ID", "Dept"},
                   {},
                   {storage::Table::QueryConstraint{"Dept", storage::Table::ConstraintType::NOT_NULL, true}},
                   orderedBy("ID")),
        {{"S001", "CS"}, {"S002", "Math"}, {"S003", "CS"}},
        "student NOT NULL constraint query");

    ensure(!databaseManager->insertRow(dbName, studentTableName, {"S010", "Alice", "Physics"}),
           "duplicate Name should be rejected");
    ensure(!databaseManager->insertRow(dbName, studentTableName, {"S011", "Eve", ""}),
           "empty Dept should be rejected");

    ensure(databaseManager->updateRowByPrimaryKey(dbName, studentTableName, "S002", {"S002", "Bobby", "EE"}),
           "update S002 failed");
    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   studentTableName,
                   {"ID", "Name", "Dept"},
                   {storage::Table::WhereCondition{"ID", storage::Table::CompareOp::EQ, "S002"}}),
        {{"S002", "Bobby", "EE"}},
        "student update query");

    ensure(databaseManager->insertRow(dbName, studentTableName, {"S004", "Doris", "CS"}),
           "insert S004 failed");
    ensure(databaseManager->deleteRowByPrimaryKey(dbName, studentTableName, "S003"),
           "delete S003 failed");

    expectRowsEqual(
        selectRows(databaseManager, dbName, studentTableName, {"ID", "Name", "Dept"}, {}, {}, orderedBy("ID")),
        {{"S001", "Alice", "CS"}, {"S002", "Bobby", "EE"}, {"S004", "Doris", "CS"}},
        "student final query");

    verifyDatabaseArtifacts(layout, dbName, studentTableName);
}

void runConstraintFlow(DatabaseManager *databaseManager,
                       const TestLayout &layout,
                       const std::string &dbName,
                       const std::string &profileTableName)
{
    ensure(databaseManager->createTable(dbName, profileTableName, {"ID", "Email", "Tag"}),
           "create profile table failed");

    ensure(databaseManager->insertRow(dbName, profileTableName, {"P001", "alice@db.local", "seed"}),
           "insert P001 failed");
    ensure(databaseManager->insertRow(dbName, profileTableName, {"P002", "bob@db.local", "vip"}),
           "insert P002 failed");

    ensure(databaseManager->addColumnConstraint(
               dbName,
               profileTableName,
               storage::Table::ColumnConstraintSpec{"Email", false, true, false, ""}),
           "add UNIQUE(Email) failed");
    ensure(databaseManager->addColumnConstraint(
               dbName,
               profileTableName,
               storage::Table::ColumnConstraintSpec{"Tag", false, false, true, "normal"}),
           "add DEFAULT(Tag) failed");

    ensure(databaseManager->insertRow(dbName, profileTableName, {"P003", "carol@db.local"}),
           "insert P003 with default Tag failed");
    ensure(!databaseManager->insertRow(dbName, profileTableName, {"P004", "alice@db.local", "dup"}),
           "duplicate Email should be rejected");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   profileTableName,
                   {"ID", "Tag"},
                   {},
                   {storage::Table::QueryConstraint{"Tag", storage::Table::ConstraintType::DEFAULT_VALUE, true}},
                   orderedBy("ID")),
        {{"P003", "normal"}},
        "profile DEFAULT constraint query");

    const auto ticFile = layout.dbDir / (profileTableName + ".tic");
    expectFileContainsLine(ticFile, "constraint=UNIQUE(Email)", "profile .tic");
    expectFileContainsLine(ticFile, "constraint=DEFAULT(Tag|normal)", "profile .tic");
}

void runJoinFlow(DatabaseManager *databaseManager,
                 const std::string &dbName,
                 const std::string &studentTableName,
                 const std::string &scholarshipTableName)
{
    ensure(databaseManager->createTable(dbName, scholarshipTableName, {"StudentID", "Level", "Amount"}),
           "create scholarship table failed");
    ensure(databaseManager->insertRow(dbName, scholarshipTableName, {"S001", "A", "1500"}),
           "insert scholarship S001 failed");
    ensure(databaseManager->insertRow(dbName, scholarshipTableName, {"S004", "A", "2000"}),
           "insert scholarship S004 failed");

    DatabaseManager::JoinQuery innerJoinQuery;
    innerJoinQuery.baseTable = studentTableName;
    innerJoinQuery.baseAlias = "s";

    DatabaseManager::JoinSpec scholarshipJoin;
    scholarshipJoin.type = DatabaseManager::JoinType::INNER_JOIN;
    scholarshipJoin.tableName = scholarshipTableName;
    scholarshipJoin.alias = "c";
    scholarshipJoin.onConditions.push_back({{"s", "ID"}, {"c", "StudentID"}, storage::Table::CompareOp::EQ});
    innerJoinQuery.joins.push_back(scholarshipJoin);
    innerJoinQuery.projections = {
        {{"s", "ID"}, "student_id"},
        {{"s", "Name"}, "student_name"},
        {{"c", "Level"}, "level"},
        {{"c", "Amount"}, "amount"}
    };
    innerJoinQuery.postFilters.push_back({{"c", "Amount"}, storage::Table::CompareOp::GT, "1000", "", {}});
    innerJoinQuery.options.orderByOutput = "student_id";

    const auto innerJoinRows = databaseManager->selectJoinRows(dbName, innerJoinQuery);
    ensure(innerJoinRows.columns == std::vector<std::string>({"student_id", "student_name", "level", "amount"}),
           "inner join columns mismatch");
    expectRowsEqual(innerJoinRows.rows,
                    {{"S001", "Alice", "A", "1500"}, {"S004", "Doris", "A", "2000"}},
                    "inner join query");

    DatabaseManager::JoinQuery leftJoinQuery;
    leftJoinQuery.baseTable = studentTableName;
    leftJoinQuery.baseAlias = "s";

    DatabaseManager::JoinSpec leftJoin = scholarshipJoin;
    leftJoin.type = DatabaseManager::JoinType::LEFT_JOIN;
    leftJoinQuery.joins.push_back(leftJoin);
    leftJoinQuery.projections = {
        {{"s", "ID"}, "student_id"},
        {{"s", "Name"}, "student_name"},
        {{"c", "Level"}, "level"}
    };
    leftJoinQuery.options.orderByOutput = "student_id";

    const auto leftJoinRows = databaseManager->selectJoinRows(dbName, leftJoinQuery);
    expectRowsEqual(leftJoinRows.rows,
                    {{"S001", "Alice", "A"}, {"S002", "Bobby", ""}, {"S004", "Doris", "A"}},
                    "left join query");
}

void runSubqueryFlow(DatabaseManager *databaseManager,
                     const TestLayout &layout,
                     const std::string &dbName,
                     const std::string &studentTableName,
                     const std::string &scholarshipTableName)
{
    auto studentTable = storage::Table::load(layout.dbDir, studentTableName);

    storage::Table::SubquerySpec scalarSpec;
    scalarSpec.dbName = dbName;
    scalarSpec.tableName = scholarshipTableName;
    scalarSpec.targetColumns = {"Amount"};
    scalarSpec.aggregates = {{storage::Table::AggregateOp::MAX, "Amount"}};

    const auto scalarResult = studentTable.evaluateSubquery(scalarSpec);
    ensure(scalarResult.kind == storage::Table::SubqueryKind::Scalar, "scalar subquery kind mismatch");
    ensure(scalarResult.scalarValue == "2000", "scalar subquery MAX(Amount) mismatch");

    expectRowsEqual(
        selectRows(databaseManager,
                   dbName,
                   scholarshipTableName,
                   {"StudentID", "Amount"},
                   {storage::Table::WhereCondition{"Amount", storage::Table::CompareOp::EQ, scalarResult.scalarValue}},
                   {},
                   orderedBy("StudentID")),
        {{"S004", "2000"}},
        "scalar subquery driven select");

    storage::Table::SubquerySpec inSpec;
    inSpec.dbName = dbName;
    inSpec.tableName = scholarshipTableName;
    inSpec.targetColumns = {"StudentID"};
    inSpec.whereConditions = {storage::Table::WhereCondition{"Level", storage::Table::CompareOp::EQ, "A"}};

    const auto inResult = studentTable.evaluateSubquery(inSpec);
    ensure(inResult.kind == storage::Table::SubqueryKind::RowSet, "IN subquery kind mismatch");
    ensure(inResult.rows.size() == 2, "IN subquery row count mismatch");

    auto inCondition = storage::Table::WhereCondition{"ID", storage::Table::CompareOp::IN, ""};
    inCondition.values = inResult.rows;
    expectRowsEqual(
        studentTable.select({"ID", "Name"}, {inCondition}, orderedBy("ID")),
        {{"S001", "Alice"}, {"S004", "Doris"}},
        "IN subquery select");

    auto notInCondition = inCondition;
    notInCondition.isSubqueryNot = true;
    expectRowsEqual(
        studentTable.select({"ID", "Name"}, {notInCondition}, orderedBy("ID")),
        {{"S002", "Bobby"}},
        "NOT IN subquery select");

    std::vector<std::string> matchedIds;
    for (const auto &row : studentTable.select({"*"}, {}, orderedBy("ID"))) {
        storage::Table::SubquerySpec correlatedSpec;
        correlatedSpec.dbName = dbName;
        correlatedSpec.tableName = scholarshipTableName;
        correlatedSpec.targetColumns = {"StudentID"};
        correlatedSpec.whereConditions = {
            storage::Table::WhereCondition{"StudentID", storage::Table::CompareOp::EQ, "$outer.ID"}};

        const auto correlated = studentTable.evaluateSubqueryForRow(correlatedSpec, row);
        if (!correlated.rows.empty()) {
            matchedIds.push_back(row.values.front());
        }
    }

    ensure(matchedIds == std::vector<std::string>({"S001", "S004"}),
           "correlated EXISTS subquery mismatch");
}

} // namespace

class TestCore {
public:
    TestCore()
        : storageManager(new StorageManager(reinterpret_cast<Core *>(this)))
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
        const std::string dbName = "StorageChainDB";
        const std::string studentTableName = "Students";
        const std::string profileTableName = "Profiles";
        const std::string scholarshipTableName = "Scholarships";

        const auto layout = buildLayout(dbName);
        ensure(std::filesystem::exists(layout.storageDir) && std::filesystem::is_directory(layout.storageDir),
               "storage directory not found: " + layout.storageDir.string());
        std::filesystem::current_path(layout.storageDir);

        if (std::filesystem::exists(layout.dbDir)) {
            std::filesystem::remove_all(layout.dbDir);
        }
        removeDatabaseFromCatalog(layout.catalogFile, dbName);

        TestCore core;
        auto *systemCatalogManager = core.getStorageManager()->getSystemCatalogManager();
        auto *databaseManager = core.getStorageManager()->getDatabaseManager();
        ensure(systemCatalogManager != nullptr, "systemCatalogManager is null");
        ensure(databaseManager != nullptr, "databaseManager is null");

        DatabaseBlock dbInfo;
        dbInfo.setName(toArray<128>(dbName));
        ensure(systemCatalogManager->createDatabase(dbInfo), "createDatabase failed");
        ensure(std::filesystem::exists(layout.dbDir), "database directory was not created");

        runStudentCrudChain(databaseManager, layout, dbName, studentTableName);
        runConstraintFlow(databaseManager, layout, dbName, profileTableName);
        runJoinFlow(databaseManager, dbName, studentTableName, scholarshipTableName);
        runSubqueryFlow(databaseManager, layout, dbName, studentTableName, scholarshipTableName);

        // PK smallest key insertion test
        {
            auto pkTable = storage::Table::create(layout.dbDir, "PKTest", {"K","V"});
            for (int i = 5; i <= 7; ++i)
                ensure(databaseManager->insertRow(dbName, "PKTest", {std::to_string(i),"v"+std::to_string(i)}),
                       "PKTest insert big failed");
            // Insert keys smaller than all existing keys
            ensure(databaseManager->insertRow(dbName, "PKTest", {"1","v1"}),
                   "PKTest insert 1 failed");
            ensure(databaseManager->insertRow(dbName, "PKTest", {"0","v0"}),
                   "PKTest insert 0 failed");
            ensure(databaseManager->insertRow(dbName, "PKTest", {"-1","v-1"}),
                   "PKTest insert -1 failed");
            ensure(databaseManager->insertRow(dbName, "PKTest", {"a","va"}),
                   "PKTest insert a failed");

            auto pkLoaded = storage::Table::load(layout.dbDir, "PKTest");
            auto pkAll = pkLoaded.select({"*"});
            ensure(pkAll.size() == 7, "PKTest count mismatch (" + std::to_string(pkAll.size()) + ")");

            // EQ smallest
            auto r1 = pkLoaded.select({"*"}, {{"K", storage::Table::CompareOp::EQ, "-1"}});
            ensure(r1.size() == 1 && r1[0].values[1] == "v-1", "PKTest EQ -1");

            // Update smallest
            auto pu = storage::Table::load(layout.dbDir, "PKTest");
            pu.updateByPrimaryKey("-1", {"-1", "v-1_updated"});

            // Delete smallest
            auto pd = storage::Table::load(layout.dbDir, "PKTest");
            pd.deleteByPrimaryKey("0");
            auto pv = storage::Table::load(layout.dbDir, "PKTest");
            auto pAfter = pv.select({"*"});
            ensure(pAfter.size() == 6, "PKTest after delete mismatch (" + std::to_string(pAfter.size()) + ")");
        }

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



