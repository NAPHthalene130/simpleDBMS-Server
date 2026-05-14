#include "TableVersionManager.h"
#include "StorageCommon.h"

#include <fstream>

namespace storage {

std::filesystem::path TableVersionManager::versionFilePath(const std::filesystem::path& dbPath,
                                                            const std::string& tableName) {
    return dbPath / (tableName + ".ver");
}

void TableVersionManager::initialize(const std::filesystem::path& dbPath,
                                     const std::string& tableName) {
    const auto path = versionFilePath(dbPath, tableName);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ensure(ofs.good(), "failed to create version file: " + path.string());
    const std::uint64_t version = 0;
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
}

std::uint64_t TableVersionManager::getVersion(const std::filesystem::path& dbPath,
                                               const std::string& tableName) {
    const auto path = versionFilePath(dbPath, tableName);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) {
        return 0;
    }
    std::uint64_t version = 0;
    ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
    return version;
}

std::uint64_t TableVersionManager::incrementVersion(const std::filesystem::path& dbPath,
                                                     const std::string& tableName) {
    std::uint64_t version = getVersion(dbPath, tableName) + 1;
    const auto path = versionFilePath(dbPath, tableName);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ensure(ofs.good(), "failed to write version file: " + path.string());
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
    return version;
}

} // namespace storage
