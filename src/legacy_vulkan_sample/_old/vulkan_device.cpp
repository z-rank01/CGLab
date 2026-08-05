#pragma once

#include "vulkan_device.h"
#include <utility/logger.h>

///-------------------------------------------------------------------------------------------------
// helper function declaration
int CountSupportedPropertiesOrFeatures(const SVulkanPhysicalDeviceConfig& device_config, VkPhysicalDevice physical_device,
    VkPhysicalDeviceProperties supported_properties, VkPhysicalDeviceFeatures supported_features);
bool CheckPhysicalDeviceFeatureAvailable(EPhysicalDeviceFeatures feature, VkPhysicalDeviceFeatures supported_features);

///-------------------------------------------------------------------------------------------------
// VulkanDeviceHelper implementation
VulkanDeviceHelper::VulkanDeviceHelper()
{
    vkPhysicalDevice_ = VK_NULL_HANDLE;
    vkLogicalDevice_ = VK_NULL_HANDLE;

    // Initialize the Vulkan device features
    vkSupportedFeatures_ = {};
    vkSupportedProperties_ = {};
}

VulkanDeviceHelper::~VulkanDeviceHelper()
{
    vkDestroyDevice(vkLogicalDevice_, nullptr);
}

bool VulkanDeviceHelper::CreateLogicalDevice(SVulkanDeviceConfig config)
{
    // create logical device
    VkDeviceCreateInfo device_info = {};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = config.queue_create_infos.size();
    device_info.pQueueCreateInfos = config.queue_create_infos.data();
    device_info.enabledExtensionCount = static_cast<uint32_t>(config.device_extensions.size());
    device_info.ppEnabledExtensionNames = config.device_extensions.data();
    device_info.enabledLayerCount = 0;
    device_info.ppEnabledLayerNames = nullptr;
    device_info.pEnabledFeatures = nullptr; // Optional, can be set to nullptr if no specific features are required
    return Logger::LogWithVkResult(
        vkCreateDevice(vkPhysicalDevice_, &device_info, nullptr, &vkLogicalDevice_),
        "Failed to create logical device",
        "Succeeded in creating logical device");
}

bool VulkanDeviceHelper::CreatePhysicalDevice(SVulkanPhysicalDeviceConfig config, const VkInstance& instance)
{
    // enumerate physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        Logger::LogError("Failed to find GPUs with Vulkan support");
        return false;
    }

    // retrieve all physical devices
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // pick the physical device that supports the most required features
    vkPhysicalDevice_ = PickPhysicalDevice(config, devices);

    // Check if a suitable device was found
    if (vkPhysicalDevice_ == VK_NULL_HANDLE)
    {
        Logger::LogError("Failed to find a suitable physical device.");
        return false;
    }

    // Log the selected device
    VkPhysicalDeviceProperties selected_properties;
    vkGetPhysicalDeviceProperties(vkPhysicalDevice_, &selected_properties);
    Logger::LogInfo("Selected Physical Device: " + std::string(selected_properties.deviceName));

    return true;
}

bool VulkanDeviceHelper::WaitIdle() const
{
    return Logger::LogWithVkResult(
        vkDeviceWaitIdle(vkLogicalDevice_),
        "Failed to wait for device idle",
        "Succeeded in waiting for device idle");
}

VkPhysicalDevice VulkanDeviceHelper::PickPhysicalDevice(const SVulkanPhysicalDeviceConfig& config, const std::vector<VkPhysicalDevice>& physical_devices)
{
    int max_cnt = -1;             // Track maximum features supported
    int best_device_index = -1; // Track index of the best device found so far

    for (int i = 0; i < physical_devices.size(); ++i)
    {
        const auto& physical_device = physical_devices[i];

        // get the physical device properties and features
        vkGetPhysicalDeviceProperties(physical_device, &vkSupportedProperties_);
        vkGetPhysicalDeviceFeatures(physical_device, &vkSupportedFeatures_);

        // log the physical device properties and features
        Logger::LogInfo("-------------------------");
        Logger::LogInfo("Checking Physical Device: " + std::string(vkSupportedProperties_.deviceName));
        // Extract Vulkan version components (major.minor.patch)
        uint32_t apiVersion = vkSupportedProperties_.apiVersion;
        uint32_t major = VK_VERSION_MAJOR(apiVersion);
        uint32_t minor = VK_VERSION_MINOR(apiVersion);
        uint32_t patch = VK_VERSION_PATCH(apiVersion);
        Logger::LogInfo("Vulkan API Version: " + std::to_string(major) + "." +
            std::to_string(minor) + "." + std::to_string(patch));


        // count the number of supported properties or features
        int current_cnt = CountSupportedPropertiesOrFeatures(config, physical_device, vkSupportedProperties_, vkSupportedFeatures_);
        Logger::LogInfo("Supported feature count: " + std::to_string(current_cnt));
        Logger::LogInfo("-------------------------");

        // Update best device if current device supports more features
        if (current_cnt > max_cnt)
        {
            max_cnt = current_cnt;
            best_device_index = i;
        }
    }

    // Check if a suitable device was found
    if (best_device_index == -1)
    {
        Logger::LogError("No suitable physical device found meeting the minimum requirements.");
        return VK_NULL_HANDLE; // Return NULL handle if no suitable device is found
    }

    // Return the device that supported the most features
    return physical_devices[best_device_index];
}

