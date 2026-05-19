#include "Parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "log/LogWriter.h"
#include "models/parser/AlterTableStmt.h"
#include "models/parser/DclStmt.h"
#include "models/parser/ParserException.h"
#include "models/parser/UnionStmt.h"

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
constexpr std::int32_t FIELD_TYPE_DATE = 6;
constexpr std::int32_t FIELD_TYPE_DECIMAL = 7;
constexpr std::int32_t FIELD_TYPE_TEXT = 8;

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
    "!=",
    "LIKE"
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

std::string joinValues(const std::vector<std::string> &values)
{
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) result += ",";
        result += values[i];
    }
    return result;
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
    LogWriter::debug("parser",
                     "Parser",
                     "parse",
                     std::string("Starting parse with token count=") + std::to_string(tokens.size()));
    try {
        TokenStream tokenStream(core, tokens);
        std::shared_ptr<SQLStatement> statement = parseStatement(tokenStream);

        // 检查是否为 UNION 查询
        // 作者：NAPH130
        if (tokenStream.match(SqlTokenType::Keyword, "UNION")) {
            bool unionAll = tokenStream.consumeOptional(SqlTokenType::Keyword, "ALL");
            // 第二个 SELECT 需重新以 SELECT 关键字开头
            // 作者：NAPH130
            tokenStream.expect(SqlTokenType::Keyword, "SELECT",
                               "UNION must be followed by SELECT statement.");
            // 退一格后重新解析 SELECT
            // 作者：NAPH130
            // 由于已消费 SELECT，需将当前游标回退... 
            // 简单处理：expect 后构造新的 TokenStream 不可回退，
            // 换种方式：先 peek 确认是 SELECT，再完整走 parseStatement
            // 当前简单实现：直接走 Select only
            auto rightStmt = parseSelectStatement(tokenStream);
            auto unionStmt = std::make_shared<UnionStmt>(unionAll);
            unionStmt->setLeftStmt(statement);
            unionStmt->setRightStmt(rightStmt);
            statement = unionStmt;
        }

        expectStatementEnd(tokenStream);
        LogWriter::info("parser",
                        "Parser",
                        "parse",
                        std::string("Parse succeeded, statement type=")
                            + std::to_string(static_cast<int>(statement->getStmtType())));
        return ParseResult::makeSuccess(statement);
    } catch (const ParserException &parserException) {
        LogWriter::warning("parser",
                           "Parser",
                           "parse",
                           std::string("Parse failed with parser exception: ") + parserException.what());
        return ParseResult::makeFailure(parserException.what(), parserException.getTokenIndex());
    } catch (const std::exception &exception) {
        LogWriter::error("parser",
                         "Parser",
                         "parse",
                         std::string("Parse failed with std::exception: ") + exception.what());
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
        LogWriter::warning("parser", "Parser", "parseStatement", "Token stream is empty.");
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

    if (tokenStream.match(SqlTokenType::Keyword, "TRUNCATE")) {
        return parseTruncateStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "ALTER")) {
        return parseAlterStatement(tokenStream);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "GRANT")) {
        return parseDclStatement(tokenStream, DclOperationType::Grant);
    }

    if (tokenStream.match(SqlTokenType::Keyword, "REVOKE")) {
        return parseDclStatement(tokenStream, DclOperationType::Revoke);
    }

    LogWriter::warning("parser", "Parser", "parseStatement", "Encountered unsupported statement type.");
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

    // 解析列定义（以逗号分隔）
    // 作者：NAPH130
    bool firstItem = true;
    while (true) {
        if (!firstItem) {
            if (!tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
                break;
            }
        }
        firstItem = false;

        // 检查是否为表级约束 FOREIGN KEY (...) REFERENCES ...(...)
        // 作者：NAPH130
        if (tokenStream.match(SqlTokenType::Keyword, "FOREIGN")) {
            tokenStream.expect(SqlTokenType::Keyword, "KEY", "FOREIGN must be followed by KEY.");
            tokenStream.expect(SqlTokenType::Symbol, "(", "FOREIGN KEY requires '('.");
            // 外键列名（目前仅支持单列，解析后存储到约束字段）
            // 作者：NAPH130
            const Token &fkColumnToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "FOREIGN KEY requires a column identifier.");
            tokenStream.expect(SqlTokenType::Symbol, ")", "FOREIGN KEY column requires ')'.");

            tokenStream.expect(SqlTokenType::Keyword, "REFERENCES",
                               "FOREIGN KEY requires REFERENCES keyword.");
            const Token &refTableToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "REFERENCES requires a table identifier.");
            tokenStream.expect(SqlTokenType::Symbol, "(", "REFERENCES requires '('.");
            const Token &refColumnToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "REFERENCES requires a column identifier.");
            tokenStream.expect(SqlTokenType::Symbol, ")", "REFERENCES column requires ')'.");

            // 找到对应列并设置外键标记
            // 作者：NAPH130
            const std::string fkColName = fkColumnToken.getValue();
            const std::string refTable = refTableToken.getValue();
            const std::string refCol = refColumnToken.getValue();
            for (auto &field : fields) {
                const auto endIt = std::find(field.getName().begin(), field.getName().end(), '\0');
                const std::string existingName(field.getName().begin(), endIt);
                if (existingName == fkColName) {
                    field.addIntegrityFlag(FieldBlock::INTEGRITY_FOREIGN_KEY);
                    // 外键引用信息存入 default 字段（格式：table.column）
                    // 作者：NAPH130
                    field.setDefaultValue(refTable + "." + refCol);
                    break;
                }
            }
            continue;
        }

        // 普通列定义
        // 作者：NAPH130
        const std::int32_t fieldOrder = static_cast<std::int32_t>(fields.size());
        fields.push_back(parseFieldDefinition(tokenStream, fieldOrder));
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

    std::vector<std::string> columnNames;
    if (tokenStream.match(SqlTokenType::Symbol, "(")) {
        columnNames = parseIdentifierList(tokenStream);
        tokenStream.expect(
            SqlTokenType::Symbol,
            ")",
            "INSERT INTO statement requires ')' after column list.");
    }

    tokenStream.expect(SqlTokenType::Keyword, "VALUES", "INSERT statement requires VALUES keyword.");

    const std::shared_ptr<InsertStmt> statement = std::make_shared<InsertStmt>();
    statement->setTableName(tableNameToken.getValue());
    statement->setColumnNames(columnNames);

    // 解析多组 VALUES
    // 作者：NAPH130
    bool firstValueGroup = true;
    while (true) {
        if (!firstValueGroup) {
            if (!tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
                break;
            }
        }
        firstValueGroup = false;

        tokenStream.expect(
            SqlTokenType::Symbol,
            "(",
            "INSERT statement requires '(' before value list.");
        const std::vector<std::string> values = parseValueList(tokenStream);
        tokenStream.expect(
            SqlTokenType::Symbol,
            ")",
            "INSERT statement requires ')' after value list.");

        if (!columnNames.empty() && columnNames.size() != values.size()) {
            throw ParserException("INSERT column count does not match value count.", tokenStream.position());
        }

        if (statement->getMultiValues().empty()) {
            // 第一组值也存到 values 字段兼容旧代码
            // 作者：NAPH130
            statement->setValues(values);
        }
        statement->addValuesRow(values);
    }

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
    bool distinct = false;
    std::vector<std::string> targetFields;
    std::shared_ptr<ConditionNode> whereCondition = nullptr;

    // 解析 DISTINCT 关键字
    // 作者：NAPH130
    if (tokenStream.consumeOptional(SqlTokenType::Keyword, "DISTINCT")) {
        distinct = true;
    }

    if (tokenStream.consumeOptional(SqlTokenType::Symbol, "*") ||
        tokenStream.consumeOptional(SqlTokenType::Operator, "*")) {
        selectAllFields = true;
    } else {
        // 解析目标字段列表，支持普通标识符和聚合函数调用
        // 作者：NAPH130
        targetFields = parseSelectTargetList(tokenStream);
    }

    tokenStream.expect(SqlTokenType::Keyword, "FROM", "SELECT statement requires FROM keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "SELECT statement requires a table identifier after FROM.");

    // 表别名（AS alias 或 直接 alias）
    // 作者：NAPH130
    if (tokenStream.consumeOptional(SqlTokenType::Keyword, "AS")) {
        tokenStream.expect(
            SqlTokenType::Identifier,
            "AS keyword requires an alias identifier.");
    } else if (tokenStream.match(SqlTokenType::Identifier)) {
        // 简单启发式：下一个标识符可能为别名
        // 仅在后续为 JOIN/WHERE/GROUP/HAVING/; 或 EOF 时才作为别名消费
        // 作者：NAPH130
        const auto &nextToken = tokenStream.peek();
        if (nextToken.getType() == SqlTokenType::Identifier) {
            tokenStream.advance();
        }
    }

    const std::shared_ptr<SelectStmt> statement = std::make_shared<SelectStmt>();
    statement->setTableName(tableNameToken.getValue());

    // 解析 JOIN 子句
    // 作者：NAPH130
    auto joinInfos = parseJoinClauses(tokenStream);
    for (auto &joinInfo : joinInfos) {
        statement->addJoinInfo(std::move(joinInfo));
    }

    if (tokenStream.match(SqlTokenType::Keyword, "WHERE")) {
        const Token &currentToken = tokenStream.peek();
        if (currentToken.getType() == SqlTokenType::EndOfFile ||
            (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
            throw ParserException("WHERE clause requires a condition expression.", tokenStream.position());
        }

        whereCondition = parseConditionOr(tokenStream);
    }

    std::vector<std::string> groupByColumns;
    if (tokenStream.match(SqlTokenType::Keyword, "GROUP")) {
        tokenStream.expect(SqlTokenType::Keyword,
                           "BY",
                           "GROUP must be followed by BY keyword.");
        groupByColumns = parseIdentifierList(tokenStream);
    }

    std::shared_ptr<ConditionNode> havingCondition = nullptr;
    if (tokenStream.match(SqlTokenType::Keyword, "HAVING")) {
        const Token &currentToken = tokenStream.peek();
        if (currentToken.getType() == SqlTokenType::EndOfFile ||
            (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
            throw ParserException("HAVING clause requires a condition expression.", tokenStream.position());
        }

        havingCondition = parseConditionOr(tokenStream);
    }

    // ORDER BY 子句
    // 作者：NAPH130
    std::string orderByColumn;
    bool orderByDesc = false;
    if (tokenStream.match(SqlTokenType::Keyword, "ORDER")) {
        tokenStream.expect(SqlTokenType::Keyword, "BY", "ORDER must be followed by BY keyword.");
        std::vector<std::string> orderColumns;
        bool firstCol = true;
        while (true) {
            if (!firstCol) {
                if (!tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
                    break;
                }
            }
            firstCol = false;
            const Token &orderColToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "ORDER BY requires a column identifier.");
            std::string colExpr = orderColToken.getValue();
            if (tokenStream.match(SqlTokenType::Keyword, "DESC")) {
                colExpr += " DESC";
                orderByDesc = true;
            } else if (tokenStream.consumeOptional(SqlTokenType::Keyword, "ASC")) {
                colExpr += " ASC";
            }
            orderColumns.push_back(colExpr);
        }
        if (!orderColumns.empty()) {
            std::string combined;
            for (std::size_t i = 0; i < orderColumns.size(); ++i) {
                if (i > 0) combined += ", ";
                combined += orderColumns[i];
            }
            orderByColumn = combined;
            // 取第一个列的排序方向作为主方向
            if (!orderColumns.empty() && orderColumns.front().find(" DESC") != std::string::npos) {
                orderByDesc = true;
            }
        }
    }

    // LIMIT 子句
    // 作者：NAPH130
    bool hasLimit = false;
    std::size_t limitCount = 0;
    if (tokenStream.match(SqlTokenType::Keyword, "LIMIT")) {
        const Token &limitToken = tokenStream.expect(
            SqlTokenType::Number,
            "LIMIT requires a number.");
        hasLimit = true;
        try {
            limitCount = static_cast<std::size_t>(std::stoull(limitToken.getValue()));
        } catch (...) {
            throw ParserException("LIMIT value must be a valid positive integer.", tokenStream.position());
        }
    }

    statement->setSelectAllFields(selectAllFields);
    statement->setTargetFields(targetFields);
    statement->setWhereCondition(whereCondition);
    statement->setGroupByColumns(groupByColumns);
    statement->setHavingCondition(havingCondition);
    statement->setOrderByColumn(orderByColumn);
    statement->setOrderByDesc(orderByDesc);
    statement->setHasLimit(hasLimit);
    statement->setLimitCount(limitCount);
    return statement;
}

/**
 * @brief 解析 JOIN 子句序列
 * @details 支持 [INNER] JOIN、LEFT [OUTER] JOIN、RIGHT [OUTER] JOIN 带 ON 条件。
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return JOIN 子句信息列表
 */
std::vector<JoinInfo> Parser::parseJoinClauses(TokenStream &tokenStream) const
{
    std::vector<JoinInfo> result;

    while (true) {
        std::string joinType = "INNER";

        if (tokenStream.consumeOptional(SqlTokenType::Keyword, "INNER")) {
            joinType = "INNER";
        } else if (tokenStream.consumeOptional(SqlTokenType::Keyword, "LEFT")) {
            joinType = "LEFT";
            tokenStream.consumeOptional(SqlTokenType::Keyword, "OUTER");
        } else if (tokenStream.consumeOptional(SqlTokenType::Keyword, "RIGHT")) {
            joinType = "RIGHT";
            tokenStream.consumeOptional(SqlTokenType::Keyword, "OUTER");
        } else if (!tokenStream.match(SqlTokenType::Keyword, "JOIN")) {
            break;
        }

        tokenStream.expect(SqlTokenType::Keyword, "JOIN", "JOIN keyword expected.");

        const Token &joinTableToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "JOIN requires a table identifier.");

        // 可选的表别名
        // 作者：NAPH130
        std::string alias;
        if (tokenStream.consumeOptional(SqlTokenType::Keyword, "AS")) {
            const Token &aliasToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "AS keyword requires an alias identifier.");
            alias = aliasToken.getValue();
        }

        // ON 条件
        // 作者：NAPH130
        std::shared_ptr<ConditionNode> onCondition;
        if (tokenStream.match(SqlTokenType::Keyword, "ON")) {
            // match 已消费 ON token，直接解析后续条件表达式
            // 作者：NAPH130
            const Token &currentToken = tokenStream.peek();
            if (currentToken.getType() == SqlTokenType::EndOfFile ||
                (currentToken.getType() == SqlTokenType::Symbol && currentToken.getValue() == ";")) {
                throw ParserException("ON clause requires a condition expression.",
                                      tokenStream.position());
            }
            onCondition = parseConditionOr(tokenStream);
        }

        JoinInfo joinInfo;
        joinInfo.joinType = joinType;
        joinInfo.tableName = joinTableToken.getValue();
        joinInfo.alias = alias;
        joinInfo.onCondition = onCondition;
        result.push_back(std::move(joinInfo));
    }

    return result;
}

