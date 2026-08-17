#pragma once

#include <string>
#include <iostream>

// 日志级别枚举
enum class ELogLevel {
    kDebug,
    kInfo,
    kWarning,
    kError
};

class Logger
{
public:
    Logger() = default;
    ~Logger() = default;

    static void LogDebug(const std::string& message);
    static void LogInfo(const std::string& message);
    static void LogWarning(const std::string& message);
    static void LogError(const std::string& message);

private:
    static void Log(ELogLevel level, const std::string& message);
    static std::string LogLevelToString(ELogLevel level);
};
