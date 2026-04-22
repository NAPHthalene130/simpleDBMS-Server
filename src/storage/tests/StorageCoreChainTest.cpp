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

    ensure(std::filesystem::exists(dbFile), ".db file not found");
    ensure(std::filesystem::exists(tdfFile), ".tdf file not found");
    ensure(std::filesystem::exists(trdFile), ".trd file not found");

    std::ifstream tdf(tdfFile);
    std::ifstream trd(trdFile);
    std::string tdfLine1;
    std::string tdfLine2;
    std::string trdLine1;
    std::getline(tdf, tdfLine1);
    std::getline(tdf, tdfLine2);
    std::getline(trd, trdLine1);

    ensure(tdfLine1 == "table=" + tableName, "tdf table line mismatch");
    ensure(tdfLine2 == "columns=A|B|C", "tdf columns line mismatch");
    ensure(trdLine1 == "v1|v2|v3", "trd row line mismatch");

    std::cout << "StorageCoreChainTest passed." << std::endl;
    return 0;
}
