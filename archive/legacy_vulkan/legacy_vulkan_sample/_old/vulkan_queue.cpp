#pragma once

#include "vulkan_queue.h"
#include <utility/logger.h>


VulkanQueueHelper::VulkanQueueHelper(SVulkanQueueConfig config) : queue_config_(config)
{
}

VulkanQueueHelper::~VulkanQueueHelper()
{
    // wait for all queues to finish
    for (auto& queue : queue_map_)
    {
        vkQueueWaitIdle(queue.second);
    }
}

VkQueue VulkanQueueHelper::GetQueueFromDevice(VkDevice logical_device, std::string id)
{
    if (!queue_family_index_.has_value())
    {
        Logger::LogError("Queue family index is not set");
        return VK_NULL_HANDLE;
    }

    // check if queue family index is set
    queue_index_ = 0;   // default to 0 for now
    if (queue_map_.find(id) != queue_map_.end())
    {
        Logger::LogError("Queue with ID " + id + " already exists.");
        return queue_map_[id];
    }

    // create queue
    VkQueue queue;
    vkGetDeviceQueue(logical_device, queue_family_index_.value(), queue_index_.value(), &queue);
    queue_map_[id] = queue;
    return queue;
}

bool VulkanQueueHelper::GenerateQueueCreateInfo(VkDeviceQueueCreateInfo& queue_create_info) const
{
    // check if queue family index is set
    if (!queue_family_index_.has_value())
    {
        Logger::LogError("Queue family index is not set");
        return false;
    }

    // create queue
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family_index_.value();
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = new float[1] { 1.0f };
    return true;
}

void VulkanQueueHelper::PickQueueFamily(VkPhysicalDevice physical_device, VkSurfaceKHR surface)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queueFamilyCount, queueFamilyProperties.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilyProperties[i].queueFlags & queue_config_.queue_flags)
        {
            // keep the graphic and present queue family index the same
            // so that we only need one family index for both rather than two for each
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &presentSupport);
            if (presentSupport)
            {
                queue_family_index_ = i;
                Logger::LogInfo("Queue family for both present and graphic index: " + std::to_string(i));
                break;
            }
        }
    }
}

bool VulkanQueueHelper::SubmitCommandBuffer(const SVulkanQueueSubmitConfig& config, VkDevice logical_device, VkFence fence)
{
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(config.wait_semaphores.size());
    submitInfo.pWaitSemaphores = config.wait_semaphores.data();
    submitInfo.pWaitDstStageMask = config.wait_stages.data();
    submitInfo.commandBufferCount = static_cast<uint32_t>(config.command_buffers.size());
    submitInfo.pCommandBuffers = config.command_buffers.data();
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(config.signal_semaphores.size());
    submitInfo.pSignalSemaphores = config.signal_semaphores.data();
    return Logger::LogWithVkResult(
        vkQueueSubmit(queue_map_.at(config.queue_id), 1, &submitInfo, fence),
        "Failed to submit command buffer",
        "Succeeded in submitting command buffer");
}

bool VulkanQueueHelper::PresentImage(const SVulkanQueuePresentConfig& config, VkDevice logical_device)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(config.wait_semaphores.size());
    presentInfo.pWaitSemaphores = config.wait_semaphores.data();
    presentInfo.swapchainCount = static_cast<uint32_t>(config.swapchains.size());
    presentInfo.pSwapchains = config.swapchains.data();
    presentInfo.pImageIndices = config.image_indices.data();
    return Logger::LogWithVkResult(
        vkQueuePresentKHR(queue_map_.at(config.queue_id), &presentInfo),
        "Failed to present image",
        "Succeeded in presenting image");
}

bool VulkanQueueHelper::PresentImage(const SVulkanQueuePresentConfig& config, VkDevice logical_device, bool& resize_request)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(config.wait_semaphores.size());
    presentInfo.pWaitSemaphores = config.wait_semaphores.data();
    presentInfo.swapchainCount = static_cast<uint32_t>(config.swapchains.size());
    presentInfo.pSwapchains = config.swapchains.data();
    presentInfo.pImageIndices = config.image_indices.data();
    VkResult result = vkQueuePresentKHR(queue_map_.at(config.queue_id), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        resize_request = true;
        return false;
    }
    else if (result != VK_SUCCESS)
    {
        Logger::LogError("Failed to present image: " + std::to_string(result));
        return false;
    }
    return true;
}

