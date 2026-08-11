#include "vulkan_framebuffer.h"
#include <array>
#include <utility/logger.h>

VulkanFrameBufferHelper::~VulkanFrameBufferHelper()
{
    if (!framebuffers_.empty())
    {
        for (auto framebuffer : framebuffers_)
        {
            device_.destroyFramebuffer(framebuffer);
        }
        framebuffers_.clear();
    }
}

bool VulkanFrameBufferHelper::CreateFrameBuffer(vk::RenderPass renderpass)
{
    // 创建帧缓冲
    framebuffers_.resize(config_.swapchain_image_views_.size());
    
    for (size_t i = 0; i < config_.swapchain_image_views_.size(); i++) {
        std::array<vk::ImageView, 2> attachments = {
            config_.swapchain_image_views_[i],
            config_.depth_image_view_  // 使用从配置中传入的深度图像视图
        };

        vk::FramebufferCreateInfo framebuffer_info
        {
            .renderPass = renderpass, 
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .width = config_.extent_.width,
            .height = config_.extent_.height,
            .layers = 1
        };

        if (device_.createFramebuffer(&framebuffer_info, nullptr, &framebuffers_[i]) != vk::Result::eSuccess)
        {
            Logger::LogError("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}