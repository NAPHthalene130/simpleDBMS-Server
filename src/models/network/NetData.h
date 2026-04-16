#pragma once

#include <string>

/**
 * @class NetData
 * @brief 网络传输数据类
 * @details 封装网络层传输过程中使用的数据类型与数据内容。
 * @author NAPH130
 */
class NetData
{
public:
    /**
     * @brief 默认构造函数
     * @author NAPH130
     */
    NetData();

    /**
     * @brief 带参构造函数
     * @author NAPH130
     * @param type 数据类型
     * @param content 数据内容
     */
    NetData(const std::string &type, const std::string &content);

    /**
     * @brief 获取数据类型
     * @author NAPH130
     * @return 数据类型
     */
    const std::string &getType() const;

    /**
     * @brief 设置数据类型
     * @author NAPH130
     * @param type 数据类型
     */
    void setType(const std::string &type);

    /**
     * @brief 获取数据内容
     * @author NAPH130
     * @return 数据内容
     */
    const std::string &getContent() const;

    /**
     * @brief 设置数据内容
     * @author NAPH130
     * @param content 数据内容
     */
    void setContent(const std::string &content);

private:
    std::string type;
    std::string content;
};
