#include "FileManager.h"

#include "log/LogWriter.h"

// 当前仅预留文件管理器实现入口。

FileManager::FileManager(Core *core)
    : core(core)
{
    LogWriter::debug("storage", "FileManager", "FileManager", "File manager initialized.");
}