/**
 * @brief 解析 USE 语句
 * @details 支持 `USE dbName` 与 `USE DATABASE dbName` 两种写法。
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return USE 语句节点
 */
std::shared_ptr<SQLStatement> Parser::parseUseStatement(TokenStream &tokenStream) const
{
    if (tokenStream.match(SqlTokenType::Keyword, "DATABASE")) {
        const Token &databaseNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "USE DATABASE statement requires a database identifier.");

        const std::shared_ptr<UseDbStmt> statement = std::make_shared<UseDbStmt>();
        statement->setDbName(databaseNameToken.getValue());
        return statement;
    }

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
 * @brief 解析 TRUNCATE TABLE 语句
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return TRUNCATE 语句节点（按 DeleteStmt 处理）
 */
std::shared_ptr<SQLStatement> Parser::parseTruncateStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(SqlTokenType::Keyword, "TABLE", "TRUNCATE requires TABLE keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "TRUNCATE TABLE requires a table identifier.");
    auto stmt = std::make_shared<DeleteStmt>();
    stmt->setTableName(tableNameToken.getValue());
    stmt->setWhereCondition(nullptr);
    stmt->setStmtType(ExecutionStatementType::Delete);
    return stmt;
}

/**
 * @brief 解析 ALTER TABLE 语句
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return ALTER 语句 AST 节点
 * @details 支持 ADD COLUMN / DROP COLUMN / RENAME COLUMN / ALTER COLUMN TYPE
 */
