#include "render_graph_vulkan/vulkan_backend_internal.h"

bool vulkan_backend::resize_swapchain()
{
    int width = 0;
    int height = 0;
    window->get_extent(width, height);
    if (width == 0 || height == 0 || window->should_close())
    {
        return true;
    }

    completed_frame = runtime->frames().next_submission > 0 ? runtime->frames().next_submission - 1 : 0;
    const auto resized = runtime->resize();
    if (!resized)
    {
        Logger::LogError("Failed to recreate Vulkan swapchain: " + resized.error);
        return false;
    }
    sync_runtime_context_views();
    if (!create_pipeline())
    {
        Logger::LogError("Failed to recreate Vulkan pipeline");
        return false;
    }
    resize_request = false;
    return true;
}
