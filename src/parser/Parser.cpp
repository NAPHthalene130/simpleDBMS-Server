#include "Parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "ParserException.h"

namespace
{
/**
 * @brief 字段类型编码常量
 * @details 与 FieldBlock::type 对应，当前仅用于 Parser 阶段的语法结果表达。
 * @author YuzhSong
 */
constexpr std::int32_t FIELD_TYPE_INT = 1;
constexpr std::int32_t FIELD_TYPE_FLOAT = 2;
constexpr std::int32_t FIELD_TYPE_DOUBLE = 3;
constexpr std::int32_t FIELD_TYPE_CHAR = 4;
constexpr std::int32_t FIELD_TYPE_VARCHAR = 5;

/**
 * @brief 将字符串拷贝到定长字符数组
 * @author YuzhSong
 * @param source 源字符串
 * @return 填充后的 128 字节字符数组，超长部分会被截断，末尾保证 '\0'
 */
std::array<char, 128> toFixedNameArray(const std::string &source)
{
    std::array<char, 128> target{};
    const std::size_t copySize = std::min(source.size(), target.size() - 1);
    std::copy_n(source.data(), copySize, target.begin());
    target[copySize] = '\0';
    return target;
}

/**
 * @brief 将 token 文本解析为正整数
 * @author YuzhSong
 * @param value token 文本
 * @param tokenIndex token 下标，用于异常定位
 * @return 解析得到的正整数
 * @throw ParserException 当数值非法或越界时抛出
 */
std::int32_t parsePositiveInt(const std::string &value, const std::size_t tokenIndex)
{
    try {
        const long long parsed = std::stoll(value);
        if (parsed <= 0 || parsed > static_cast<long long>(std::numeric_limits<std::int32_t>::max())) {
            throw ParserException("Type parameter must be a positive 32-bit integer.", tokenIndex);
        }

        return static_cast<std::int32_t>(parsed);
    } catch (const std::invalid_argument &) {
        throw ParserException("Type parameter must be a valid integer.", tokenIndex);
    } catch (const std::out_of_range &) {
        throw ParserException("Type parameter is out of range.", tokenIndex);
    }
}
} // namespace

ParseResult Parser::parse(const std::vector<Token> &tokens) const
{
    try {
        TokenStream tokenStream(tokens);
        const std::shared_ptr<SQLStatement> statement = parseStatement(tokenStream);
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

    throw ParserException("Unsupported statement type.", tokenStream.position());
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
    const Token &tableNameToken = tokenStream.expect(
        TokenType::Identifier,
        "CREATE TABLE statement requires a table identifier.");
    tokenStream.expect(
        TokenType::Symbol,
        "(",
        "CREATE TABLE statement requires '(' before field definitions.");

    std::vector<FieldBlock> fields;
    fields.push_back(parseFieldDefinition(tokenStream, 0));
    while (tokenStream.consumeOptional(TokenType::Symbol, ",")) {
        fields.push_back(parseFieldDefinition(tokenStream, static_cast<std::int32_t>(fields.size())));
    }

    tokenStream.expect(
        TokenType::Symbol,
        ")",
        "CREATE TABLE statement requires ')' after field definitions.");

    const std::shared_ptr<CreateTableStmt> statement = std::make_shared<CreateTableStmt>();
    statement->setTableName(toFixedNameArray(tableNameToken.getValue()));
    statement->setFields(fields);
    return statement;
}

std::shared_ptr<InsertStmt> Parser::parseInsertStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(TokenType::Keyword, "INTO", "INSERT statement requires INTO keyword.");
    const Token &tableNameToken = tokenStream.expect(
        TokenType::Identifier,
        "INSERT INTO statement requires a table identifier.");

    tokenStream.expect(
        TokenType::Symbol,
        "(",
        "INSERT INTO statement requires '(' before column list.");
    const std::vector<std::string> columnNames = parseIdentifierList(tokenStream);
    tokenStream.expect(
        TokenType::Symbol,
        ")",
        "INSERT INTO statement requires ')' after column list.");

    tokenStream.expect(TokenType::Keyword, "VALUES", "INSERT statement requires VALUES keyword.");
    tokenStream.expect(
        TokenType::Symbol,
        "(",
        "INSERT statement requires '(' before value list.");
    const std::vector<std::string> values = parseValueList(tokenStream);
    tokenStream.expect(
        TokenType::Symbol,
        ")",
        "INSERT statement requires ')' after value list.");

    if (columnNames.size() != values.size()) {
        throw ParserException("INSERT column count does not match value count.", tokenStream.position());
    }

    const std::shared_ptr<InsertStmt> statement = std::make_shared<InsertStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setColumnNames(columnNames);
    statement->setValues(values);
    return statement;
}

