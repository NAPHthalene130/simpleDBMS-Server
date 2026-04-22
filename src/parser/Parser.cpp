#include "Parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "models/parser/ParserException.h"

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
 * @brief 支持的比较运算符集合
 * @details 用于 WHERE 叶子谓词中比较表达式的运算符合法性校验。
 * @author YuzhSong
 */
const std::unordered_set<std::string> COMPARISON_OPERATORS = {
    "=",
    "<",
    ">",
    "<=",
    ">=",
    "<>",
    "!="
};

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

/**
 * @brief 判断 token 是否可作为右操作数
 * @author YuzhSong
 * @param token 待判定 token
 * @return 可作为右操作数返回 true，否则返回 false
 */
bool isRightOperandToken(const Token &token)
{
    return token.getType() == SqlTokenType::Identifier ||
           token.getType() == SqlTokenType::Number ||
           token.getType() == SqlTokenType::String ||
           token.getType() == SqlTokenType::Keyword;
}
} // namespace

/**
 * @brief 构造函数
 * @author YuzhSong
 * @param core 服务端核心对象指针
 */
Parser::Parser(Core *core)
    : core(core)
{
}

/**
 * @brief 语法分析统一入口
 * @author YuzhSong
 * @param tokens 词法分析输出 token 序列
 * @return 解析结果对象
 */
ParseResult Parser::parse(const std::vector<Token> &tokens) const
{
    try {
        TokenStream tokenStream(core, tokens);
        const std::shared_ptr<SQLStatement> statement = parseStatement(tokenStream);
        expectStatementEnd(tokenStream);
        return ParseResult::makeSuccess(statement);
    } catch (const ParserException &parserException) {
        return ParseResult::makeFailure(parserException.what(), parserException.getTokenIndex());
    } catch (const std::exception &exception) {
        return ParseResult::makeFailure(exception.what(), 0);
    }
}

/**
 * @brief 解析 SQL 语句根入口
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return SQL 语句 AST 根节点
 */
std::shared_ptr<SQLStatement> Parser::parseStatement(TokenStream &tokenStream) const
{
    if (tokenStream.isAtEnd()) {
        throw ParserException("Empty token stream cannot be parsed.", tokenStream.position());
    }

    if (tokenStream.match(SqlTokenType::Keyword, "CREATE")) {
        return parseCreateStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "INSERT")) {
        return parseInsertStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "SELECT")) {
        return parseSelectStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "USE")) {
        return parseUseStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "SHOW")) {
        return parseShowStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DROP")) {
        return parseDropStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DELETE")) {
        return parseDeleteStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "UPDATE")) {
        return parseUpdateStatement(tokenStream);
    }

    throw ParserException("Unsupported statement type.", tokenStream.position());
}

/**
 * @brief 解析 CREATE 分支
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return CREATE 语句节点
 */
std::shared_ptr<SQLStatement> Parser::parseCreateStatement(TokenStream &tokenStream) const
{
    if (tokenStream.match(SqlTokenType::Keyword, "DATABASE")) {
        return parseCreateDatabaseStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "TABLE")) {
        return parseCreateTableStatement(tokenStream);
    }

    throw ParserException("CREATE statement requires DATABASE or TABLE keyword.", tokenStream.position());
}

/**
 * @brief 解析 CREATE DATABASE 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return CreateDbStmt 节点
 */
std::shared_ptr<CreateDbStmt> Parser::parseCreateDatabaseStatement(TokenStream &tokenStream) const
{
    const Token &databaseNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "CREATE DATABASE statement requires a database identifier.");

    const std::shared_ptr<CreateDbStmt> statement = std::make_shared<CreateDbStmt>();
    statement->setDbName(databaseNameToken.getValue());
    return statement;
}

/**
 * @brief 解析 CREATE TABLE 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return CreateTableStmt 节点
 */
