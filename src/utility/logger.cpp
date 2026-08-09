#pragma once

#include "logger.h"

#include <vulkan/vulkan.h>

// 全局日志对象
extern Logger kLogger;

// VkResult 到字符串的宏定义
#define VK_RESULT_STRING(result) \
    case result: return #result;

#define VK_RESULT_STRING_MAPPED(result, str) \
    case result: return str;


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

/// @brief log message according to the Vulkan API calling result
/// @param result Vulkan API calling result
/// @param messageOnFail message content on failure
/// @param level level of the log message (default is error)
/// @return true if the result is success, otherwise false
bool Logger::LogWithVkResult(std::int32_t result, const std::string& messageOnFail, const std::string& messageOnSuccess)
{
    if (IsVulkanResultSuccess(result))
    {
        LogDebug(messageOnSuccess);
        return true;
    }
    else
    {
        LogError(VulkanResultToString(result) + ": " + messageOnFail);
        return false;
    }
}

/// @brief convert Vulkan API result to human-readable string
/// @param result Vulkan API result code
/// @return human-readable string of the result
std::string Logger::VulkanResultToString(std::int32_t result)
{
    switch (result)
    {
        VK_RESULT_STRING(VK_SUCCESS)
            VK_RESULT_STRING(VK_NOT_READY)
            VK_RESULT_STRING(VK_TIMEOUT)
            VK_RESULT_STRING(VK_EVENT_SET)
            VK_RESULT_STRING(VK_EVENT_RESET)
            VK_RESULT_STRING(VK_INCOMPLETE)
            VK_RESULT_STRING(VK_ERROR_OUT_OF_HOST_MEMORY)
            VK_RESULT_STRING(VK_ERROR_OUT_OF_DEVICE_MEMORY)
            VK_RESULT_STRING(VK_ERROR_INITIALIZATION_FAILED)
            VK_RESULT_STRING(VK_ERROR_DEVICE_LOST)
            VK_RESULT_STRING(VK_ERROR_MEMORY_MAP_FAILED)
            VK_RESULT_STRING(VK_ERROR_LAYER_NOT_PRESENT)
            VK_RESULT_STRING(VK_ERROR_EXTENSION_NOT_PRESENT)
            VK_RESULT_STRING(VK_ERROR_FEATURE_NOT_PRESENT)
            VK_RESULT_STRING(VK_ERROR_INCOMPATIBLE_DRIVER)
            VK_RESULT_STRING(VK_ERROR_TOO_MANY_OBJECTS)
            VK_RESULT_STRING(VK_ERROR_FORMAT_NOT_SUPPORTED)
            VK_RESULT_STRING(VK_ERROR_FRAGMENTED_POOL)
            VK_RESULT_STRING(VK_ERROR_UNKNOWN)
            VK_RESULT_STRING(VK_ERROR_OUT_OF_POOL_MEMORY)
            VK_RESULT_STRING(VK_ERROR_INVALID_EXTERNAL_HANDLE)
            VK_RESULT_STRING(VK_ERROR_FRAGMENTATION)
            VK_RESULT_STRING(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS)
            VK_RESULT_STRING(VK_ERROR_SURFACE_LOST_KHR)
            VK_RESULT_STRING(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
            VK_RESULT_STRING(VK_SUBOPTIMAL_KHR)
            VK_RESULT_STRING(VK_ERROR_OUT_OF_DATE_KHR)
            VK_RESULT_STRING(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
            VK_RESULT_STRING(VK_ERROR_VALIDATION_FAILED_EXT)
            VK_RESULT_STRING(VK_ERROR_INVALID_SHADER_NV)
            VK_RESULT_STRING(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT)
            VK_RESULT_STRING(VK_ERROR_NOT_PERMITTED_EXT)
            VK_RESULT_STRING(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
            VK_RESULT_STRING(VK_THREAD_IDLE_KHR)
            VK_RESULT_STRING(VK_THREAD_DONE_KHR)
            VK_RESULT_STRING(VK_OPERATION_DEFERRED_KHR)
            VK_RESULT_STRING(VK_OPERATION_NOT_DEFERRED_KHR)
            VK_RESULT_STRING(VK_PIPELINE_COMPILE_REQUIRED_EXT)
    default:
        return "Unknown error";
    }
}

/// @brief check if the Vulkan API result is success
/// @param result Vulkan API result code
/// @return true if the result is success, otherwise false
bool Logger::IsVulkanResultSuccess(std::int32_t result)
{
    return result == VK_SUCCESS;
}

/// @brief convert log level enum to string
/// @param level log level enum
/// @return string of the log level
std::string Logger::LogLevelToString(ELogLevel level)
{
    switch (level)
    {
        VK_RESULT_STRING_MAPPED(ELogLevel::kDebug, "Debug");
        VK_RESULT_STRING_MAPPED(ELogLevel::kInfo, "Info");
        VK_RESULT_STRING_MAPPED(ELogLevel::kWarning, "Warning");
        VK_RESULT_STRING_MAPPED(ELogLevel::kError, "Error");
    default:
        return "Undefined";
    }
}