std::shared_ptr<SQLStatement> Parser::parseAlterStatement(TokenStream &tokenStream) const
{
    tokenStream.expect(SqlTokenType::Keyword, "TABLE", "ALTER requires TABLE keyword.");
    const Token &tableNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "ALTER TABLE requires a table identifier.");

    if (tokenStream.match(SqlTokenType::Keyword, "ADD")) {
        tokenStream.consumeOptional(SqlTokenType::Keyword, "COLUMN");
        const Token &colNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "ADD COLUMN requires a column identifier.");

        std::string columnType;
        std::uint16_t varcharLen = 0;
        const Token &typeToken = tokenStream.peek();
        if (typeToken.getType() == SqlTokenType::Keyword) {
            const std::string upperType = typeToken.getValue();
            columnType = upperType;
            tokenStream.advance();
            if (upperType == "VARCHAR" || upperType == "CHAR") {
                tokenStream.expect(SqlTokenType::Symbol, "(", upperType + " requires '(' before length.");
                const Token &lenToken = tokenStream.expect(
                    SqlTokenType::Number, upperType + " length must be a number.");
                varcharLen = static_cast<std::uint16_t>(
                    std::stoul(lenToken.getValue()));
                tokenStream.expect(SqlTokenType::Symbol, ")", upperType + " requires ')' after length.");
            } else if (upperType == "DECIMAL") {
                if (tokenStream.match(SqlTokenType::Symbol, "(")) {
                    tokenStream.expect(SqlTokenType::Number, "DECIMAL type requires precision.");
                    if (tokenStream.match(SqlTokenType::Symbol, ",")) {
                        tokenStream.expect(SqlTokenType::Number,
                                           "DECIMAL type requires scale after ','.");
                    }
                    tokenStream.expect(SqlTokenType::Symbol, ")", "DECIMAL type requires ')'.");
                }
            }
        } else {
            throw ParserException("ADD COLUMN requires a type keyword.", tokenStream.position());
        }

        std::string defaultValue;
        if (tokenStream.match(SqlTokenType::Keyword, "DEFAULT")) {
            const Token &defaultToken = tokenStream.peek();
            if (defaultToken.getType() == SqlTokenType::Number
                || defaultToken.getType() == SqlTokenType::String
                || defaultToken.getType() == SqlTokenType::Identifier
                || defaultToken.getType() == SqlTokenType::Keyword) {
                defaultValue = defaultToken.getValue();
                tokenStream.advance();
            }
        }

        auto stmt = std::make_shared<AlterTableStmt>();
        stmt->setTableName(tableNameToken.getValue());
        stmt->setTargetType(AlterTableTargetType::AddColumn);
        stmt->setColumnName(colNameToken.getValue());
        stmt->setColumnType(columnType);
        stmt->setVarcharLen(varcharLen);
        stmt->setDefaultValue(defaultValue);
        return stmt;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "ALTER")) {
        tokenStream.consumeOptional(SqlTokenType::Keyword, "COLUMN");
        const Token &colNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "ALTER COLUMN requires a column identifier.");

        const Token &typeToken = tokenStream.peek();
        if (typeToken.getType() != SqlTokenType::Keyword) {
            throw ParserException("ALTER COLUMN requires a type keyword.", tokenStream.position());
        }
        std::string newType = typeToken.getValue();
        std::uint16_t varcharLen = 0;
        tokenStream.advance();
        if (newType == "VARCHAR" || newType == "CHAR") {
            tokenStream.expect(SqlTokenType::Symbol, "(", newType + " requires '(' before length.");
            const Token &lenToken = tokenStream.expect(
                SqlTokenType::Number, newType + " length must be a number.");
            varcharLen = static_cast<std::uint16_t>(std::stoul(lenToken.getValue()));
            tokenStream.expect(SqlTokenType::Symbol, ")", newType + " requires ')' after length.");
        }

        auto stmt = std::make_shared<AlterTableStmt>();
        stmt->setTableName(tableNameToken.getValue());
        stmt->setTargetType(AlterTableTargetType::AlterColumnType);
        stmt->setColumnName(colNameToken.getValue());
        stmt->setColumnType(newType);
        stmt->setVarcharLen(varcharLen);
        return stmt;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DROP")) {
        tokenStream.consumeOptional(SqlTokenType::Keyword, "COLUMN");
        const Token &colNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "DROP COLUMN requires a column identifier.");

        auto stmt = std::make_shared<AlterTableStmt>();
        stmt->setTableName(tableNameToken.getValue());
        stmt->setTargetType(AlterTableTargetType::DropColumn);
        stmt->setColumnName(colNameToken.getValue());
        return stmt;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "RENAME")) {
        tokenStream.consumeOptional(SqlTokenType::Keyword, "COLUMN");
        const Token &oldNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "RENAME COLUMN requires old column name.");
        tokenStream.expect(SqlTokenType::Keyword, "TO",
                           "RENAME COLUMN requires TO keyword.");
        const Token &newNameToken = tokenStream.expect(
            SqlTokenType::Identifier,
            "RENAME COLUMN requires new column name.");

        auto stmt = std::make_shared<AlterTableStmt>();
        stmt->setTableName(tableNameToken.getValue());
        stmt->setTargetType(AlterTableTargetType::RenameColumn);
        stmt->setColumnName(oldNameToken.getValue());
        stmt->setNewColumnName(newNameToken.getValue());
        return stmt;
    }

    throw ParserException(
        "ALTER TABLE only supports ADD/DROP/RENAME/ALTER COLUMN.",
        tokenStream.position());
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

    // 解析左操作数：支持标识符、table.column 形式、聚合函数
    // 作者：NAPH130
    std::string leftOperand;
    const Token &leftFirst = tokenStream.peek();

    // 处理 EXISTS / NOT EXISTS 无左操作数形式
    // 作者：NAPH130
    if (leftFirst.getType() == SqlTokenType::Keyword) {
        const std::string upperVal = leftFirst.getValue();
        if (upperVal == "EXISTS") {
            return parseInOrExists(tokenStream, "");
        }
        if (upperVal == "NOT") {
            const Token &second = tokenStream.peek(1);
            if (second.getType() == SqlTokenType::Keyword && second.getValue() == "EXISTS") {
                tokenStream.advance(); // 消费 NOT
                auto node = parseInOrExists(tokenStream, "");
                node->setNegated(true);
                return node;
            }
        }
    }

    // 聚合函数作为左操作数（如 HAVING COUNT(*) > 1）
    // 作者：NAPH130
    bool isAggFuncFirst = false;
    if (leftFirst.getType() == SqlTokenType::Keyword) {
        const std::string upperVal = leftFirst.getValue();
        isAggFuncFirst = (upperVal == "COUNT" || upperVal == "SUM"
                          || upperVal == "AVG" || upperVal == "MIN" || upperVal == "MAX");
    }

    if (isAggFuncFirst) {
        leftOperand = leftFirst.getValue();
        tokenStream.advance();
        tokenStream.expect(SqlTokenType::Symbol, "(", "Aggregate function requires '('.");
        const Token &argToken = tokenStream.peek();
        if ((argToken.getType() == SqlTokenType::Symbol || argToken.getType() == SqlTokenType::Operator)
            && argToken.getValue() == "*") {
            leftOperand += "(*)";
            tokenStream.advance();
        } else if (argToken.getType() == SqlTokenType::Identifier) {
            leftOperand += "(" + argToken.getValue() + ")";
            tokenStream.advance();
        } else {
            throw ParserException("Aggregate function requires argument.", tokenStream.position());
        }
        tokenStream.expect(SqlTokenType::Symbol, ")", "Aggregate function requires ')'.");
    } else if (leftFirst.getType() != SqlTokenType::Identifier) {
        throw ParserException("Missing left operand in predicate.", tokenStream.position());
    } else {
        leftOperand = leftFirst.getValue();
        tokenStream.advance();

        // 处理 table.column 或 alias.column 的 . column 部分
        // 作者：NAPH130
        if (tokenStream.peek().getType() == SqlTokenType::Symbol && tokenStream.peek().getValue() == ".") {
            tokenStream.advance();
            const Token &colToken = tokenStream.peek();
            if (colToken.getType() != SqlTokenType::Identifier) {
                throw ParserException("Expected column name after '.' in predicate.", tokenStream.position());
            }
            leftOperand += "." + colToken.getValue();
            tokenStream.advance();
        }
    }

    // 处理 IS NULL / IS NOT NULL
    // 作者：NAPH130
    if (tokenStream.match(SqlTokenType::Keyword, "IS")) {
        bool negated = tokenStream.consumeOptional(SqlTokenType::Keyword, "NOT");
        tokenStream.expect(SqlTokenType::Keyword, "NULL", "IS requires NULL keyword.");
        const std::shared_ptr<ConditionNode> node = std::make_shared<ConditionNode>();
        node->setLeftOperand(leftOperand);
        node->setOperator(negated ? "IS NOT NULL" : "IS NULL");
        return node;
    }

    // 处理 BETWEEN ... AND ...
    // 作者：NAPH130
    if (tokenStream.match(SqlTokenType::Keyword, "BETWEEN")) {
        const Token &lowToken = tokenStream.peek();
        if (!isRightOperandToken(lowToken)) {
            throw ParserException("BETWEEN requires a lower bound value.", tokenStream.position());
        }
        tokenStream.advance();
        tokenStream.expect(SqlTokenType::Keyword, "AND", "BETWEEN requires AND keyword.");
        const Token &highToken = tokenStream.peek();
        if (!isRightOperandToken(highToken)) {
            throw ParserException("BETWEEN requires an upper bound value.", tokenStream.position());
        }
        tokenStream.advance();
        const std::shared_ptr<ConditionNode> node = std::make_shared<ConditionNode>();
        node->setLeftOperand(leftOperand);
        node->setOperator("BETWEEN");
        node->setRightOperand(lowToken.getValue() + " AND " + highToken.getValue());
        return node;
    }

    // 处理 NOT LIKE / NOT BETWEEN / NOT IN
    // 作者：NAPH130
    bool negated = false;
    if (tokenStream.match(SqlTokenType::Keyword, "NOT")) {
        negated = true;
    }

    // 处理 IN / EXISTS / NOT IN / NOT EXISTS
    // 作者：NAPH130
    const Token &nextToken = tokenStream.peek();
    if (nextToken.getType() == SqlTokenType::Keyword) {
        const std::string upperNext = nextToken.getValue();
        if (upperNext == "IN" || upperNext == "EXISTS") {
            auto node = parseInOrExists(tokenStream, leftOperand);
            if (negated) node->setNegated(true);
            return node;
        }
        // LIKE 操作符
        // 作者：NAPH130
        if (upperNext == "LIKE") {
            tokenStream.advance();
            const Token &patternToken = tokenStream.peek();
            if (patternToken.getType() != SqlTokenType::String
                && patternToken.getType() != SqlTokenType::Identifier
                && patternToken.getType() != SqlTokenType::Number) {
                throw ParserException("LIKE requires a pattern value.", tokenStream.position());
            }
            tokenStream.advance();
            const std::shared_ptr<ConditionNode> node = std::make_shared<ConditionNode>();
            node->setLeftOperand(leftOperand);
            node->setOperator(negated ? "NOT LIKE" : "LIKE");
            node->setRightOperand(patternToken.getValue());
            return node;
        }
        // BETWEEN after NOT
        // 作者：NAPH130
        if (upperNext == "BETWEEN") {
            tokenStream.advance();
            const Token &lowToken = tokenStream.peek();
            if (!isRightOperandToken(lowToken)) {
                throw ParserException("BETWEEN requires a lower bound value.", tokenStream.position());
            }
            tokenStream.advance();
            tokenStream.expect(SqlTokenType::Keyword, "AND", "BETWEEN requires AND keyword.");
            const Token &highToken = tokenStream.peek();
            if (!isRightOperandToken(highToken)) {
                throw ParserException("BETWEEN requires an upper bound value.", tokenStream.position());
            }
            tokenStream.advance();
            const std::shared_ptr<ConditionNode> node = std::make_shared<ConditionNode>();
            node->setLeftOperand(leftOperand);
            node->setOperator("NOT BETWEEN");
            node->setRightOperand(lowToken.getValue() + " AND " + highToken.getValue());
            return node;
        }
    }

    if (negated) {
        // NOT 后跟普通比较运算（如 NOT id = 1）
        if (tokenStream.peek().getType() == SqlTokenType::Operator
            && COMPARISON_OPERATORS.find(tokenStream.peek().getValue()) != COMPARISON_OPERATORS.end()) {
            // 允许 NOT = 等比较（语法上接受，语义上由执行器处理）
        } else {
            throw ParserException("NOT must be followed by IN, EXISTS, LIKE, BETWEEN, or a comparison.",
                                  tokenStream.position());
        }
    }

    const Token &operatorToken = tokenStream.peek();
    if (operatorToken.getType() != SqlTokenType::Operator) {
        throw ParserException("Illegal or missing comparison operator in predicate.", tokenStream.position());
    }
    if (COMPARISON_OPERATORS.find(operatorToken.getValue()) == COMPARISON_OPERATORS.end()) {
        throw ParserException("Illegal comparison operator in predicate.", tokenStream.position());
    }
    tokenStream.advance();

    // 解析右操作数：支持标识符、table.column 形式、数字、字符串、关键字、子查询
    // 作者：NAPH130
    const Token &rightFirst = tokenStream.peek();

    // 检查子查询：(SELECT ...)
    // 作者：NAPH130
    if (rightFirst.getType() == SqlTokenType::Symbol && rightFirst.getValue() == "(") {
        // Peek ahead for SELECT keyword
        // 作者：NAPH130
        if (tokenStream.peek(1).getType() == SqlTokenType::Keyword
            && tokenStream.peek(1).getValue() == "SELECT") {
            tokenStream.advance(); // consume (
            auto subSelect = parseSelectStatement(tokenStream);
            tokenStream.expect(SqlTokenType::Symbol, ")", "Subquery requires closing parenthesis.");

            const std::shared_ptr<ConditionNode> predicateNode = std::make_shared<ConditionNode>();
            predicateNode->setLeftOperand(leftOperand);
            predicateNode->setOperator(operatorToken.getValue());
            predicateNode->setSubquery(subSelect);
            return predicateNode;
        }
    }

    std::string rightOperand;
    if (!isRightOperandToken(rightFirst)) {
        throw ParserException("Missing right operand in predicate.", tokenStream.position());
    }
    rightOperand = rightFirst.getValue();
    tokenStream.advance();

    // 处理右操作数的 table.column 形式
    // 作者：NAPH130
    if (tokenStream.peek().getType() == SqlTokenType::Symbol && tokenStream.peek().getValue() == ".") {
        tokenStream.advance();
        const Token &rightColToken = tokenStream.peek();
        if (rightColToken.getType() != SqlTokenType::Identifier) {
            throw ParserException("Expected column name after '.' in predicate right operand.",
                                  tokenStream.position());
        }
        rightOperand += "." + rightColToken.getValue();
        tokenStream.advance();
    }

    const std::shared_ptr<ConditionNode> predicateNode = std::make_shared<ConditionNode>();
    predicateNode->setLeftOperand(leftOperand);
    predicateNode->setOperator(operatorToken.getValue());
    predicateNode->setRightOperand(rightOperand);
    if (negated) {
        predicateNode->setNegated(true);
    }
    return predicateNode;
}