std::shared_ptr<SelectStmt> Parser::parseSelectStatement(TokenStream &tokenStream) const
{
    bool selectAllFields = false;
    std::vector<std::string> targetFields;

    if (tokenStream.consumeOptional(TokenType::Symbol, "*") ||
        tokenStream.consumeOptional(TokenType::Operator, "*")) {
        selectAllFields = true;
    } else {
        targetFields = parseIdentifierList(tokenStream);
    }

    tokenStream.expect(TokenType::Keyword, "FROM", "SELECT statement requires FROM keyword.");
    const Token &tableNameToken = tokenStream.expect(
        TokenType::Identifier,
        "SELECT statement requires a table identifier after FROM.");

    if (tokenStream.match(TokenType::Keyword, "WHERE")) {
        throw ParserException("WHERE clause is not supported in phase 1 parser.", tokenStream.position() - 1);
    }

    const std::shared_ptr<SelectStmt> statement = std::make_shared<SelectStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setSelectAllFields(selectAllFields);
    statement->setTargetFields(targetFields);
    statement->setWhereCondition(nullptr);
    return statement;
}

FieldBlock Parser::parseFieldDefinition(TokenStream &tokenStream, const std::int32_t fieldOrder) const
{
    const Token &fieldNameToken = tokenStream.expect(
        TokenType::Identifier,
        "Field definition requires a column identifier.");

    FieldBlock fieldBlock;
    fieldBlock.setOrder(fieldOrder);
    fieldBlock.setName(toFixedNameArray(fieldNameToken.getValue()));
    fieldBlock.setIntegrities(0);
    parseFieldType(tokenStream, fieldBlock);
    return fieldBlock;
}

void Parser::parseFieldType(TokenStream &tokenStream, FieldBlock &fieldBlock) const
{
    if (tokenStream.match(TokenType::Keyword, "INT")) {
        fieldBlock.setType(FIELD_TYPE_INT);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(TokenType::Keyword, "FLOAT")) {
        fieldBlock.setType(FIELD_TYPE_FLOAT);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(TokenType::Keyword, "DOUBLE")) {
        fieldBlock.setType(FIELD_TYPE_DOUBLE);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(TokenType::Keyword, "CHAR")) {
        tokenStream.expect(TokenType::Symbol, "(", "CHAR type requires '(' before length.");
        const Token &lengthToken = tokenStream.expect(TokenType::Number, "CHAR type length must be a number.");
        tokenStream.expect(TokenType::Symbol, ")", "CHAR type requires ')' after length.");
        fieldBlock.setType(FIELD_TYPE_CHAR);
        fieldBlock.setParam(parsePositiveInt(lengthToken.getValue(), tokenStream.position() - 1));
        return;
    }

    if (tokenStream.match(TokenType::Keyword, "VARCHAR")) {
        tokenStream.expect(TokenType::Symbol, "(", "VARCHAR type requires '(' before length.");
        const Token &lengthToken = tokenStream.expect(TokenType::Number, "VARCHAR type length must be a number.");
        tokenStream.expect(TokenType::Symbol, ")", "VARCHAR type requires ')' after length.");
        fieldBlock.setType(FIELD_TYPE_VARCHAR);
        fieldBlock.setParam(parsePositiveInt(lengthToken.getValue(), tokenStream.position() - 1));
        return;
    }

    throw ParserException("Unsupported field type in CREATE TABLE statement.", tokenStream.position());
}

std::vector<std::string> Parser::parseIdentifierList(TokenStream &tokenStream) const
{
    std::vector<std::string> identifiers;
    const Token &firstToken = tokenStream.expect(
        TokenType::Identifier,
        "Identifier list requires at least one identifier.");
    identifiers.push_back(firstToken.getValue());

    while (tokenStream.consumeOptional(TokenType::Symbol, ",")) {
        const Token &identifierToken = tokenStream.expect(
            TokenType::Identifier,
            "Identifier list contains invalid identifier.");
        identifiers.push_back(identifierToken.getValue());
    }

    return identifiers;
}

std::vector<std::string> Parser::parseValueList(TokenStream &tokenStream) const
{
    std::vector<std::string> values;
    const Token &firstValueToken = tokenStream.peek();
    if (firstValueToken.getType() != TokenType::Number &&
        firstValueToken.getType() != TokenType::String &&
        firstValueToken.getType() != TokenType::Identifier) {
        throw ParserException("Value list requires at least one literal or identifier.", tokenStream.position());
    }

    values.push_back(tokenStream.advance().getValue());
    while (tokenStream.consumeOptional(TokenType::Symbol, ",")) {
        const Token &valueToken = tokenStream.peek();
        if (valueToken.getType() != TokenType::Number &&
            valueToken.getType() != TokenType::String &&
            valueToken.getType() != TokenType::Identifier) {
            throw ParserException("Value list contains invalid value token.", tokenStream.position());
        }

        values.push_back(tokenStream.advance().getValue());
    }

    return values;
}

void Parser::expectStatementEnd(TokenStream &tokenStream) const
{
    tokenStream.consumeOptional(TokenType::Symbol, ";");
    tokenStream.expect(TokenType::EndOfFile, "Statement must end at EndOfFile.");
}
