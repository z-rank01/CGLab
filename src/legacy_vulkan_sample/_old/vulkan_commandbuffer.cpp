#include "vulkan_commandbuffer.h"
#include "utility/logger.h"

VulkanCommandBufferHelper::VulkanCommandBufferHelper()
{
}

VulkanCommandBufferHelper::~VulkanCommandBufferHelper()
{
    // destroy command buffers
    for (auto command_buffer : command_buffer_map_)
    {
        device_.freeCommandBuffers(command_pool_, 1, &command_buffer.second);
    }
    command_buffer_map_.clear();

    // destroy command pool
    if (command_pool_ != VK_NULL_HANDLE)
    {
        device_.destroyCommandPool(command_pool_);
        command_pool_ = VK_NULL_HANDLE;
    }
}

vk::CommandBuffer VulkanCommandBufferHelper::GetCommandBuffer(std::string id) const
{
    if (command_buffer_map_.find(id) != command_buffer_map_.end())
    {
        return command_buffer_map_.at(id);
    }
    return VK_NULL_HANDLE;
}

bool VulkanCommandBufferHelper::CreateCommandPool(vk::Device device, uint32_t queue_family_index)
{
    device_ = device;

    // create command pool
    vk::CommandPoolCreateInfo pool_info
    {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queue_family_index,
    };

    if (device_.createCommandPool(&pool_info, nullptr, &command_pool_) != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to create command pool");
        return false;
    }
    Logger::LogInfo("Command pool created successfully");
    return true;
}

bool VulkanCommandBufferHelper::AllocateCommandBuffer(const SVulkanCommandBufferAllocationConfig& config, std::string id)
{
    // check if command buffer already exists
    if (command_buffer_map_.find(id) != command_buffer_map_.end())
    {
        Logger::LogError("Command buffer with ID " + id + " already exists.");
        return false;
    }

    // allocate command buffer
    vk::CommandBufferAllocateInfo alloc_info
    {
        .commandPool = command_pool_,
        .level = config.command_buffer_level,
        .commandBufferCount = config.command_buffer_count
    };

    vk::CommandBuffer command_buffer;
    if (device_.allocateCommandBuffers(&alloc_info, &command_buffer) != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to allocate command buffer " + id);
        return false;
    }
    Logger::LogInfo("Command buffer " + id + " allocated successfully");
    command_buffer_map_[id] = command_buffer;
    return true;
}

bool VulkanCommandBufferHelper::BeginCommandBufferRecording(std::string id, vk::CommandBufferUsageFlags usage_flags)
{
    // begin command buffer recording
    vk::CommandBufferBeginInfo begin_info
    {
        .flags = usage_flags, 
        .pInheritanceInfo = nullptr
    };

    if (command_buffer_map_[id].begin(&begin_info) != vk::Result::eSuccess)
    {
        Logger::LogInfo("Failed to begin command buffer recording");
        return false;
    }
    // Logger::LogInfo("Succeeded in beginning command buffer recording");
    return true;
}

bool VulkanCommandBufferHelper::EndCommandBufferRecording(std::string id)
{
    command_buffer_map_[id].end();
    // Logger::LogInfo("Succeeded in ending command buffer recording");
    return true;
}

bool VulkanCommandBufferHelper::ResetCommandBuffer(std::string id)
{
    // reset command buffer
    command_buffer_map_.at(id).reset();
    // Logger::LogInfo("Succeeded in resetting command buffer");
    return true;
}
