#include "DatabaseManager.h"

namespace storage {

DatabaseManager::DatabaseManager(std::filesystem::path rootPath)
    : rootPath_(std::move(rootPath)) {
    std::filesystem::create_directories(rootPath_);
}

void DatabaseManager::createDatabase(const std::string& dbName) {
    ensure(!dbName.empty(), "database name cannot be empty");

    const auto dbPath = databasePathOf(dbName);
    ensure(!std::filesystem::exists(dbPath), "database already exists: " + dbName);

    std::filesystem::create_directories(dbPath);
}

void DatabaseManager::useDatabase(const std::string& dbName) {
    ensure(!dbName.empty(), "database name cannot be empty");

    const auto dbPath = databasePathOf(dbName);
    ensure(std::filesystem::exists(dbPath) && std::filesystem::is_directory(dbPath),
           "database does not exist: " + dbName);

    currentDbName_ = dbName;
    currentDbPath_ = dbPath;
    openedTables_.clear();
}

void DatabaseManager::createTable(const std::string& tableName,
                                  const std::vector<std::string>& columns) {
    ensureDatabaseSelected();

    auto table = std::make_shared<Table>(Table::create(currentDbPath_, tableName, columns));
    openedTables_[tableName] = std::move(table);
}

void DatabaseManager::insert(const std::string& tableName,
                             const std::vector<std::string>& values) {
    ensureDatabaseSelected();
    openTable(tableName)->insert(values);
}

bool DatabaseManager::hasSelectedDatabase() const {
    return !currentDbName_.empty();
}

std::string DatabaseManager::currentDatabaseName() const {
    return currentDbName_;
}

std::filesystem::path DatabaseManager::databasePathOf(const std::string& dbName) const {
    return rootPath_ / dbName;
}

void DatabaseManager::ensureDatabaseSelected() const {
    ensure(hasSelectedDatabase(), "no database selected, call useDatabase first");
}

std::shared_ptr<Table> DatabaseManager::openTable(const std::string& tableName) {
    auto it = openedTables_.find(tableName);
    if (it != openedTables_.end()) {
        return it->second;
    }

    auto table = std::make_shared<Table>(Table::load(currentDbPath_, tableName));
    openedTables_[tableName] = table;
    return table;
}

} // namespace storage