///-------------------------------------------------------------------------------------------------

// helper function:
// count the number of supported properties or features
int CountSupportedPropertiesOrFeatures(const SVulkanPhysicalDeviceConfig& device_config, VkPhysicalDevice physical_device,
    VkPhysicalDeviceProperties supported_properties, VkPhysicalDeviceFeatures supported_features)
{
    // Check properies: device type and API version
    if ((device_config.physical_device_type &&
        device_config.physical_device_type != supported_properties.deviceType) ||
        (device_config.physical_device_api_version &&
            supported_properties.apiVersion < VK_MAKE_API_VERSION(device_config.physical_device_api_version[0],
                device_config.physical_device_api_version[1],
                device_config.physical_device_api_version[2],
                device_config.physical_device_api_version[3])))
    {
        return -1;
    }

    // Count supported features
    int supported_feature_cnt = 0;
    for (const auto& feature : device_config.physical_device_features)
    {
        if (CheckPhysicalDeviceFeatureAvailable(feature, supported_features))
        {
            ++supported_feature_cnt;
        }
        else
        {
            Logger::LogWarning("Physical device feature not supported: " + std::to_string(static_cast<int>(feature)));
        }
    }

    return supported_feature_cnt;
}

bool CheckPhysicalDeviceFeatureAvailable(EPhysicalDeviceFeatures feature, VkPhysicalDeviceFeatures supported_features)
{
    switch (feature)
    {
    case EPhysicalDeviceFeatures::RobustBufferAccess:
        return supported_features.robustBufferAccess;
    case EPhysicalDeviceFeatures::FullDrawIndexUint32:
        return supported_features.fullDrawIndexUint32;
    case EPhysicalDeviceFeatures::ImageCubeArray:
        return supported_features.imageCubeArray;
    case EPhysicalDeviceFeatures::IndependentBlend:
        return supported_features.independentBlend;
    case EPhysicalDeviceFeatures::GeometryShader:
        return supported_features.geometryShader;
    case EPhysicalDeviceFeatures::TessellationShader:
        return supported_features.tessellationShader;
    case EPhysicalDeviceFeatures::SampleRateShading:
        return supported_features.sampleRateShading;
    case EPhysicalDeviceFeatures::DualSrcBlend:
        return supported_features.dualSrcBlend;
    case EPhysicalDeviceFeatures::LogicOp:
        return supported_features.logicOp;
    case EPhysicalDeviceFeatures::MultiDrawIndirect:
        return supported_features.multiDrawIndirect;
    case EPhysicalDeviceFeatures::DrawIndirectFirstInstance:
        return supported_features.drawIndirectFirstInstance;
    case EPhysicalDeviceFeatures::DepthClamp:
        return supported_features.depthClamp;
    case EPhysicalDeviceFeatures::DepthBiasClamp:
        return supported_features.depthBiasClamp;
    case EPhysicalDeviceFeatures::FillModeNonSolid:
        return supported_features.fillModeNonSolid;
    case EPhysicalDeviceFeatures::DepthBounds:
        return supported_features.depthBounds;
    case EPhysicalDeviceFeatures::WideLines:
        return supported_features.wideLines;
    case EPhysicalDeviceFeatures::LargePoints:
        return supported_features.largePoints;
    case EPhysicalDeviceFeatures::AlphaToOne:
        return supported_features.alphaToOne;
    case EPhysicalDeviceFeatures::MultiViewport:
        return supported_features.multiViewport;
    case EPhysicalDeviceFeatures::SamplerAnisotropy:
        return supported_features.samplerAnisotropy;
    case EPhysicalDeviceFeatures::TextureCompressionETC2:
        return supported_features.textureCompressionETC2;
    case EPhysicalDeviceFeatures::TextureCompressionASTC_LDR:
        return supported_features.textureCompressionASTC_LDR;
    case EPhysicalDeviceFeatures::TextureCompressionBC:
        return supported_features.textureCompressionBC;
    case EPhysicalDeviceFeatures::OcclusionQueryPrecise:
        return supported_features.occlusionQueryPrecise;
    case EPhysicalDeviceFeatures::PipelineStatisticsQuery:
        return supported_features.pipelineStatisticsQuery;
    case EPhysicalDeviceFeatures::VertexPipelineStoresAndAtomics:
        return supported_features.vertexPipelineStoresAndAtomics;
    case EPhysicalDeviceFeatures::FragmentStoresAndAtomics:
        return supported_features.fragmentStoresAndAtomics;
    case EPhysicalDeviceFeatures::ShaderTessellationAndGeometryPointSize:
        return supported_features.shaderTessellationAndGeometryPointSize;
    case EPhysicalDeviceFeatures::ShaderImageGatherExtended:
        return supported_features.shaderImageGatherExtended;
    case EPhysicalDeviceFeatures::ShaderStorageImageExtendedFormats:
        return supported_features.shaderStorageImageExtendedFormats;
    case EPhysicalDeviceFeatures::ShaderStorageImageMultisample:
        return supported_features.shaderStorageImageMultisample;
    case EPhysicalDeviceFeatures::ShaderStorageImageReadWithoutFormat:
        return supported_features.shaderStorageImageReadWithoutFormat;
    case EPhysicalDeviceFeatures::ShaderStorageImageWriteWithoutFormat:
        return supported_features.shaderStorageImageWriteWithoutFormat;
    case EPhysicalDeviceFeatures::ShaderUniformBufferArrayDynamicIndexing:
        return supported_features.shaderUniformBufferArrayDynamicIndexing;
    case EPhysicalDeviceFeatures::ShaderSampledImageArrayDynamicIndexing:
        return supported_features.shaderSampledImageArrayDynamicIndexing;
    case EPhysicalDeviceFeatures::ShaderStorageBufferArrayDynamicIndexing:
        return supported_features.shaderStorageBufferArrayDynamicIndexing;
    case EPhysicalDeviceFeatures::ShaderStorageImageArrayDynamicIndexing:
        return supported_features.shaderStorageImageArrayDynamicIndexing;
    case EPhysicalDeviceFeatures::ShaderClipDistance:
        return supported_features.shaderClipDistance;
    case EPhysicalDeviceFeatures::ShaderCullDistance:
        return supported_features.shaderCullDistance;
    case EPhysicalDeviceFeatures::ShaderFloat64:
        return supported_features.shaderFloat64;
    case EPhysicalDeviceFeatures::ShaderInt64:
        return supported_features.shaderInt64;
    case EPhysicalDeviceFeatures::ShaderInt16:
        return supported_features.shaderInt16;
    case EPhysicalDeviceFeatures::ShaderResourceResidency:
        return supported_features.shaderResourceResidency;
    case EPhysicalDeviceFeatures::ShaderResourceMinLod:
        return supported_features.shaderResourceMinLod;
    case EPhysicalDeviceFeatures::SparseBinding:
        return supported_features.sparseBinding;
    case EPhysicalDeviceFeatures::SparseResidencyBuffer:
        return supported_features.sparseResidencyBuffer;
    case EPhysicalDeviceFeatures::SparseResidencyImage2D:
        return supported_features.sparseResidencyImage2D;
    case EPhysicalDeviceFeatures::SparseResidencyImage3D:
        return supported_features.sparseResidencyImage3D;
    case EPhysicalDeviceFeatures::SparseResidency2Samples:
        return supported_features.sparseResidency2Samples;
    case EPhysicalDeviceFeatures::SparseResidency4Samples:
        return supported_features.sparseResidency4Samples;
    case EPhysicalDeviceFeatures::SparseResidency8Samples:
        return supported_features.sparseResidency8Samples;
    case EPhysicalDeviceFeatures::SparseResidency16Samples:
        return supported_features.sparseResidency16Samples;
    case EPhysicalDeviceFeatures::SparseResidencyAliased:
        return supported_features.sparseResidencyAliased;
    case EPhysicalDeviceFeatures::VariableMultisampleRate:
        return supported_features.variableMultisampleRate;
    case EPhysicalDeviceFeatures::InheritedQueries:
        return supported_features.inheritedQueries;
    default:
        Logger::LogError("Unknown physical device feature: " + std::to_string(static_cast<int>(feature)));
        return false;
    }
}
