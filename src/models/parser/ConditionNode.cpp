#include "ConditionNode.h"

ConditionNode::ConditionNode() = default;

const std::string &ConditionNode::getLeftOperand() const
{
    return leftOperand;
}

void ConditionNode::setLeftOperand(const std::string &leftOperand)
{
    this->leftOperand = leftOperand;
}

const std::string &ConditionNode::getOperator() const
{
    return operatorValue;
}

void ConditionNode::setOperator(const std::string &operatorValue)
{
    this->operatorValue = operatorValue;
}

const std::string &ConditionNode::getRightOperand() const
{
    return rightOperand;
}

void ConditionNode::setRightOperand(const std::string &rightOperand)
{
    this->rightOperand = rightOperand;
}

const std::shared_ptr<ConditionNode> &ConditionNode::getLeftNode() const
{
    return leftNode;
}

void ConditionNode::setLeftNode(const std::shared_ptr<ConditionNode> &leftNode)
{
    this->leftNode = leftNode;
}

const std::shared_ptr<ConditionNode> &ConditionNode::getRightNode() const
{
    return rightNode;
}

void ConditionNode::setRightNode(const std::shared_ptr<ConditionNode> &rightNode)
{
    this->rightNode = rightNode;
}
