#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "TokenStream.h"
#include "models/parser/ConditionNode.h"
#include "models/parser/CreateDbStmt.h"
#include "models/parser/CreateTableStmt.h"
#include "models/parser/DeleteStmt.h"
#include "models/parser/DropStmt.h"
#include "models/parser/InsertStmt.h"
#include "models/parser/ParseResult.h"
#include "models/parser/SQLStatement.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/ShowStmt.h"
#include "models/parser/UpdateStmt.h"
#include "models/parser/UseDbStmt.h"
#include "models/parser/UseStmt.h"
#include "models/tokenizer/Token.h"

class Core;

/**
 * @class Parser
 * @brief SQL 语法分析器
 * @details 负责消费 Tokenizer 输出的 token 序列，完成语法校验并构建语句 AST。
 * @author YuzhSong
 */
class Parser
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     */
    explicit Parser(Core *core = nullptr);

    /**
     * @brief 语法分析统一入口
     * @author YuzhSong
     * @param tokens 词法分析输出的 token 序列
     * @return 解析结果，成功时包含 AST，失败时包含错误信息和 token 下标
     */
    ParseResult parse(const std::vector<Token> &tokens) const;

private:
    Core *core;

    /**
     * @brief 解析 SQL 语句入口
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return SQL 语句 AST 根节点
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
     * @brief 解析 USE 语句
     * @details 支持 `USE dbName` 与 `USE DATABASE dbName` 两种写法。
     * @author NAPH130
     * @param tokenStream token 游标流
     * @return USE 语句 AST 节点
     */
    std::shared_ptr<SQLStatement> parseUseStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 SHOW 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return ShowStmt 节点
     */
    std::shared_ptr<ShowStmt> parseShowStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 DROP 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return DropStmt 节点
     */
    std::shared_ptr<DropStmt> parseDropStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 DELETE 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return DeleteStmt 节点
     */
    std::shared_ptr<DeleteStmt> parseDeleteStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 UPDATE 语句
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return UpdateStmt 节点
     */
    std::shared_ptr<UpdateStmt> parseUpdateStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 TRUNCATE TABLE 语句
     * @author NAPH130
     * @param tokenStream token 游标流
     * @return TRUNCATE 语句 AST 节点
     */
    std::shared_ptr<SQLStatement> parseTruncateStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 ALTER TABLE 语句
     * @author NAPH130
     * @param tokenStream token 游标流
     * @return ALTER 语句 AST 节点
     */
    std::shared_ptr<SQLStatement> parseAlterStatement(TokenStream &tokenStream) const;

    /**
     * @brief 解析 UPDATE SET 赋值列表
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @param columnNames 输出字段名列表
     * @param values 输出字段值列表
     */
    void parseUpdateAssignmentList(TokenStream &tokenStream,
                                   std::vector<std::string> &columnNames,
                                   std::vector<std::string> &values) const;

    /**
     * @brief 解析 WHERE 条件中的 OR 表达式层级
     * @details OR 为最低优先级，按左结合构建条件树。
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return OR 层级条件树根节点
     */
    std::shared_ptr<ConditionNode> parseConditionOr(TokenStream &tokenStream) const;

    /**
     * @brief 解析 WHERE 条件中的 AND 表达式层级
     * @details AND 优先级高于 OR，按左结合构建条件树。
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return AND 层级条件树根节点
     */
    std::shared_ptr<ConditionNode> parseConditionAnd(TokenStream &tokenStream) const;

    /**
     * @brief 解析 WHERE 条件中的基础谓词
     * @details 支持括号表达式、比较表达式、IN/EXISTS 子查询。
     * @author YuzhSong / NAPH130
     * @param tokenStream token 游标流
     * @return 基础谓词条件节点
     */
    std::shared_ptr<ConditionNode> parsePredicate(TokenStream &tokenStream) const;

    /**
     * @brief 解析 IN / EXISTS / NOT IN / NOT EXISTS 子查询或值列表
     * @author NAPH130
     * @param tokenStream token 游标流
     * @param leftOperand 已解析的左操作数
     * @return 条件节点
     */
    std::shared_ptr<ConditionNode> parseInOrExists(TokenStream &tokenStream,
                                                     const std::string &leftOperand) const;

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
     * @brief 解析字段可选约束（NOT NULL / DEFAULT / PRIMARY KEY / UNIQUE）
     * @author NAPH130
     * @param tokenStream token 游标流
     * @param fieldBlock 待填充字段块，约束及默认值会直接写入
     */
    void parseFieldConstraints(TokenStream &tokenStream, FieldBlock &fieldBlock) const;

    /**
     * @brief 解析标识符列表
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return 标识符序列
     */
    std::vector<std::string> parseIdentifierList(TokenStream &tokenStream) const;

    /**
     * @brief 解析 SELECT 目标字段列表（支持标识符与聚合函数调用）
     * @author NAPH130
     * @param tokenStream token 游标流
     * @return 目标字段字符串列表
     */
    std::vector<std::string> parseSelectTargetList(TokenStream &tokenStream) const;

    /**
     * @brief 解析值列表
     * @author YuzhSong
     * @param tokenStream token 游标流
     * @return 值序列（保持 token 文本）
     */
    std::vector<std::string> parseValueList(TokenStream &tokenStream) const;

    /**
     * @brief 解析 JOIN 子句序列
     * @author NAPH130
     * @param tokenStream token 游标流
     * @return JOIN 子句信息列表
     */
    std::vector<JoinInfo> parseJoinClauses(TokenStream &tokenStream) const;

    /**
     * @brief 断言语句结束并消费分号与 EndOfFile
     * @author YuzhSong
     * @param tokenStream token 游标流
     */
    void expectStatementEnd(TokenStream &tokenStream) const;
};
