#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace storage {

class TableVersionManager {
public:
    static void initialize(const std::filesystem::path& dbPath,
                           const std::string& tableName);
    static std::uint64_t getVersion(const std::filesystem::path& dbPath,
                                    const std::string& tableName);
    static std::uint64_t incrementVersion(const std::filesystem::path& dbPath,
                                          const std::string& tableName);

private:
    static std::filesystem::path versionFilePath(const std::filesystem::path& dbPath,
                                                  const std::string& tableName);
};

} // namespace storage
