#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vector>

// 帧缓冲配置结构体
struct SVulkanFrameBufferConfig
{
    vk::Extent2D extent_;
    std::vector<vk::ImageView> swapchain_image_views_;
    vk::ImageView depth_image_view_ = VK_NULL_HANDLE; // 添加深度图像视图字段

    SVulkanFrameBufferConfig() = default;
    SVulkanFrameBufferConfig(vk::Extent2D extent, const std::vector<vk::ImageView> swapchain_image_views, vk::ImageView depth_image_view = VK_NULL_HANDLE)
        : extent_(extent), swapchain_image_views_(swapchain_image_views), depth_image_view_(depth_image_view) {}
};

class VulkanFrameBufferHelper
{
public:
    VulkanFrameBufferHelper() = delete;
    VulkanFrameBufferHelper(vk::Device device, const SVulkanFrameBufferConfig& config)
        : device_(device), config_(config) {}
    ~VulkanFrameBufferHelper();

    bool CreateFrameBuffer(vk::RenderPass renderpass);
    [[nodiscard]] constexpr auto GetFramebuffers() const -> const std::vector<vk::Framebuffer>* { return &framebuffers_; }

private:
    vk::Device device_;
    SVulkanFrameBufferConfig config_;
    std::vector<vk::Framebuffer> framebuffers_;
};