std::shared_ptr<CreateTableStmt> Parser::parseCreateTableStatement(TokenStream &tokenStream) const
{
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "CREATE TABLE statement requires a table identifier.");
    tokenStream.expect(
        SqlTokenType::Symbol,
        "(",
        "CREATE TABLE statement requires '(' before field definitions.");

    std::vector<FieldBlock> fields;
    fields.push_back(parseFieldDefinition(tokenStream, 0));
    while (tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
        fields.push_back(parseFieldDefinition(tokenStream, static_cast<std::int32_t>(fields.size())));
    }

    tokenStream.expect(
        SqlTokenType::Symbol,
        ")",
        "CREATE TABLE statement requires ')' after field definitions.");

    const std::shared_ptr<CreateTableStmt> statement = std::make_shared<CreateTableStmt>();
    statement->setTableName(toFixedNameArray(tableNameToken.getValue()));
    statement->setFields(fields);
    return statement;
}

/**
 * @brief 解析 INSERT 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return InsertStmt 节点
 */
std::shared_ptr<InsertStmt> Parser::parseInsertStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(SqlTokenType::Keyword, "INTO", "INSERT statement requires INTO keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "INSERT INTO statement requires a table identifier.");

    tokenStream.expect(
        SqlTokenType::Symbol,
        "(",
        "INSERT INTO statement requires '(' before column list.");
    const std::vector<std::string> columnNames = parseIdentifierList(tokenStream);
    tokenStream.expect(
        SqlTokenType::Symbol,
        ")",
        "INSERT INTO statement requires ')' after column list.");

    tokenStream.expect(SqlTokenType::Keyword, "VALUES", "INSERT statement requires VALUES keyword.");
    tokenStream.expect(
        SqlTokenType::Symbol,
        "(",
        "INSERT statement requires '(' before value list.");
    const std::vector<std::string> values = parseValueList(tokenStream);
    tokenStream.expect(
        SqlTokenType::Symbol,
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

/**
 * @brief 解析 SELECT 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return SelectStmt 节点
 */
std::shared_ptr<SelectStmt> Parser::parseSelectStatement(TokenStream &tokenStream) const
{
    bool selectAllFields = false;
    std::vector<std::string> targetFields;
    std::shared_ptr<ConditionNode> whereCondition = nullptr;

    if (tokenStream.consumeOptional(SqlTokenType::Symbol, "*") ||
        tokenStream.consumeOptional(SqlTokenType::Operator, "*")) {
        selectAllFields = true;
    } else {
        targetFields = parseIdentifierList(tokenStream);
    }

    tokenStream.expect(SqlTokenType::Keyword, "FROM", "SELECT statement requires FROM keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "SELECT statement requires a table identifier after FROM.");

    if (tokenStream.match(SqlTokenType::Keyword, "WHERE")) {
        const Token &currentToken = tokenStream.peek();
        if (currentToken.getType() == SqlTokenType::EndOfFile ||
            (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
            throw ParserException("WHERE clause requires a condition expression.", tokenStream.position());
        }

        whereCondition = parseConditionOr(tokenStream);
    }

    const std::shared_ptr<SelectStmt> statement = std::make_shared<SelectStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setSelectAllFields(selectAllFields);
    statement->setTargetFields(targetFields);
    statement->setWhereCondition(whereCondition);
    return statement;
}

/**
 * @brief 解析 USE 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return UseStmt 节点
 */
std::shared_ptr<UseStmt> Parser::parseUseStatement(TokenStream &tokenStream) const
{
    const Token &databaseNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "USE statement requires a database identifier.");

    const std::shared_ptr<UseStmt> statement = std::make_shared<UseStmt>();
    statement->setDbName(databaseNameToken.getValue());
    return statement;
}

/**
 * @brief 解析 SHOW 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return ShowStmt 节点
 */
std::shared_ptr<ShowStmt> Parser::parseShowStatement(TokenStream &tokenStream) const
{
    const std::shared_ptr<ShowStmt> statement = std::make_shared<ShowStmt>();
    if (tokenStream.match(SqlTokenType::Keyword, "DATABASES")) {
        statement->setTargetType(ShowTargetType::Databases);
        return statement;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "TABLES")) {
        statement->setTargetType(ShowTargetType::Tables);
        return statement;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DATABASE")) {
        const Token &databaseNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "SHOW DATABASE statement requires a database identifier.");
        statement->setTargetType(ShowTargetType::Database);
        statement->setTargetName(databaseNameToken.getValue());
        return statement;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "TABLE")) {
        const Token &tableNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "SHOW TABLE statement requires a table identifier.");
        statement->setTargetType(ShowTargetType::Table);
        statement->setTargetName(tableNameToken.getValue());
        return statement;
    }

    throw ParserException(
        "SHOW statement requires DATABASES, TABLES, DATABASE <name>, or TABLE <name>.",
        tokenStream.position());
}

