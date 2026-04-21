#include "Parser.h"

#include "ParserException.h"

ParseResult Parser::parse(const std::vector<Token> &tokens) const
{
    try {
        TokenStream tokenStream(tokens);
        std::shared_ptr<SQLStatement> statement = parseStatement(tokenStream);
        expectStatementEnd(tokenStream);
        return ParseResult::makeSuccess(statement);
    } catch (const ParserException &parserException) {
        return ParseResult::makeFailure(parserException.what(), parserException.getTokenIndex());
    } catch (const std::exception &exception) {
        return ParseResult::makeFailure(exception.what(), 0);
    }
}

std::shared_ptr<SQLStatement> Parser::parseStatement(TokenStream &tokenStream) const
{
    if (tokenStream.isAtEnd()) {
        throw ParserException("Empty token stream cannot be parsed.", tokenStream.position());
    }

    if (tokenStream.match(TokenType::Keyword, "CREATE")) {
        return parseCreateStatement(tokenStream);
    }

    if (tokenStream.match(TokenType::Keyword, "INSERT")) {
        return parseInsertStatement(tokenStream);
    }

    if (tokenStream.match(TokenType::Keyword, "SELECT")) {
        return parseSelectStatement(tokenStream);
    }

    throw ParserException("Unsupported statement type in phase 1 parser skeleton.", tokenStream.position());
}

std::shared_ptr<SQLStatement> Parser::parseCreateStatement(TokenStream &tokenStream) const
{
    if (tokenStream.match(TokenType::Keyword, "DATABASE")) {
        return parseCreateDatabaseStatement(tokenStream);
    }

    if (tokenStream.match(TokenType::Keyword, "TABLE")) {
        return parseCreateTableStatement(tokenStream);
    }

    throw ParserException("CREATE statement requires DATABASE or TABLE keyword.", tokenStream.position());
}

std::shared_ptr<CreateDbStmt> Parser::parseCreateDatabaseStatement(TokenStream &tokenStream) const
{
    const Token &databaseNameToken = tokenStream.expect(
        TokenType::Identifier,
        "CREATE DATABASE statement requires a database identifier.");

    const std::shared_ptr<CreateDbStmt> statement = std::make_shared<CreateDbStmt>();
    statement->setDbName(databaseNameToken.getValue());
    return statement;
}

std::shared_ptr<CreateTableStmt> Parser::parseCreateTableStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(TokenType::Identifier, "CREATE TABLE statement requires a table identifier.");
    throw ParserException("CREATE TABLE parser body is not implemented in phase 1.", tokenStream.position());
}

std::shared_ptr<InsertStmt> Parser::parseInsertStatement(TokenStream &tokenStream) const
{
    (void) tokenStream;
    throw ParserException("INSERT parser body is not implemented in phase 1.", tokenStream.position());
}

std::shared_ptr<SelectStmt> Parser::parseSelectStatement(TokenStream &tokenStream) const
{
    (void) tokenStream;
    throw ParserException("SELECT parser body is not implemented in phase 1.", tokenStream.position());
}

void Parser::expectStatementEnd(TokenStream &tokenStream) const
{
    tokenStream.consumeOptional(TokenType::Symbol, ";");
    if (!tokenStream.isAtEnd()) {
        throw ParserException("Unexpected trailing tokens after valid statement.", tokenStream.position());
    }
}
