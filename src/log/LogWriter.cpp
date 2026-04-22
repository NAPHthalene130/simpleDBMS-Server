#include "LogWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

std::mutex LogWriter::logMutex;

void LogWriter::debug(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message)
{
    writeLog("DEBUG", moduleName, className, methodName, message);
}

void LogWriter::info(const std::string &moduleName,
                     const std::string &className,
                     const std::string &methodName,
                     const std::string &message)
{
    writeLog("INFO", moduleName, className, methodName, message);
}

void LogWriter::warning(const std::string &moduleName,
                        const std::string &className,
                        const std::string &methodName,
                        const std::string &message)
{
    writeLog("WARNING", moduleName, className, methodName, message);
}

void LogWriter::error(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message)
{
    writeLog("ERROR", moduleName, className, methodName, message);
}

void LogWriter::fatal(const std::string &moduleName,
                      const std::string &className,
                      const std::string &methodName,
                      const std::string &message)
{
    writeLog("FATAL", moduleName, className, methodName, message);
}

void LogWriter::writeLog(const std::string &status,
                         const std::string &moduleName,
                         const std::string &className,
                         const std::string &methodName,
                         const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);

    const std::string logFilePath = getLogFilePath();
    std::ofstream outputStream(logFilePath, std::ios::app);
    if (!outputStream.is_open()) {
        return;
    }

    outputStream << "[" << status << "]"
                 << "[" << moduleName << "-" << className << "-" << methodName << "]"
                 << "[" << buildTimestamp() << "]"
                 << ":" << message << std::endl;
}

std::string LogWriter::getLogFilePath()
{
    const std::filesystem::path logDirectory = std::filesystem::path(__FILE__).parent_path();
    std::filesystem::create_directories(logDirectory);
    return (logDirectory / "logs.log").string();
}

std::string LogWriter::buildTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    std::ostringstream timeStream;
    timeStream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return timeStream.str();
}