/**
 * @brief 解析 DROP 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return DropStmt 节点
 */
std::shared_ptr<DropStmt> Parser::parseDropStatement(TokenStream &tokenStream) const
{
    const std::shared_ptr<DropStmt> statement = std::make_shared<DropStmt>();
    if (tokenStream.match(SqlTokenType::Keyword, "DATABASE")) {
        const Token &databaseNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "DROP DATABASE statement requires a database identifier.");
        statement->setTargetType(DropTargetType::Database);
        statement->setTargetName(databaseNameToken.getValue());
        return statement;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "TABLE")) {
        const Token &tableNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "DROP TABLE statement requires a table identifier.");
        statement->setTargetType(DropTargetType::Table);
        statement->setTargetName(tableNameToken.getValue());
        return statement;
    }

    throw ParserException("DROP statement requires DATABASE or TABLE keyword.", tokenStream.position());
}

/**
 * @brief 解析 DELETE 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return DeleteStmt 节点
 */
std::shared_ptr<DeleteStmt> Parser::parseDeleteStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(SqlTokenType::Keyword, "FROM", "DELETE statement requires FROM keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "DELETE statement requires a table identifier after FROM.");

    std::shared_ptr<ConditionNode> whereCondition = nullptr;
    if (tokenStream.match(SqlTokenType::Keyword, "WHERE")) {
        const Token &currentToken = tokenStream.peek();
        if (currentToken.getType() == SqlTokenType::EndOfFile ||
            (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
            throw ParserException("WHERE clause requires a condition expression.", tokenStream.position());
        }

        whereCondition = parseConditionOr(tokenStream);
    }

    const std::shared_ptr<DeleteStmt> statement = std::make_shared<DeleteStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setWhereCondition(whereCondition);
    return statement;
}

/**
 * @brief 解析 UPDATE 语句
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return UpdateStmt 节点
 */
std::shared_ptr<UpdateStmt> Parser::parseUpdateStatement(TokenStream &tokenStream) const
{
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "UPDATE statement requires a table identifier.");
    tokenStream.expect(SqlTokenType::Keyword, "SET", "UPDATE statement requires SET keyword.");

    std::vector<std::string> columnNames;
    std::vector<std::string> values;
    parseUpdateAssignmentList(tokenStream, columnNames, values);

    std::shared_ptr<ConditionNode> whereCondition = nullptr;
    if (tokenStream.match(SqlTokenType::Keyword, "WHERE")) {
        const Token &currentToken = tokenStream.peek();
        if (currentToken.getType() == SqlTokenType::EndOfFile ||
            (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
            throw ParserException("WHERE clause requires a condition expression.", tokenStream.position());
        }

        whereCondition = parseConditionOr(tokenStream);
    }

    const std::shared_ptr<UpdateStmt> statement = std::make_shared<UpdateStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setColumnNames(columnNames);
    statement->setValues(values);
    statement->setWhereCondition(whereCondition);
    return statement;
}

std::shared_ptr<UseDbStmt> Parser::parseUseStatement(TokenStream &tokenStream) const
{
    const Token &databaseNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "USE statement requires a database identifier.");

    const std::shared_ptr<UseDbStmt> statement = std::make_shared<UseDbStmt>();
    statement->setDbName(databaseNameToken.getValue());
    return statement;
}

/**
 * @brief 解析 UPDATE SET 赋值列表
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @param columnNames 输出字段名列表
 * @param values 输出字段值列表
 */
