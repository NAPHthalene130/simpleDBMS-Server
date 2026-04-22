#include "TableDefManager.h"

#include "log/LogWriter.h"

// 当前仅预留表定义管理器实现入口。

TableDefManager::TableDefManager(Core *core)
    : core(core)
{
    LogWriter::debug("storage", "TableDefManager", "TableDefManager", "Table definition manager initialized.");
}
