#include "ParserManager.h"

#include "Core.h"
#include "Parser.h"
#include "log/LogWriter.h"

ParserManager::ParserManager(Core *core)
    : core(core), parser(new Parser(core))
{
    LogWriter::info("parser", "ParserManager", "ParserManager", "Parser manager initialized.");
}

ParserManager::~ParserManager()
{
    LogWriter::info("parser", "ParserManager", "~ParserManager", "Parser manager is being released.");
    delete parser;
    parser = nullptr;
}

Parser *ParserManager::getParser() const
{
    return parser;
}
