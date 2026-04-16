#pragma once

#include <memory>
#include <string>

/**
 * @class ConditionNode
 * @brief 条件树节点类
 * @details 封装 WHERE 子句中的比较条件或逻辑连接关系，支持构成树状条件表达式。
 * @author NAPH130
 */
class ConditionNode
{
public:
    ConditionNode();

    const std::string &getLeftOperand() const;
    void setLeftOperand(const std::string &leftOperand);

    const std::string &getOperator() const;
    void setOperator(const std::string &operatorValue);

    const std::string &getRightOperand() const;
    void setRightOperand(const std::string &rightOperand);

    const std::shared_ptr<ConditionNode> &getLeftNode() const;
    void setLeftNode(const std::shared_ptr<ConditionNode> &leftNode);

    const std::shared_ptr<ConditionNode> &getRightNode() const;
    void setRightNode(const std::shared_ptr<ConditionNode> &rightNode);

private:
    std::string leftOperand;
    std::string operatorValue;
    std::string rightOperand;
    std::shared_ptr<ConditionNode> leftNode;
    std::shared_ptr<ConditionNode> rightNode;
};
