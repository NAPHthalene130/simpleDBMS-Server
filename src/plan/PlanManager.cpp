#include "PlanManager.h"

#include "Core.h"
#include "Planner.h"
#include "log/LogWriter.h"

PlanManager::PlanManager(Core *core)
    : core(core)
    , planner(new Planner(core))
{
    LogWriter::info("plan", "PlanManager", "PlanManager", "Plan manager initialized.");
}

PlanManager::~PlanManager()
{
    LogWriter::info("plan", "PlanManager", "~PlanManager", "Plan manager is being destroyed.");
    delete planner;
    planner = nullptr;
}

Planner *PlanManager::getPlanner() const
{
    return planner;
}
