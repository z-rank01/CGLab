#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "builder.h"

typedef enum EPhysicalDeviceFeatures
{
    RobustBufferAccess = 0,
    FullDrawIndexUint32,
    ImageCubeArray,
    IndependentBlend,
    GeometryShader,
    TessellationShader,
    SampleRateShading,
    DualSrcBlend,
    LogicOp,
    MultiDrawIndirect,
    DrawIndirectFirstInstance,
    DepthClamp,
    DepthBiasClamp,
    FillModeNonSolid,
    DepthBounds,
    WideLines,
    LargePoints,
    AlphaToOne,
    MultiViewport,
    SamplerAnisotropy,
    TextureCompressionETC2,
    TextureCompressionASTC_LDR,
    TextureCompressionBC,
    OcclusionQueryPrecise,
    PipelineStatisticsQuery,
    VertexPipelineStoresAndAtomics,
    FragmentStoresAndAtomics,
    ShaderTessellationAndGeometryPointSize,
    ShaderImageGatherExtended,
    ShaderStorageImageExtendedFormats,
    ShaderStorageImageMultisample,
    ShaderStorageImageReadWithoutFormat,
    ShaderStorageImageWriteWithoutFormat,
    ShaderUniformBufferArrayDynamicIndexing,
    ShaderSampledImageArrayDynamicIndexing,
    ShaderStorageBufferArrayDynamicIndexing,
    ShaderStorageImageArrayDynamicIndexing,
    ShaderClipDistance,
    ShaderCullDistance,
    ShaderFloat64,
    ShaderInt64,
    ShaderInt16,
    ShaderResourceResidency,
    ShaderResourceMinLod,
    SparseBinding,
    SparseResidencyBuffer,
    SparseResidencyImage2D,
    SparseResidencyImage3D,
    SparseResidency2Samples,
    SparseResidency4Samples,
    SparseResidency8Samples,
    SparseResidency16Samples,
    SparseResidencyAliased,
    VariableMultisampleRate,
    InheritedQueries,
} EPhysicalDeviceFeatures;

struct SVulkanPhysicalDeviceConfig
{
    // physical device config
    // properties
    VkPhysicalDeviceType physical_device_type;
    uint8_t physical_device_api_version[4];
    // features
    std::vector<EPhysicalDeviceFeatures> physical_device_features;

    // Queue and Queue Family config
    std::vector<VkQueueFlagBits> queue_flags;
};

struct SVulkanDeviceConfig
{
    // logical device config
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    // device extensions
    int device_extension_count;
    std::vector<const char*> device_extensions;
};

class VulkanDeviceHelper
{
public:
    VulkanDeviceHelper();
    ~VulkanDeviceHelper();

    bool CreateLogicalDevice(SVulkanDeviceConfig config);
    bool CreatePhysicalDevice(SVulkanPhysicalDeviceConfig config, const VkInstance& instance);

    VkPhysicalDevice GetPhysicalDevice() const { return vkPhysicalDevice_; }
    VkDevice GetLogicalDevice() const { return vkLogicalDevice_; }
    bool WaitIdle() const;

private:
    VkPhysicalDevice vkPhysicalDevice_;
    VkDevice vkLogicalDevice_;
    VkPhysicalDeviceFeatures vkSupportedFeatures_;
    VkPhysicalDeviceProperties vkSupportedProperties_;

    VkPhysicalDevice PickPhysicalDevice(const SVulkanPhysicalDeviceConfig& config,
                                        const std::vector<VkPhysicalDevice>& instance);
};

class VulkanDeviceBuilder : public Builder
{
};