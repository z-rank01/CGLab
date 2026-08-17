#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>


struct SVulkanRenderpassConfig
{
    vk::Format color_format;
    vk::Format depth_format;
    vk::SampleCountFlagBits sample_count;
};

class VulkanRenderpassHelper
{
private:
    SVulkanRenderpassConfig config_;
    vk::RenderPass renderpass_;
    vk::Device device_;
public:
    VulkanRenderpassHelper() = delete;
    VulkanRenderpassHelper(SVulkanRenderpassConfig config) : config_(config) {};
    ~VulkanRenderpassHelper();

    bool CreateRenderpass(vk::Device device);
    vk::RenderPass GetRenderpass() const { return renderpass_; }
};
