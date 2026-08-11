#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <map>

class VulkanSynchronizationHelper
{
private:
    vk::Device device_;
    std::map<std::string, vk::Semaphore> semaphores_;
    std::map<std::string, vk::Fence> fences_;
public:
    VulkanSynchronizationHelper(vk::Device device) : device_(device) {}
    ~VulkanSynchronizationHelper();

    bool CreateVkSemaphore(std::string id);
    bool CreateFence(std::string id);

    bool WaitForFence(std::string id);
    bool ResetFence(std::string id);

    vk::Semaphore GetSemaphore(std::string id) const;
    vk::Fence GetFence(std::string id) const;
};
