#include "ParserManager.h"

#include "Core.h"
#include "Parser.h"

ParserManager::ParserManager(Core *core)
    : core(core), parser(new Parser(core))
{
}

ParserManager::~ParserManager()
{
    delete parser;
    parser = nullptr;
}

Parser *ParserManager::getParser() const
{
    return parser;
}
