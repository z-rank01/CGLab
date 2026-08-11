#include "render_graph_vulkan/vulkan_backend_internal.h"

#include "platform/vulkan/sdl_vulkan_surface_adapter.h"

void vulkan_backend::initialize_vulkan()
{
    if (!create_render_graph_runtime())
    {
        throw std::runtime_error("Failed to create Render Graph Vulkan runtime.");
    }

    if (!create_graph_allocator_bridge())
    {
        throw std::runtime_error("Failed to connect the Render Graph allocator.");
    }

    if (!create_geometry_arena())
    {
        throw std::runtime_error("Failed to create geometry arena.");
    }

    if (!create_uniform_buffers())
    {
        throw std::runtime_error("Failed to create Vulkan uniform buffers.");
    }

    if (!create_gpu_scene_tables())
    {
        throw std::runtime_error("Failed to create GPU scene tables.");
    }

    if (!create_pipeline())
    {
        throw std::runtime_error("Failed to create Vulkan pipeline.");
    }

}

bool vulkan_backend::create_render_graph_runtime()
{
    runtime = std::make_unique<render_graph::vk_runtime>();
    const auto initialized = runtime->initialize(render_graph::vk_runtime_config{
        .application_name = config.application_name,
        .frames_in_flight = config.frame_count,
        .validation = config.use_validation_layers,
        .surface = platform::vulkan::make_sdl_surface_provider(*window),
    });
    if (!initialized)
    {
        Logger::LogError(initialized.error);
        return false;
    }
    sync_runtime_context_views();
    return true;
}

void vulkan_backend::sync_runtime_context_views()
{
    const auto& devices = runtime->devices();
    const auto& swapchain = runtime->swapchain_images();
    vma_allocator = devices.allocator;
    swapchain_image_states.reset(swapchain.rows.size());
    config.width = static_cast<int>(swapchain.extent.width);
    config.height = static_cast<int>(swapchain.extent.height);
}

bool vulkan_backend::create_graph_allocator_bridge()
{
    if (vma_allocator == VK_NULL_HANDLE)
    {
        Logger::LogError("Render Graph Vulkan runtime did not provide a VMA allocator");
        return false;
    }
    const auto graphics_family = runtime->queues().graphics.family;
    frame_graph = std::make_unique<frame_render_graph>();
    frame_graph->set_queue_availability({.compute = false, .copy = false});
    frame_graph->set_backend_context(runtime->devices().physical_device,
                                     runtime->devices().device,
                                     vma_allocator,
                                     render_graph::vk_queue_family_indices{
                                         .graphics = graphics_family,
                                         .compute = graphics_family,
                                         .copy = graphics_family,
                                     },
                                     config.frame_count);
    return true;
}
