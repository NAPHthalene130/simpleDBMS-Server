#include "SystemCatalogManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "log/LogWriter.h"

namespace {

/**
 * @brief 获取数据存储根目录的绝对路径
 * @author NAPH130
 * @return 数据根目录路径（基于源文件位置，不依赖进程工作目录）
 * @details 本文件位于 src/storage/manager/，数据目录期望在 src/storage/data/。
 *          通过 __FILE__ 向上两级获得 storage/ 再拼接 data/，确保无论从何处启动服务端都写入正确位置。
 */
const std::filesystem::path &getDataRootPath()
{
    static const std::filesystem::path dataRoot =
        (std::filesystem::path(__FILE__).parent_path().parent_path() / "data").lexically_normal();
    return dataRoot;
}

const std::filesystem::path &getDatabaseCatalogPath()
{
    static const std::filesystem::path catalogPath = getDataRootPath() / "database.db";
    return catalogPath;
}

constexpr const char *kCatalogBlockSeparator = "---DB_BLOCK---";

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

DateTime buildCurrentDateTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
    localtime_s(&localTime, &currentTime);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch())
                              % 1000;

    DateTime dateTime;
    dateTime.setYear(static_cast<std::uint16_t>(localTime.tm_year + 1900));
    dateTime.setMonth(static_cast<std::uint16_t>(localTime.tm_mon + 1));
    dateTime.setDayOfWeek(static_cast<std::uint16_t>(localTime.tm_wday));
    dateTime.setDay(static_cast<std::uint16_t>(localTime.tm_mday));
    dateTime.setHour(static_cast<std::uint16_t>(localTime.tm_hour));
    dateTime.setMinute(static_cast<std::uint16_t>(localTime.tm_min));
    dateTime.setSecond(static_cast<std::uint16_t>(localTime.tm_sec));
    dateTime.setMilliseconds(static_cast<std::uint16_t>(milliseconds.count()));
    return dateTime;
}

std::string dateTimeToString(const DateTime &dateTime)
{
    std::ostringstream oss;
    oss << dateTime.getYear() << ','
        << dateTime.getMonth() << ','
        << dateTime.getDayOfWeek() << ','
        << dateTime.getDay() << ','
        << dateTime.getHour() << ','
        << dateTime.getMinute() << ','
        << dateTime.getSecond() << ','
        << dateTime.getMilliseconds();
    return oss.str();
}

bool tryParseDateTime(const std::string &text, DateTime &dateTime)
{
    std::istringstream iss(text);
    std::string token;
    std::vector<int> values;
    try {
        while (std::getline(iss, token, ',')) {
            if (token.empty()) {
                return false;
            }
            values.push_back(std::stoi(token));
        }
    } catch (...) {
        return false;
    }
    if (values.size() != 8) {
        return false;
    }
    dateTime.setYear(static_cast<std::uint16_t>(values[0]));
    dateTime.setMonth(static_cast<std::uint16_t>(values[1]));
    dateTime.setDayOfWeek(static_cast<std::uint16_t>(values[2]));
    dateTime.setDay(static_cast<std::uint16_t>(values[3]));
    dateTime.setHour(static_cast<std::uint16_t>(values[4]));
    dateTime.setMinute(static_cast<std::uint16_t>(values[5]));
    dateTime.setSecond(static_cast<std::uint16_t>(values[6]));
    dateTime.setMilliseconds(static_cast<std::uint16_t>(values[7]));
    return true;
}

bool tryParseBool(const std::string &text, bool &value)
{
    if (text == "1" || text == "true" || text == "TRUE") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE") {
        value = false;
        return true;
    }
    return false;
}

std::string blockName(const DatabaseBlock &block)
{
    return arrayToString(block.getName());
}

DatabaseBlock normalizeBlock(const DatabaseBlock &source,
                             const std::string &dbName,
                             const std::filesystem::path &dbFolderPath)
{
    DatabaseBlock normalized = source;
    normalized.setName(stringToArray<128>(dbName));
    normalized.setFileName(stringToArray<256>(dbFolderPath.string()));
    if (normalized.getCreateTime().getYear() == 0) {
        normalized.setCreateTime(buildCurrentDateTime());
    }
    return normalized;
}

