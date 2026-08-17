#pragma once

#include <vulkan/vulkan.h>
#include <unordered_set>
#include <functional>
#include <vector>
#include <string> // 添加 string 头文件

#include "builder.h"
#include "utility/logger.h"

class VulkanInstanceBuilder : public Builder
{
private:
    typedef struct ApplicationInfo
    {
        // 使用 std::string 存储名称以避免悬空指针
        std::string application_name;
        std::string engine_name;
        uint32_t application_version;
        uint32_t engine_version;
        uint32_t highest_api_version;
        void* pNext;
    } ApplicationInfo;

    typedef struct InstanceInfo
    {
        ApplicationInfo app_info;
        const char* const* required_layers;
        uint32_t required_layer_count;
        const char* const* required_extensions;
        uint32_t required_extension_count;
    } InstanceInfo;

    InstanceInfo instance_info_;
    std::vector<std::function<void(VkInstance)>> build_callbacks_;

    // 持久化存储 layers/extensions 字符串和指针
    std::vector<std::string> required_layer_names_;
    std::vector<const char*> required_layer_ptrs_;
    std::vector<std::string> required_extension_names_;
    std::vector<const char*> required_extension_ptrs_;

public:
    VulkanInstanceBuilder();
    ~VulkanInstanceBuilder();

    // callback on build
    void AddListener(std::function<void(VkInstance)> callback)
    {
        build_callbacks_.push_back(callback);
    }

    // Build the Vulkan instance
    bool Build() override;

    // Application Info
    VulkanInstanceBuilder& SetApplicationName(std::string name)
    {
        instance_info_.app_info.application_name = std::move(name);
        return *this;
    }
    VulkanInstanceBuilder& SetApplicationVersion(uint8_t major, uint8_t minor, uint8_t patch)
    {
        instance_info_.app_info.application_version = VK_MAKE_VERSION(major, minor, patch);
        return *this;
    }
    VulkanInstanceBuilder& SetEngineName(std::string name)
    {
        instance_info_.app_info.engine_name = std::move(name);
        return *this;
    }
    VulkanInstanceBuilder& SetEngineVersion(uint8_t major, uint8_t minor, uint8_t patch)
    {
        instance_info_.app_info.engine_version = VK_MAKE_VERSION(major, minor, patch);
        return *this;
    }
    VulkanInstanceBuilder& SetApiHighestVersion(uint8_t major, uint8_t minor, uint8_t patch)
    {
        instance_info_.app_info.highest_api_version = VK_MAKE_API_VERSION(0, major, minor, patch);
        return *this;
    }
    VulkanInstanceBuilder& SetApplicationNext(void* pNext)
    {
        instance_info_.app_info.pNext = pNext;
        return *this;
    }

    // layer
    VulkanInstanceBuilder& SetRequiredLayers(const char* const* layers, uint32_t count);

    // extension
    VulkanInstanceBuilder& SetRequiredExtensions(const char* const* extensions, uint32_t count);
};

class VulkanInstanceHelper
{
public:
    VulkanInstanceHelper() = default;
    ~VulkanInstanceHelper();

    VkInstance GetVulkanInstance() const { return vkInstance_; }
    VulkanInstanceBuilder& GetVulkanInstanceBuilder() 
    {
        vkInstanceBuilder_.AddListener([this](VkInstance instance) {
            vkInstance_ = instance;
        }); 
        return vkInstanceBuilder_; 
    }

private:
    VulkanInstanceBuilder vkInstanceBuilder_;
    VkInstance vkInstance_;
};