/**
 * @brief 解析 IN / EXISTS / NOT IN / NOT EXISTS 子查询或值列表
 * @details 支持 col IN (SELECT ...)、col IN (v1, v2, ...)、EXISTS (SELECT ...)、NOT EXISTS (SELECT ...)。
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return 条件树节点
 */
std::shared_ptr<ConditionNode> Parser::parseInOrExists(TokenStream &tokenStream,
                                                         const std::string &leftOperand) const
{
    const std::shared_ptr<ConditionNode> node = std::make_shared<ConditionNode>();
    node->setLeftOperand(leftOperand);

    // NOT 前缀处理（如 NOT EXISTS / NOT IN）
    // 作者：NAPH130
    bool isNot = tokenStream.match(SqlTokenType::Keyword, "NOT");
    if (isNot) {
        node->setNegated(true);
    }

    // 获取实际操作关键字（IN 或 EXISTS）
    // 作者：NAPH130
    const Token &opToken = tokenStream.peek();
    const std::string keyword = opToken.getValue();
    node->setOperator(keyword);

    if (tokenStream.match(SqlTokenType::Keyword, "EXISTS")) {
        tokenStream.expect(SqlTokenType::Symbol, "(", "EXISTS requires '('.");
        tokenStream.expect(SqlTokenType::Keyword, "SELECT", "EXISTS subquery requires SELECT.");
        auto subSelect = parseSelectStatement(tokenStream);
        tokenStream.expect(SqlTokenType::Symbol, ")", "EXISTS subquery requires ')'.");
        node->setSubquery(subSelect);
        return node;
    }

    // IN ( ... )
    // 作者：NAPH130
    tokenStream.expect(SqlTokenType::Keyword, "IN", "Expected IN or EXISTS.");
    tokenStream.expect(SqlTokenType::Symbol, "(", "IN requires '('.");

    // 检查是子查询还是值列表
    // 作者：NAPH130
    if (tokenStream.match(SqlTokenType::Keyword, "SELECT")) {
        auto subSelect = parseSelectStatement(tokenStream);
        tokenStream.expect(SqlTokenType::Symbol, ")", "IN subquery requires ')'.");
        node->setSubquery(subSelect);
    } else {
        // 值列表：IN (v1, v2, ...)
        // 作者：NAPH130
        const auto values = parseValueList(tokenStream);
        node->setRightOperand("(" + joinValues(values) + ")");
        tokenStream.expect(SqlTokenType::Symbol, ")", "IN value list requires ')'.");
    }
    return node;
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
    parseFieldConstraints(tokenStream, fieldBlock);
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
    if (tokenStream.match(SqlTokenType::Keyword, "INT")
        || tokenStream.match(SqlTokenType::Keyword, "INTEGER")) {
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

    if (tokenStream.match(SqlTokenType::Keyword, "DATE")) {
        fieldBlock.setType(FIELD_TYPE_DATE);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "BOOLEAN")
        || tokenStream.match(SqlTokenType::Keyword, "BOOL")) {
        // BOOLEAN/BOOL 暂按 INT 类型处理
        fieldBlock.setType(FIELD_TYPE_INT);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DATETIME")) {
        // DATETIME 暂按 DATE 类型处理
        // 作者：NAPH130
        fieldBlock.setType(FIELD_TYPE_DATE);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "DECIMAL")) {
        // DECIMAL 暂按 FLOAT 类型处理，接收可选精度(precision,scale)但忽略
        // 修复：NAPH130 — 适应 PerformanceTest 中使用 DECIMAL(10,2) 的场景
        if (tokenStream.match(SqlTokenType::Symbol, "(")) {
            tokenStream.expect(SqlTokenType::Number, "DECIMAL type requires precision.");
            if (tokenStream.match(SqlTokenType::Symbol, ",")) {
                tokenStream.expect(SqlTokenType::Number, "DECIMAL type requires scale after ','.");
            }
            tokenStream.expect(SqlTokenType::Symbol, ")", "DECIMAL type requires ')' after precision/scale.");
        }
        fieldBlock.setType(FIELD_TYPE_DECIMAL);
        fieldBlock.setParam(0);
        return;
    }

    if (tokenStream.match(SqlTokenType::Keyword, "TEXT")) {
        fieldBlock.setType(FIELD_TYPE_TEXT);
        fieldBlock.setParam(0);
        return;
    }

    throw ParserException("Unsupported field type in CREATE TABLE statement.", tokenStream.position());
}