void Parser::parseUpdateAssignmentList(TokenStream &tokenStream,
                                       std::vector<std::string> &columnNames,
                                       std::vector<std::string> &values) const
{
    do {
        const Token &columnNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "UPDATE SET clause requires a column identifier.");
        tokenStream.expect(
            SqlTokenType::Operator,
            "=",
            "UPDATE SET clause requires '=' between column and value.");

        const Token &valueToken = tokenStream.peek();
        if (valueToken.getType() != SqlTokenType::Number &&
            valueToken.getType() != SqlTokenType::String &&
            valueToken.getType() != SqlTokenType::Identifier &&
            valueToken.getType() != SqlTokenType::Keyword) {
            throw ParserException("UPDATE SET clause contains invalid value token.", tokenStream.position());
        }
        tokenStream.advance();

        columnNames.push_back(columnNameToken.getValue());
        values.push_back(valueToken.getValue());
    } while (tokenStream.consumeOptional(SqlTokenType::Symbol, ","));
}

/**
 * @brief 解析 OR 层级条件表达式
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return OR 层级条件树根节点
 */
std::shared_ptr<ConditionNode> Parser::parseConditionOr(TokenStream &tokenStream) const
{
    std::shared_ptr<ConditionNode> left = parseConditionAnd(tokenStream);

    while (tokenStream.match(SqlTokenType::Keyword, "OR")) {
        const std::shared_ptr<ConditionNode> right = parseConditionAnd(tokenStream);
        const std::shared_ptr<ConditionNode> parent = std::make_shared<ConditionNode>();
        parent->setOperator("OR");
        parent->setLeftNode(left);
        parent->setRightNode(right);
        left = parent;
    }

    return left;
}

/**
 * @brief 解析 AND 层级条件表达式
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return AND 层级条件树根节点
 */
std::shared_ptr<ConditionNode> Parser::parseConditionAnd(TokenStream &tokenStream) const
{
    std::shared_ptr<ConditionNode> left = parsePredicate(tokenStream);

    while (tokenStream.match(SqlTokenType::Keyword, "AND")) {
        const std::shared_ptr<ConditionNode> right = parsePredicate(tokenStream);
        const std::shared_ptr<ConditionNode> parent = std::make_shared<ConditionNode>();
        parent->setOperator("AND");
        parent->setLeftNode(left);
        parent->setRightNode(right);
        left = parent;
    }

    return left;
}

/**
 * @brief 解析基础谓词
 * @details 支持括号表达式和比较表达式，括号优先级最高。
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return 基础谓词节点
 */
std::shared_ptr<ConditionNode> Parser::parsePredicate(TokenStream &tokenStream) const
{
    if (tokenStream.consumeOptional(SqlTokenType::Symbol, "(")) {
        const std::shared_ptr<ConditionNode> groupedCondition = parseConditionOr(tokenStream);
        tokenStream.expect(
            SqlTokenType::Symbol,
            ")",
            "Missing right parenthesis in WHERE condition.");
        return groupedCondition;
    }

    const Token &leftToken = tokenStream.peek();
    if (leftToken.getType() != SqlTokenType::Identifier) {
        throw ParserException("Missing left operand in predicate.", tokenStream.position());
    }
    tokenStream.advance();

    const Token &operatorToken = tokenStream.peek();
    if (operatorToken.getType() != SqlTokenType::Operator) {
        throw ParserException("Illegal or missing comparison operator in predicate.", tokenStream.position());
    }
    if (COMPARISON_OPERATORS.find(operatorToken.getValue()) == COMPARISON_OPERATORS.end()) {
        throw ParserException("Illegal comparison operator in predicate.", tokenStream.position());
    }
    tokenStream.advance();

    const Token &rightToken = tokenStream.peek();
    if (!isRightOperandToken(rightToken)) {
        throw ParserException("Missing right operand in predicate.", tokenStream.position());
    }
    tokenStream.advance();

    const std::shared_ptr<ConditionNode> predicateNode = std::make_shared<ConditionNode>();
    predicateNode->setLeftOperand(leftToken.getValue());
    predicateNode->setOperator(operatorToken.getValue());
    predicateNode->setRightOperand(rightToken.getValue());
    return predicateNode;
}

/**
 * @brief 解析字段定义
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @param fieldOrder 字段顺序号
 * @return 构造后的字段块
 */