bool parseCatalogBlock(const std::vector<std::string> &lines, DatabaseBlock &out)
{
    if (lines.empty()) {
        return false;
    }
    if (lines.size() == 1 && lines.front().find('=') == std::string::npos) {
        const std::string dbName = lines.front();
        if (dbName.empty()) {
            return false;
        }
        DatabaseBlock block;
        block.setName(stringToArray<128>(dbName));
        block.setType(false);
        block.setFileName(stringToArray<256>((getDataRootPath() / dbName).string()));
        block.setCreateTime(buildCurrentDateTime());
        out = block;
        return true;
    }

    std::string name;
    bool type = false;
    bool hasType = false;
    std::string fileName;
    DateTime createTime;
    bool hasCreateTime = false;

    for (const auto &line : lines) {
        const auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        if (key == "name") {
            name = value;
        } else if (key == "type") {
            hasType = tryParseBool(value, type);
        } else if (key == "filename") {
            fileName = value;
        } else if (key == "ctime") {
            hasCreateTime = tryParseDateTime(value, createTime);
        }
    }

    if (name.empty()) {
        return false;
    }
    DatabaseBlock block;
    block.setName(stringToArray<128>(name));
    block.setType(hasType ? type : false);
    block.setFileName(stringToArray<256>(fileName.empty() ? (getDataRootPath() / name).string() : fileName));
    block.setCreateTime(hasCreateTime ? createTime : buildCurrentDateTime());
    out = block;
    return true;
}

std::vector<DatabaseBlock> readDatabaseCatalog()
{
    std::vector<DatabaseBlock> blocks;
    const auto &catalogPath = getDatabaseCatalogPath();
    if (!std::filesystem::exists(catalogPath)) {
        return blocks;
    }

    std::ifstream ifs(catalogPath);
    if (!ifs.good()) {
        return blocks;
    }

    std::string line;
    std::vector<std::string> blockLines;
    while (std::getline(ifs, line)) {
        if (line == kCatalogBlockSeparator) {
            DatabaseBlock block;
            if (parseCatalogBlock(blockLines, block)) {
                blocks.push_back(block);
            }
            blockLines.clear();
            continue;
        }
        if (line.empty()) {
            continue;
        }
        blockLines.push_back(line);
    }
    DatabaseBlock block;
    if (parseCatalogBlock(blockLines, block)) {
        blocks.push_back(block);
    }
    return blocks;
}

bool writeDatabaseCatalog(const std::vector<DatabaseBlock> &blocks)
{
    std::filesystem::create_directories(getDataRootPath());
    std::ofstream ofs(getDatabaseCatalogPath(), std::ios::trunc);
    if (!ofs.good()) {
        return false;
    }
    for (const auto &block : blocks) {
        const std::string dbName = blockName(block);
        if (dbName.empty()) {
            continue;
        }
        ofs << "name=" << dbName << '\n';
        ofs << "type=" << (block.getType() ? "1" : "0") << '\n';
        ofs << "filename=" << arrayToString(block.getFileName()) << '\n';
        ofs << "ctime=" << dateTimeToString(block.getCreateTime()) << '\n';
        ofs << kCatalogBlockSeparator << '\n';
    }
    return true;
}

} // namespace

SystemCatalogManager::SystemCatalogManager(Core *core)
    : core(core)
{
}

bool SystemCatalogManager::createDatabase(DatabaseBlock dbInfo)
{
    try {
        const std::string dbName = arrayToString(dbInfo.getName());
        if (dbName.empty()) {
            LogWriter::warning("storage", "SystemCatalogManager", "createDatabase", "Rejected empty database name.");
            return false;
        }

        const auto &dbRootPath = getDataRootPath();
        const auto dbFolderPath = dbRootPath / dbName;
        const auto dbDescFilePath = dbFolderPath / (dbName + ".tb");
        const auto dbLogFilePath = dbFolderPath / (dbName + ".log");
        auto databaseBlocks = readDatabaseCatalog();
        const bool inCatalog = std::any_of(databaseBlocks.begin(),
                                           databaseBlocks.end(),
                                           [&dbName](const DatabaseBlock &block) {
                                               return blockName(block) == dbName;
                                           });

        if (std::filesystem::exists(dbFolderPath) || inCatalog) {
            LogWriter::warning("storage",
                               "SystemCatalogManager",
                               "createDatabase",
                               std::string("Database already exists: ") + dbName);
            return false;
        }

        std::filesystem::create_directories(dbFolderPath);

        std::ofstream dbDescFile(dbDescFilePath, std::ios::app);
        if (!dbDescFile.good()) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to create database descriptor for ") + dbName);
            return false;
        }

        std::ofstream dbLogFile(dbLogFilePath, std::ios::app);
        if (!dbLogFile.good()) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to create database log file for ") + dbName);
            return false;
        }

        databaseBlocks.push_back(normalizeBlock(dbInfo, dbName, dbFolderPath));
        if (!writeDatabaseCatalog(databaseBlocks)) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to update database catalog for ") + dbName);
            return false;
        }
        LogWriter::info("storage",
                        "SystemCatalogManager",
                        "createDatabase",
                        std::string("Database created successfully: ") + dbName);
        return true;
    } catch (...) {
        LogWriter::error("storage", "SystemCatalogManager", "createDatabase", "Unknown exception while creating database.");
        return false;
    }
}

