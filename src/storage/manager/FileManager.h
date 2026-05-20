#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class Core;

/**
 * @class FileManager
 * @brief 存储文件管理器
 * @details 统一管理底层文件句柄与页级 I/O 操作。
 * @author NAPH130
 */
class FileManager
{
public:
    static constexpr std::uint32_t kPageSize = 4096;

    explicit FileManager(Core *core);

    /**
     * @brief 读取指定文件的第 pageId 页
     * @param filePath 文件路径
     * @param pageId 页号（1-based，页偏移 = pageId * kPageSize）
     * @return 页内容字符串（最大 kPageSize 字节），文件不存在或页超出则返回空
     */
    static std::string readPage(const std::filesystem::path& filePath, std::uint32_t pageId);

    /**
     * @brief 写入指定文件的第 pageId 页（页内容超出 kPageSize 时截断）
     * @param filePath 文件路径
     * @param pageId 页号
     * @param content 页内容
     * @return 是否成功
     */
    static bool writePage(const std::filesystem::path& filePath, std::uint32_t pageId, const std::string& content);

    /**
     * @brief 写入文件头（第 0 页）
     */
    static bool writeHeader(const std::filesystem::path& filePath, const std::string& header);

    /**
     * @brief 读取文件头
     */
    static std::string readHeader(const std::filesystem::path& filePath);

private:
    Core *core;
};
