#include "vulkan_synchronization.h"
#include "utility/logger.h"


VulkanSynchronizationHelper::~VulkanSynchronizationHelper()
{
    for (auto& semaphore : semaphores_)
    {
        device_.destroySemaphore(semaphore.second);
    }
    semaphores_.clear();

    for (auto& fence : fences_)
    {
        device_.destroyFence(fence.second);
    }
    fences_.clear();
}

bool VulkanSynchronizationHelper::CreateVkSemaphore(std::string id)
{
    vk::SemaphoreCreateInfo semaphore_info{};

    if (semaphores_.find(id) != semaphores_.end())
    {
        Logger::LogError("Semaphore with ID " + id + " already exists.");
        return semaphores_[id];
    }
    vk::Semaphore semaphore = device_.createSemaphore(semaphore_info);
    if (!semaphore)
    {
        Logger::LogError("Failed to create semaphore " + id);
        return false;
    }
    semaphores_[id] = semaphore;
    return true;
}

bool VulkanSynchronizationHelper::CreateFence(std::string id)
{
    vk::FenceCreateInfo fence_info
    {
        .flags = vk::FenceCreateFlagBits::eSignaled
    };

    if (fences_.find(id) != fences_.end())
    {
        Logger::LogError("Fence with ID " + id + " already exists.");
        return fences_[id];
    }
    vk::Fence fence = device_.createFence(fence_info);
    if (!fence)
    {
        Logger::LogError("Failed to create fence " + id);
        return false;
    }
    fences_[id] = fence;
    return true;
}

bool VulkanSynchronizationHelper::WaitForFence(std::string id)
{
    if (device_.waitForFences(1, &fences_[id], VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to wait for fence " + id);
        return false;
    }
    // Logger::LogInfo("Successfully waited for fence " + id);
    return true;
}

bool VulkanSynchronizationHelper::ResetFence(std::string id)
{
    if (device_.resetFences(1, &fences_[id]) != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to reset fence " + id);
        return false;
    }
    // Logger::LogInfo("Succeeded in resetting fence " + id);
    return true;
}

vk::Semaphore VulkanSynchronizationHelper::GetSemaphore(std::string id) const
{
    if (semaphores_.find(id) != semaphores_.end())
    {
        return semaphores_.at(id);
    }
    return VK_NULL_HANDLE;
}

vk::Fence VulkanSynchronizationHelper::GetFence(std::string id) const
{
    if (fences_.find(id) != fences_.end())
    {
        return fences_.at(id);
    }
    return VK_NULL_HANDLE;
}