bool SystemCatalogManager::dropDatabase(std::string dbName)
{
    if (dbName.empty()) {
        LogWriter::warning("storage", "SystemCatalogManager", "dropDatabase", "Rejected empty database name.");
        return false;
    }

    try {
        const auto &dbRootPath = getDataRootPath();
        const auto dbFolderPath = dbRootPath / dbName;
        auto databaseBlocks = readDatabaseCatalog();
        const auto newEnd = std::remove_if(databaseBlocks.begin(),
                                           databaseBlocks.end(),
                                           [&dbName](const DatabaseBlock &block) {
                                               return blockName(block) == dbName;
                                           });
        const bool removedCatalog = newEnd != databaseBlocks.end();
        databaseBlocks.erase(newEnd, databaseBlocks.end());

        const bool removedFolder = std::filesystem::exists(dbFolderPath)
                                   && std::filesystem::remove_all(dbFolderPath) > 0;
        const bool catalogUpdated = removedCatalog ? writeDatabaseCatalog(databaseBlocks) : true;
        if (!catalogUpdated) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "dropDatabase",
                             std::string("Failed to update database catalog while dropping ") + dbName);
            return false;
        }
        LogWriter::info("storage",
                        "SystemCatalogManager",
                        "dropDatabase",
                        std::string("Drop database result for ") + dbName + ": "
                            + ((removedFolder || removedCatalog) ? "success" : "not found"));
        return removedFolder || removedCatalog;
    } catch (...) {
        LogWriter::error("storage", "SystemCatalogManager", "dropDatabase", "Unknown exception while dropping database.");
        return false;
    }
}

std::vector<DatabaseBlock> SystemCatalogManager::getAllDatabases()
{
    std::vector<DatabaseBlock> blocks;
    const auto &dbRootPath = getDataRootPath();

    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        return blocks;
    }

    for (const auto &block : readDatabaseCatalog()) {
        const std::string dbName = blockName(block);
        const auto dbFolderPath = dbRootPath / dbName;
        if (!std::filesystem::exists(dbFolderPath) || !std::filesystem::is_directory(dbFolderPath)) {
            continue;
        }
        blocks.push_back(normalizeBlock(block, dbName, dbFolderPath));
    }
    LogWriter::debug("storage",
                     "SystemCatalogManager",
                     "getAllDatabases",
                     std::string("Enumerated database count=") + std::to_string(blocks.size()));
    return blocks;
}

bool SystemCatalogManager::checkDbExists(std::string dbName)
{
    if (dbName.empty()) {
        LogWriter::warning("storage", "SystemCatalogManager", "checkDbExists", "Rejected empty database name.");
        return false;
    }

    const auto &dbRootPath = getDataRootPath();
    const auto dbFolderPath = dbRootPath / dbName;
    const auto databaseBlocks = readDatabaseCatalog();
    const bool inCatalog = std::any_of(databaseBlocks.begin(),
                                       databaseBlocks.end(),
                                       [&dbName](const DatabaseBlock &block) {
                                           return blockName(block) == dbName;
                                       });
    const bool exists = std::filesystem::exists(dbFolderPath) && std::filesystem::is_directory(dbFolderPath)
                        && inCatalog;
    LogWriter::debug("storage",
                      "SystemCatalogManager",
                      "checkDbExists",
                     std::string("Database existence check for ") + dbName + ": " + (exists ? "true" : "false"));
    return exists;
}

uInt64 SystemCatalogManager::getDatabaseVersion(std::string dbName)
{
    (void)dbName;
    return 0;
}

void SystemCatalogManager::addDatabaseVersion(std::string dbName)
{
    (void)dbName;
}
