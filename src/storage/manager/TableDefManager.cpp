#include "TableDefManager.h"

#include <cctype>
#include "log/LogWriter.h"

TableDefManager::TableDefManager(Core *core)
    : core(core)
{
    LogWriter::debug("storage", "TableDefManager", "TableDefManager", "Table definition manager initialized.");
}

bool TableDefManager::validateColumn(const std::string& name, storage::DataType type, std::uint16_t varcharLen)
{
    if (name.empty()) return false;
    for (char ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
    }
    if (type == storage::DataType::VARCHAR && varcharLen == 0) return false;
    return true;
}

bool TableDefManager::validateRename(const std::string& oldName, const std::string& newName)
{
    if (newName.empty()) return false;
    if (oldName == newName) return true;
    for (char ch : newName) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
    }
    return true;
}
