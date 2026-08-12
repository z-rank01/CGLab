#include "logger.h"

// 全局日志对象
extern Logger kLogger;

/// @brief log message with target level
/// @param level level of the log message
/// @param message message content
void Logger::Log(ELogLevel level, const std::string& message)
{
    std::cout << '[' << LogLevelToString(level) << "] " << message << std::endl;
}

/// @brief log message with debug level
/// @param message message content
void Logger::LogDebug(const std::string& message)
{
    Log(ELogLevel::kDebug, message);
}

/// @brief log message with infomation level
/// @param message message content
void Logger::LogInfo(const std::string& message)
{
    Log(ELogLevel::kInfo, message);
}

/// @brief log message with warning level
/// @param message message content
void Logger::LogWarning(const std::string& message)
{
    Log(ELogLevel::kWarning, message);
}

/// @brief log message with error level
/// @param message message content
void Logger::LogError(const std::string& message)
{
    Log(ELogLevel::kError, message);
}

/// @brief convert log level enum to string
/// @param level log level enum
/// @return string of the log level
std::string Logger::LogLevelToString(ELogLevel level)
{
    switch (level)
    {
    case ELogLevel::kDebug: return "Debug";
    case ELogLevel::kInfo: return "Info";
    case ELogLevel::kWarning: return "Warning";
    case ELogLevel::kError: return "Error";
    default:
        return "Undefined";
    }
}
