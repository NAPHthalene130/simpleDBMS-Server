#include "FileManager.h"

#include <cstring>
#include <fstream>

#include "log/LogWriter.h"

FileManager::FileManager(Core *core)
    : core(core)
{
    LogWriter::debug("storage", "FileManager", "FileManager", "File manager initialized.");
}

std::string FileManager::readPage(const std::filesystem::path& filePath, std::uint32_t pageId) {
    if (!std::filesystem::exists(filePath)) return {};
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.good()) return {};
    const std::streamoff offset = static_cast<std::streamoff>(pageId) * kPageSize;
    ifs.seekg(0, std::ios::end);
    std::streamoff fileSize = ifs.tellg();
    if (offset >= fileSize) return {};
    ifs.seekg(offset, std::ios::beg);
    if (!ifs.good()) return {};
    std::string content(kPageSize, '\0');
    ifs.read(content.data(), kPageSize);
    content.resize(static_cast<std::size_t>(ifs.gcount()));
    return content;
}

bool FileManager::writePage(const std::filesystem::path& filePath, std::uint32_t pageId, const std::string& content) {
    std::fstream fs;
    fs.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs.good()) {
        fs.clear();
        fs.open(filePath, std::ios::out | std::ios::binary);
        fs.close();
        fs.open(filePath, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!fs.good()) return false;
    const std::streamoff offset = static_cast<std::streamoff>(pageId) * kPageSize;
    fs.seekp(offset, std::ios::beg);
    if (!fs.good()) return false;
    std::string padded = content;
    if (padded.size() > kPageSize) padded.resize(kPageSize);
    fs.write(padded.data(), static_cast<std::streamsize>(padded.size()));
    return fs.good();
}

bool FileManager::writeHeader(const std::filesystem::path& filePath, const std::string& header) {
    return writePage(filePath, 0, header);
}

std::string FileManager::readHeader(const std::filesystem::path& filePath) {
    return readPage(filePath, 0);
}
