#include "StatementExecutor.h"

#include "log/LogWriter.h"

StatementExecutor::StatementExecutor(Core *core)
    : core(core)
{
    LogWriter::debug("executor", "StatementExecutor", "StatementExecutor", "Statement executor base initialized.");
}
