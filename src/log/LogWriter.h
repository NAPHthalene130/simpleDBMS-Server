#pragma once

#include <mutex>
#include <string>

/**
 * @class LogWriter
 * @brief 文件日志写入工具类
 * @details 负责将统一格式的日志内容追加写入 src/log/logs.log 文件。
 * @author NAPH130
 */
class LogWriter
{
public:
    /**
     * @brief 写入 DEBUG 级别日志
     * @author NAPH130
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void debug(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message);

    /**
     * @brief 写入 INFO 级别日志
     * @author NAPH130
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void info(const std::string &moduleName,
                     const std::string &className,
                     const std::string &methodName,
                     const std::string &message);

    /**
     * @brief 写入 WARNING 级别日志
     * @author NAPH130
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void warning(const std::string &moduleName,
                        const std::string &className,
                        const std::string &methodName,
                        const std::string &message);

    /**
     * @brief 写入 ERROR 级别日志
     * @author NAPH130
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void error(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message);

    /**
     * @brief 写入 FATAL 级别日志
     * @author NAPH130
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void fatal(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message);

private:
    /**
     * @brief 写入统一格式日志
     * @author NAPH130
     * @param status 日志级别
     * @param moduleName 模块名
     * @param className 类名
     * @param methodName 方法名
     * @param message 日志信息
     */
    static void writeLog(const std::string &status,
                         const std::string &moduleName,
                         const std::string &className,
                         const std::string &methodName,
                         const std::string &message);

    /**
     * @brief 获取日志文件路径
     * @author NAPH130
     * @return 日志文件绝对路径字符串
     */
    static std::string getLogFilePath();

    /**
     * @brief 生成当前时间字符串
     * @author NAPH130
     * @return 格式化后的时间字符串
     */
    static std::string buildTimestamp();

private:
    static std::mutex logMutex;
};