/**
 * @brief 解析字段可选约束
 * @author NAPH130
 * @param tokenStream token 游标流
 * @param fieldBlock 待填充字段块
 * @details 在字段类型之后循环解析 NOT NULL、DEFAULT、PRIMARY KEY、UNIQUE 等可选约束，
 *          每个约束按位掩码和默认值字段存储到 FieldBlock 中。
 */
void Parser::parseFieldConstraints(TokenStream &tokenStream, FieldBlock &fieldBlock) const
{
    while (true) {
        if (tokenStream.match(SqlTokenType::Keyword, "NOT")) {
            tokenStream.expect(SqlTokenType::Keyword,
                               "NULL",
                               "NOT constraint requires NULL keyword.");
            fieldBlock.addIntegrityFlag(FieldBlock::INTEGRITY_NOT_NULL);
            continue;
        }

        if (tokenStream.match(SqlTokenType::Keyword, "DEFAULT")) {
            const Token &defaultToken = tokenStream.peek();
            if (defaultToken.getType() != SqlTokenType::Number
                && defaultToken.getType() != SqlTokenType::String
                && defaultToken.getType() != SqlTokenType::Identifier
                && defaultToken.getType() != SqlTokenType::Keyword) {
                throw ParserException("DEFAULT constraint requires a literal value.", tokenStream.position());
            }
            tokenStream.advance();
            fieldBlock.setDefaultValue(defaultToken.getValue());
            continue;
        }

        if (tokenStream.match(SqlTokenType::Keyword, "PRIMARY")) {
            tokenStream.expect(SqlTokenType::Keyword,
                               "KEY",
                               "PRIMARY constraint requires KEY keyword.");
            fieldBlock.addIntegrityFlag(FieldBlock::INTEGRITY_PRIMARY_KEY);
            continue;
        }

        if (tokenStream.match(SqlTokenType::Keyword, "UNIQUE")) {
            fieldBlock.addIntegrityFlag(FieldBlock::INTEGRITY_UNIQUE);
            continue;
        }

        if (tokenStream.match(SqlTokenType::Keyword, "NULL")) {
            fieldBlock.addIntegrityFlag(0);
            continue;
        }

        // AUTO_INCREMENT 约束
        // 作者：NAPH130
        if (tokenStream.match(SqlTokenType::Keyword, "AUTO_INCREMENT")
            || tokenStream.match(SqlTokenType::Keyword, "AUTOINCREMENT")) {
            fieldBlock.addIntegrityFlag(FieldBlock::INTEGRITY_AUTO_INCREMENT);
            continue;
        }

        // 跳过未识别的约束关键字（如 CHECK、AUTO_INCREMENT 等）
        // 作者：NAPH130
        const Token &nextToken = tokenStream.peek();
        if (nextToken.getType() == SqlTokenType::Identifier) {
            tokenStream.advance();
            continue;
        }
        if (nextToken.getType() == SqlTokenType::Keyword) {
            // 关键字但不属于约束 → 可能是 FOREIGN KEY 等表级约束，由上层处理
            // 作者：NAPH130
            break;
        }

        break;
    }
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
 * @brief 解析 SELECT 目标字段列表
 * @details 支持普通标识符与聚合函数调用（如 COUNT(*)、SUM(col) 等），
 *          函数调用的完整形式会作为字符串保留。
 * @author NAPH130
 * @param tokenStream token 游标流
 * @return 目标字段字符串列表
 */
std::vector<std::string> Parser::parseSelectTargetList(TokenStream &tokenStream) const
{
    std::vector<std::string> result;
    bool first = true;

    while (true) {
        if (!first) {
            // 需要逗号分隔
            // 作者：NAPH130
            if (!tokenStream.consumeOptional(SqlTokenType::Symbol, ",")) {
                break;
            }
        }
        first = false;

        const Token &currentToken = tokenStream.peek();

        // 聚合函数调用：FUNC(...)
        // 作者：NAPH130
        if (currentToken.getType() == SqlTokenType::Keyword) {
            const std::string upperVal = currentToken.getValue();
            // 检查是否为聚合函数关键字
            // 作者：NAPH130
            const bool isAggFunc = (upperVal == "COUNT" || upperVal == "SUM"
                                     || upperVal == "AVG" || upperVal == "MIN"
                                     || upperVal == "MAX");
            if (isAggFunc) {
                std::string functionStr = currentToken.getValue();
                tokenStream.advance();
                tokenStream.expect(SqlTokenType::Symbol, "(", "Aggregate function requires '('.");
                const Token &argToken = tokenStream.peek();
                if ((argToken.getType() == SqlTokenType::Symbol
                     || argToken.getType() == SqlTokenType::Operator)
                    && argToken.getValue() == "*") {
                    functionStr += "(*)";
                    tokenStream.advance();
                } else if (argToken.getType() == SqlTokenType::Identifier) {
                    functionStr += "(" + argToken.getValue() + ")";
                    tokenStream.advance();
                } else {
                    throw ParserException("Aggregate function requires argument.", tokenStream.position());
                }
                tokenStream.expect(SqlTokenType::Symbol, ")", "Aggregate function requires ')'.");
                result.push_back(functionStr);
                continue;
            }
        }

        // 数字常量 / 字符串常量 / NULL / TRUE / FALSE
        // 作者：NAPH130
        if (currentToken.getType() == SqlTokenType::Number
            || currentToken.getType() == SqlTokenType::String) {
            tokenStream.advance();
            result.push_back(currentToken.getValue());
            continue;
        }
        if (currentToken.getType() == SqlTokenType::Keyword) {
            const std::string upperK = currentToken.getValue();
            if (upperK == "NULL" || upperK == "TRUE" || upperK == "FALSE") {
                tokenStream.advance();
                result.push_back(currentToken.getValue());
                continue;
            }
        }

        // 普通标识符或 table.column
        // 作者：NAPH130
        const Token &identifierToken = tokenStream.peek();
        if (identifierToken.getType() != SqlTokenType::Identifier
            && identifierToken.getType() != SqlTokenType::Keyword) {
            throw ParserException("SELECT target list requires an identifier, aggregate function, or constant.",
                                   tokenStream.position());
        }
        tokenStream.advance();

        std::string fieldName = identifierToken.getValue();

        // 处理 table.column 形式
        // 作者：NAPH130
        if (tokenStream.peek().getType() == SqlTokenType::Symbol && tokenStream.peek().getValue() == ".") {
            tokenStream.advance();
            const Token &colToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "Expected column name after '.' in SELECT target.");
            fieldName += "." + colToken.getValue();
        }

        // 列别名 AS alias
        // 作者：NAPH130
        if (tokenStream.match(SqlTokenType::Keyword, "AS")) {
            tokenStream.advance();
            const Token &aliasToken = tokenStream.peek();
            if (aliasToken.getType() != SqlTokenType::Identifier
                && aliasToken.getType() != SqlTokenType::Keyword) {
                throw ParserException("AS keyword requires an alias identifier.", tokenStream.position());
            }
            tokenStream.advance();
            fieldName += " AS " + aliasToken.getValue();
        }

        result.push_back(fieldName);
    }

    return result;
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
 * @brief 解析 DCL 语句（GRANT / REVOKE）
 * @author NAPH130
 * @param tokenStream token 游标流
 * @param operationType GRANT 或 REVOKE
 * @return DCL 语句 AST 节点
 * @details 当前阶段仅支持 GRANT ALL PRIVILEGES TO user IDENTIFIED BY password
 *          与 REVOKE ALL PRIVILEGES FROM user。
 */
std::shared_ptr<SQLStatement> Parser::parseDclStatement(TokenStream &tokenStream,
                                                         DclOperationType operationType) const
{
    const std::shared_ptr<DclStmt> statement = std::make_shared<DclStmt>();
    statement->setOperationType(operationType);

    tokenStream.expect(SqlTokenType::Keyword, "ALL",
                       "GRANT/REVOKE requires ALL keyword.");
    tokenStream.expect(SqlTokenType::Keyword, "PRIVILEGES",
                       "GRANT/REVOKE requires PRIVILEGES keyword.");

    if (operationType == DclOperationType::Grant) {
        tokenStream.expect(SqlTokenType::Keyword, "TO",
                           "GRANT requires TO keyword.");
    } else {
        tokenStream.expect(SqlTokenType::Keyword, "FROM",
                           "REVOKE requires FROM keyword.");
    }

    const Token &userNameToken = tokenStream.expect(
        SqlTokenType::Identifier,
        "DCL statement requires a user identifier.");
    statement->setUserName(userNameToken.getValue());

    if (operationType == DclOperationType::Grant) {
        if (tokenStream.match(SqlTokenType::Keyword, "IDENTIFIED")) {
            tokenStream.expect(SqlTokenType::Keyword, "BY",
                               "IDENTIFIED requires BY keyword.");
            const Token &passwordToken = tokenStream.expect(
                SqlTokenType::Identifier,
                "IDENTIFIED BY requires a password.");
            statement->setPassword(passwordToken.getValue());
        }
    }

    return statement;
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
