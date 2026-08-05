#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <map>

struct SVulkanCommandBufferAllocationConfig
{
    vk::CommandBufferLevel command_buffer_level;
    uint32_t command_buffer_count;
};

class VulkanCommandBufferHelper
{
private:
    vk::CommandPool command_pool_;
    std::map<std::string, vk::CommandBuffer> command_buffer_map_;
    vk::Device device_;
public:
    VulkanCommandBufferHelper();
    ~VulkanCommandBufferHelper();

    vk::CommandBuffer GetCommandBuffer(std::string id) const;
    bool CreateCommandPool(vk::Device device, uint32_t queue_family_index);
    bool AllocateCommandBuffer(const SVulkanCommandBufferAllocationConfig& config, std::string id);
    bool BeginCommandBufferRecording(std::string id, vk::CommandBufferUsageFlags usage_flags);
    bool EndCommandBufferRecording(std::string id);
    bool ResetCommandBuffer(std::string id);
};