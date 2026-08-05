#pragma once

#include "vulkan_instance.h"
#include <iostream> // 包含 iostream 以使用 std::cerr 和 std::cout

VulkanInstanceHelper::~VulkanInstanceHelper()
{
    vkDestroyInstance(vkInstance_, nullptr);
}

// ------------------------------------
// VulkanInstanceBuilder Implementation
// ------------------------------------

VulkanInstanceBuilder::VulkanInstanceBuilder()
{
}

VulkanInstanceBuilder::~VulkanInstanceBuilder()
{
}

bool VulkanInstanceBuilder::Build()
{
    // Create Vulkan instance
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = instance_info_.app_info.application_name.c_str();
    app_info.applicationVersion = instance_info_.app_info.application_version;
    app_info.pEngineName = instance_info_.app_info.engine_name.c_str();
    app_info.engineVersion = instance_info_.app_info.engine_version;
    app_info.apiVersion = instance_info_.app_info.highest_api_version;
    app_info.pNext = instance_info_.app_info.pNext;

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledLayerCount = instance_info_.required_layer_count;
    instance_info.ppEnabledLayerNames = instance_info_.required_layers;
    instance_info.enabledExtensionCount = instance_info_.required_extension_count;
    instance_info.ppEnabledExtensionNames = instance_info_.required_extensions;
    instance_info.pNext = nullptr;

    VkInstance instance;
    if (Logger::LogWithVkResult(
        vkCreateInstance(&instance_info, nullptr, &instance), 
        "Failed to create Vulkan instance", 
        "Vulkan instance created successfully"))
    {
        for (auto& cb : build_callbacks_) 
        {
            cb(instance);
        }
        return true;
    }
    return false;
}

VulkanInstanceBuilder& VulkanInstanceBuilder::SetRequiredLayers(const char* const* layers, uint32_t count)
{
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    std::unordered_set<std::string> available_layer_names;
    for (const auto& layer : available_layers)
    {
        available_layer_names.insert(layer.layerName);
    }

    required_layer_names_.clear();
    for (uint32_t i = 0; i < count; ++i)
    {
        if (available_layer_names.find(layers[i]) != available_layer_names.end())
        {
            required_layer_names_.emplace_back(layers[i]);
        }
        else
        {
            std::cerr << "Layer not supported: " << layers[i] << std::endl;
        }
    }

    required_layer_ptrs_.clear();
    required_layer_ptrs_.reserve(required_layer_names_.size());
    for (const auto& layer_name : required_layer_names_)
    {
        required_layer_ptrs_.push_back(layer_name.c_str());
    }

    instance_info_.required_layers = required_layer_ptrs_.data();
    instance_info_.required_layer_count = static_cast<uint32_t>(required_layer_ptrs_.size());
    return *this;
}

VulkanInstanceBuilder& VulkanInstanceBuilder::SetRequiredExtensions(const char* const* extensions, uint32_t count)
{
    uint32_t extension_count;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_extensions.data());

    std::unordered_set<std::string> available_extension_names;
    for (const auto& extension : available_extensions)
    {
        available_extension_names.insert(extension.extensionName);
    }

    required_extension_names_.clear();
    for (uint32_t i = 0; i < count; ++i)
    {
        std::cout << "Request extension: " << extensions[i] << std::endl;
        if (available_extension_names.find(extensions[i]) != available_extension_names.end())
        {
            required_extension_names_.emplace_back(extensions[i]);
        }
        else
        {
            std::cerr << "Extension not supported: " << extensions[i] << std::endl;
        }
    }

    required_extension_ptrs_.clear();
    required_extension_ptrs_.reserve(required_extension_names_.size());
    for (const auto& ext_name : required_extension_names_)
    {
        required_extension_ptrs_.push_back(ext_name.c_str());
    }

    std::cout << "Final extensions passed to Vulkan:" << std::endl;
    for (const auto& ext : required_extension_ptrs_) {
        std::cout << "  " << ext << std::endl;
    }

    instance_info_.required_extensions = required_extension_ptrs_.data();
    instance_info_.required_extension_count = static_cast<uint32_t>(required_extension_ptrs_.size());
    return *this;
}
