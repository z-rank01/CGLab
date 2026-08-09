#pragma once

#include <cstdint>
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
    // Kept as an integer at the public utility boundary so generic logging does
    // not force every consumer to include Vulkan headers.
    static bool LogWithVkResult(std::int32_t result, const std::string& messageOnFail, const std::string& messageOnSuccess);
    
private:
    static void Log(ELogLevel level, const std::string& message);
    static std::string LogLevelToString(ELogLevel level);
    static std::string VulkanResultToString(std::int32_t result);
    static bool IsVulkanResultSuccess(std::int32_t result);
};
