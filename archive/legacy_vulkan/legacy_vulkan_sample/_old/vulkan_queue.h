#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <map>
#include <vector>
#include <string>

struct SVulkanQueueConfig
{
    VkQueueFlags queue_flags;
};

struct SVulkanQueueSubmitConfig
{
    std::string queue_id;
    std::vector<VkSemaphore> wait_semaphores;
    std::vector<VkSemaphore> signal_semaphores;
    std::vector<VkPipelineStageFlags> wait_stages;
    std::vector<VkCommandBuffer> command_buffers;
};

struct SVulkanQueuePresentConfig
{
    std::string queue_id;
    std::vector<VkSwapchainKHR> swapchains;
    std::vector<uint32_t> image_indices;
    std::vector<VkSemaphore> wait_semaphores;
};

class VulkanQueueHelper
{
public:
    VulkanQueueHelper() = delete;
    VulkanQueueHelper(SVulkanQueueConfig config);
    ~VulkanQueueHelper();

    VkQueue GetQueueFromDevice(VkDevice logical_device, std::string id);
    VkQueue GetQueue(std::string id) const { return queue_map_.at(id); }
    uint32_t GetQueueFamilyIndex() const { return queue_family_index_.value(); }

    bool GenerateQueueCreateInfo(VkDeviceQueueCreateInfo& queue_create_info) const;
    void PickQueueFamily(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
    bool SubmitCommandBuffer(const SVulkanQueueSubmitConfig& config, VkDevice logical_device, VkFence fence = VK_NULL_HANDLE);
    bool PresentImage(const SVulkanQueuePresentConfig& config, VkDevice logical_device);
    bool PresentImage(const SVulkanQueuePresentConfig& config, VkDevice logical_device, bool& resize_request);

private:
    SVulkanQueueConfig queue_config_;
    std::optional<uint32_t> queue_family_index_;
    std::optional<uint32_t> queue_index_;

    std::map<std::string, VkQueue> queue_map_;
};