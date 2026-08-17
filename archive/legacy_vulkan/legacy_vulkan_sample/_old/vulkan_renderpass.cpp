#include "vulkan_renderpass.h"
#include "utility/logger.h"
#include <array>

VulkanRenderpassHelper::~VulkanRenderpassHelper()
{
    if (renderpass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderpass_, nullptr);
        renderpass_ = VK_NULL_HANDLE;
    }
}

bool VulkanRenderpassHelper::CreateRenderpass(vk::Device device)
{
    device_ = device;

    // Color attachment
    vk::AttachmentDescription color_attachment
    {
        .format = config_.color_format,
        .samples = config_.sample_count,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR
    };

    // depth attachment
    vk::AttachmentDescription depth_attachment
    {
        .format = config_.depth_format,
        .samples = config_.sample_count,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    // 颜色附件引用
    vk::AttachmentReference color_attachment_ref
    {
        .attachment=0, 
        .layout=vk::ImageLayout::eColorAttachmentOptimal
    };

    // 深度附件引用
    vk::AttachmentReference depth_attachment_ref
    {
        .attachment=1, 
        .layout=vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    // 子通道描述
    vk::SubpassDescription subpass
    {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pDepthStencilAttachment = &depth_attachment_ref
    };

    // 子通道依赖
    vk::SubpassDependency dependency
    {
        .srcSubpass = vk::SubpassExternal,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        .srcAccessMask = vk::AccessFlagBits::eNone,
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite
    };

    // 组合附件
    std::array<vk::AttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    // 创建渲染通道
    vk::RenderPassCreateInfo renderpass_info
    {
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    // create render pass
    renderpass_ = device_.createRenderPass(renderpass_info, nullptr);
    if (!renderpass_)
    {
        Logger::LogError("Failed to create render pass");
        return false;
    }
    Logger::LogInfo("Succeeded in creating render pass");
    return true;
}