FieldBlock Parser::parseFieldDefinition(TokenStream &tokenStream, const std::int32_t fieldOrder) const
{
    const Token &fieldNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "Field definition requires a column identifier.");

    FieldBlock fieldBlock;
    fieldBlock.setOrder(fieldOrder);
    fieldBlock.setName(toFixedNameArray(fieldNameToken.getValue()));
    fieldBlock.setIntegrities(0);
    parseFieldType(tokenStream, fieldBlock);
    return fieldBlock;
}

/**
 * @brief 解析字段类型
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @param fieldBlock 待填充字段块
 */
void Parser::parseFieldType(TokenStream &tokenStream, FieldBlock &fieldBlock) const
{
    if (tokenStream.match(SqlTokenType::Keyword, "INT")) {
        fieldBlock.setType(FIELD_TYPE_INT);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "FLOAT")) {
        fieldBlock.setType(FIELD_TYPE_FLOAT);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DOUBLE")) {
        fieldBlock.setType(FIELD_TYPE_DOUBLE);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "CHAR")) {
        tokenStream.expect(SqlTokenType::Symbol, "(", "CHAR type requires '(' before length.");
        const Token &lengthToken = tokenStream.expect(SqlTokenType::Number, "CHAR type length must be a number.");
        tokenStream.expect(SqlTokenType::Symbol, ")", "CHAR type requires ')' after length.");
        fieldBlock.setType(FIELD_TYPE_CHAR);
        fieldBlock.setParam(parsePositiveInt(lengthToken.getValue(), tokenStream.position() - 1));
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "VARCHAR")) {
        tokenStream.expect(SqlTokenType::Symbol, "(", "VARCHAR type requires '(' before length.");
        const Token &lengthToken = tokenStream.expect(SqlTokenType::Number, "VARCHAR type length must be a number.");
        tokenStream.expect(SqlTokenType::Symbol, ")", "VARCHAR type requires ')' after length.");
        fieldBlock.setType(FIELD_TYPE_VARCHAR);
        fieldBlock.setParam(parsePositiveInt(lengthToken.getValue(), tokenStream.position() - 1));
        return;
    }

    throw ParserException("Unsupported field type in CREATE TABLE statement.", tokenStream.position());
}

/**
 * @brief 解析标识符列表
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return 标识符序列
 */
std::vector<std::string> Parser::parseIdentifierList(TokenStream &tokenStream) const
{
    std::vector<std::string> identifiers;
    const Token &firstToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "Identifier list requires at least one identifier.");
    identifiers.push_back(firstToken.getValue());

    while (tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
        const Token &identifierToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "Identifier list contains invalid identifier.");
        identifiers.push_back(identifierToken.getValue());
    }

    return identifiers;
}

/**
 * @brief 解析值列表
 * @author YuzhSong
 * @param tokenStream token 游标流
 * @return 值序列
 */
std::vector<std::string> Parser::parseValueList(TokenStream &tokenStream) const
{
    std::vector<std::string> values;
    const Token &firstValueToken = tokenStream.peek();
    if (firstValueToken.getType() != SqlTokenType::Number &&
        firstValueToken.getType() != SqlTokenType::String &&
        firstValueToken.getType() != SqlTokenType::Identifier) {
        throw ParserException("Value list requires at least one literal or identifier.", tokenStream.position());
    }

    values.push_back(tokenStream.advance().getValue());
    while (tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
        const Token &valueToken = tokenStream.peek();
        if (valueToken.getType() != SqlTokenType::Number &&
            valueToken.getType() != SqlTokenType::String &&
            valueToken.getType() != SqlTokenType::Identifier) {
            throw ParserException("Value list contains invalid value token.", tokenStream.position());
        }

        values.push_back(tokenStream.advance().getValue());
    }

    return values;
}

/**
 * @brief 断言语句结束
 * @author YuzhSong
 * @param tokenStream token 游标流
 */
void Parser::expectStatementEnd(TokenStream &tokenStream) const
{
    tokenStream.consumeOptional(SqlTokenType::Symbol, ";");
    tokenStream.expect(SqlTokenType::EndOfFile, "Statement must end at EndOfFile.");
}
