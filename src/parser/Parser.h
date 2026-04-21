#pragma once

#include <memory>
#include <vector>

#include "models/parser/CreateDbStmt.h"
#include "models/parser/CreateTableStmt.h"
#include "models/parser/InsertStmt.h"
#include "models/parser/SQLStatement.h"
#include "models/parser/SelectStmt.h"
#include "models/tokenizer/Token.h"
#include "ParseResult.h"
#include "TokenStream.h"

/**
 * @class Parser
 * @brief SQL 语法分析器
 * @details 负责消费 Tokenizer 产出的 token 流，进行语法分析并构造 AST。
 * @author YuzhSong
 */
class Parser
{
public:
    /**
     * @brief 语法分析统一入口
     * @author YuzhSong
     * @param tokens 由 Tokenizer 产出的 token 序列
     * @return 语法分析结果
     */
    ParseResult parse(const std::vector<Token> &tokens) const;

private:
    /**
     * @brief 解析通用 SQL 语句入口
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return AST 根节点
     */
    std::shared_ptr<SQLStatement> parseStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 CREATE 分支语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return CREATE 语句 AST 根节点
     */
    std::shared_ptr<SQLStatement> parseCreateStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 CREATE DATABASE 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return CreateDbStmt 节点
     */
    std::shared_ptr<CreateDbStmt> parseCreateDatabaseStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 CREATE TABLE 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return CreateTableStmt 节点
     */
    std::shared_ptr<CreateTableStmt> parseCreateTableStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 INSERT 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return InsertStmt 节点
     */
    std::shared_ptr<InsertStmt> parseInsertStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 SELECT 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return SelectStmt 节点
     */
    std::shared_ptr<SelectStmt> parseSelectStatement(TokenStream &tokenStream) const;

    /**
     * @brief 断言语句结尾无多余 token
     * @author YuzhSong
     * @param tokenStream token 游标流
     */
    void expectStatementEnd(TokenStream &tokenStream) const;
};
