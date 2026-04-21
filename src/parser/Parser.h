#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "models/parser/CreateDbStmt.h"
#include "models/parser/CreateTableStmt.h"
#include "models/parser/InsertStmt.h"
#include "models/parser/SQLStatement.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/ParseResult.h"
#include "models/tokenizer/Token.h"
#include "TokenStream.h"

class Core;

/**
 * @class Parser
 * @brief SQL 语法分析器
 * @details 负责消费 Tokenizer 产出的 token 流，完成语法级校验并构建 AST。
 * @author YuzhSong
 */
class Parser
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit Parser(Core *core = nullptr);

    /**
     * @brief 语法分析统一入口
     * @author YuzhSong
     * @param tokens 由 Tokenizer 产出的 token 序列
     * @return 解析结果，成功时包含 AST，失败时包含错误消息与 token 下标
     */
    ParseResult parse(const std::vector<Token> &tokens) const;

private:
    Core *core;

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
     * @brief 解析字段定义并构造 FieldBlock
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @param fieldOrder 字段顺序号
     * @return 构造完成的字段块
     */
    FieldBlock parseFieldDefinition(TokenStream &tokenStream, std::int32_t fieldOrder) const;

    /**
     * @brief 解析字段类型定义
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @param fieldBlock 待填充字段块
     */
    void parseFieldType(TokenStream &tokenStream, FieldBlock &fieldBlock) const;

    /**
     * @brief 解析标识符列表
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return 标识符序列
     */
    std::vector<std::string> parseIdentifierList(TokenStream &tokenStream) const;

    /**
     * @brief 解析值列表
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return 值序列（按 token 文本保留）
     */
    std::vector<std::string> parseValueList(TokenStream &tokenStream) const;

    /**
     * @brief 断言语句结尾无多余 token，并消费可选分号与 EndOfFile
     * @author YuzhSong
     * @param tokenStream token 游标流
     */
    void expectStatementEnd(TokenStream &tokenStream) const;
};
