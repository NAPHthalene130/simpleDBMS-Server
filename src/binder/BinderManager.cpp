#include "BinderManager.h"

#include "Binder.h"
#include "Core.h"
#include "log/LogWriter.h"

BinderManager::BinderManager(Core *core)
    : core(core)
    , binder(new Binder(core))
{
    LogWriter::info("binder", "BinderManager", "BinderManager", "Binder manager initialized.");
}

BinderManager::~BinderManager()
{
    LogWriter::info("binder", "BinderManager", "~BinderManager", "Binder manager is being destroyed.");
    delete binder;
    binder = nullptr;
}

Binder *BinderManager::getBinder() const
{
    return binder;
}
