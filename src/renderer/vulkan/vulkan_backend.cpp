#include "renderer/vulkan/vulkan_backend_internal.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

vulkan_backend::vulkan_backend(engine::vulkan::render_program program) : render_program(std::move(program))
{
}
engine::result<bool> vulkan_backend::initialize(interface::window& render_window,
                                                const engine::backend_config& backend_config)
{
    engine::result<bool> result;
    try
    {
        int width = 0;
        int height = 0;
        render_window.get_extent(width, height);
        config.width = width;
        config.height = height;
        config.application_name = backend_config.application_name;
        config.working_directory = backend_config.working_directory;
        config.frame_count = backend_config.frames_in_flight;
        config.use_validation_layers = backend_config.validation;
        window = &render_window;
        // Keep a valid imported legacy buffer while the runtime path routes all
        // real content through the geometry arena. No legacy draw is emitted.
        vertices.assign(1, engine::vertex{});
        indices.assign(1, 0);
        mesh_upload_pending = false;
        initialize();
        result.value = true;
    }
    catch (const std::exception& error)
    {
        shutdown();
        result.error = error.what();
    }
    return result;
}

engine::result<engine::geometry_handle> vulkan_backend::upload_geometry(const engine::geometry_asset& asset)
{
    engine::result<engine::geometry_handle> result;
    if (asset.empty())
    {
        result.error = "Cannot upload an empty geometry asset";
        return result;
    }

    staged_geometry allocation;
    if (!stage_runtime_geometry(asset.primitives, allocation))
    {
        result.error = "Geometry arena staging failed";
        return result;
    }
    const engine::geometry_handle handle = next_geometry_handle++;
    geometry_allocations.emplace(handle, std::move(allocation));
    result.value = handle;
    return result;
}

void vulkan_backend::retire_geometry(engine::geometry_handle handle)
{
    const auto found = geometry_allocations.find(handle);
    if (found == geometry_allocations.end())
    {
        return;
    }
    retire_runtime_geometry(std::move(found->second));
    geometry_allocations.erase(found);
}

engine::frame_status vulkan_backend::render(const engine::render_snapshot& snapshot)
{
    current_snapshot = &snapshot;
    const engine::frame_status status = tick();
    current_snapshot = nullptr;
    return status;
}

void vulkan_backend::shutdown() noexcept
{
    if (shutdown_requested)
    {
        return;
    }
    shutdown_requested = true;
    if (comm_vk_logical_device)
    {
        try
        {
            (void)comm_vk_logical_device.waitIdle();
        }
        catch (...)
        {
        }
    }
}

void vulkan_backend::initialize()
{
    // initialize mvp matrices
    mvp_matrices =
        std::vector<mvp_matrix>(config.frame_count, {.model = glm::mat4(1.0F), .view = glm::mat4(1.0F), .projection = glm::mat4(1.0F)});
    frame_submission_ids.assign(config.frame_count, 0);

    // initialize SDL, vulkan, and camera
    initialize_vulkan_hpp();
    initialize_vulkan();
}

vulkan_backend::~vulkan_backend()
{
    // 等待设备空闲，确保没有正在进行的操作
    if (comm_vk_logical_device)
    {
        try
        {
            (void)comm_vk_logical_device.waitIdle();
        }
        catch (const vk::SystemError& error)
        {
            Logger::LogError(std::string("Failed to wait for Vulkan shutdown: ") + error.what());
        }
    }

    // 销毁描述符相关资源
    if (descriptor_pool != VK_NULL_HANDLE)
    {
        comm_vk_logical_device.destroyDescriptorPool(descriptor_pool);
        descriptor_pool = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout != VK_NULL_HANDLE)
    {
        comm_vk_logical_device.destroyDescriptorSetLayout(descriptor_set_layout);
        descriptor_set_layout = VK_NULL_HANDLE;
    }

    // destroy vma relatives
    if (vma_allocator != VK_NULL_HANDLE && uniform_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, uniform_buffer, uniform_buffer_allocation);
        uniform_buffer = VK_NULL_HANDLE;
    }
    if (vma_allocator != VK_NULL_HANDLE && local_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, local_buffer, local_buffer_allocation);
        local_buffer = VK_NULL_HANDLE;
    }
    if (vma_allocator != VK_NULL_HANDLE && staging_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_buffer_allocation);
        staging_buffer = VK_NULL_HANDLE;
    }
    // P2：销毁运行时上传批次与 geometry arena
    for (runtime_upload& upload : queued_uploads)
    {
        destroy_runtime_upload(vma_allocator, upload);
    }
    queued_uploads.clear();
    for (auto& [gate, upload] : in_flight_uploads)
    {
        destroy_runtime_upload(vma_allocator, upload);
    }
    in_flight_uploads.clear();
    if (vma_allocator != VK_NULL_HANDLE && arena_vertex_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, arena_vertex_buffer, arena_vertex_allocation);
        arena_vertex_buffer = VK_NULL_HANDLE;
    }
    if (vma_allocator != VK_NULL_HANDLE && arena_index_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, arena_index_buffer, arena_index_allocation);
        arena_index_buffer = VK_NULL_HANDLE;
    }
    frame_graph.reset();
    if (vma_allocator != VK_NULL_HANDLE && !runtime)
    {
        vmaDestroyAllocator(vma_allocator);
        vma_allocator = VK_NULL_HANDLE;
    }

    // destroy swapchain related resources

    for (auto image_view : runtime ? std::vector<vk::ImageView>{} : comm_vk_swapchain_context.swapchain_image_views_)
    {
        if (comm_vk_logical_device)
        {
            comm_vk_logical_device.destroyImageView(image_view);
        }
    }
    if (!runtime && comm_vk_logical_device && comm_vk_swapchain)
    {
        comm_vk_logical_device.destroySwapchainKHR(comm_vk_swapchain);
    }

    // release unique pointer

    vk_shader_helper.reset();
    vk_pipeline_helper.reset();
    vk_command_buffer_helper.reset();
    vk_synchronization_helper.reset();

    // destroy comm test data

    if (!runtime && comm_vk_logical_device)
    {
        vkDestroyDevice(comm_vk_logical_device, nullptr);
    }
    if (!runtime && comm_vk_instance && surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(comm_vk_instance, surface, nullptr);
    }
    if (!runtime)
    {
        destroy_debug_messenger();
    }
    if (!runtime && comm_vk_instance)
    {
        vkDestroyInstance(comm_vk_instance, nullptr);
    }
    runtime.reset();
}

vulkan_frame_status vulkan_backend::tick()
{
    int current_width = 0;
    int current_height = 0;
    window->get_extent(current_width, current_height);
    if (current_width == 0 || current_height == 0)
    {
        return vulkan_frame_status::skipped;
    }
    if (current_width != config.width || current_height != config.height)
    {
        resize_request = true;
    }
    if (resize_request)
    {
        if (!resize_swapchain())
        {
            return vulkan_frame_status::failed;
        }
        if (window->should_close())
        {
            return vulkan_frame_status::skipped;
        }
    }

    // update the view matrix
    update_uniform_buffer(frame_index);

    // render a frame
    return draw();
}

vulkan_frame_status vulkan_backend::draw()
{
    return draw_frame();
}

vulkan_frame_status vulkan_backend::draw_frame()
{
    render_graph::vk_frame_token token;
    const auto acquired = runtime->acquire(token);
    if (acquired == render_graph::vk_frame_status::skipped)
    {
        resize_request = true;
        return vulkan_frame_status::skipped;
    }
    if (acquired == render_graph::vk_frame_status::failed)
    {
        Logger::LogError(runtime->last_error());
        return vulkan_frame_status::failed;
    }
    frame_index = static_cast<uint8_t>(token.frame_index);
    completed_frame = runtime->frames().completed_submission;
    submitted_frame = runtime->frames().next_submission;
    collect_deferred_resources();

    if (!runtime->realize_resources() ||
        !runtime->record_batches(token,
                                 this,
                                 [](void* state, VkCommandBuffer commands, uint32_t image_index)
                                 {
                                     return static_cast<vulkan_backend*>(state)->record_command(image_index, commands);
                                 }))
    {
        frame_graph->abort_frame();
        Logger::LogError(runtime->last_error());
        return vulkan_frame_status::failed;
    }
    if (!runtime->submit(token))
    {
        Logger::LogError(runtime->last_error());
        frame_graph->abort_frame();
        return vulkan_frame_status::failed;
    }
    submitted_frame = runtime->frames().next_submission;
    if (!frame_graph->commit_frame())
    {
        Logger::LogError("Failed to commit render graph frame");
        return vulkan_frame_status::failed;
    }
    mesh_upload_pending = false;
    if (runtime_upload_pending)
    {
        for (runtime_upload& upload : queued_uploads)
        {
            in_flight_uploads.emplace_back(submitted_frame - 1, std::move(upload));
        }
        queued_uploads.clear();
        runtime_upload_pending = false;
    }

    const auto presented = runtime->present(token);
    if (presented == render_graph::vk_frame_status::skipped)
    {
        resize_request = true;
        return vulkan_frame_status::skipped;
    }
    if (presented == render_graph::vk_frame_status::failed)
    {
        Logger::LogError(runtime->last_error());
        return vulkan_frame_status::failed;
    }
    swapchain_image_states.mark_presented(token.image_index);
    ++run_statistics.presented_frames;
    frame_index = static_cast<uint8_t>(runtime->frames().cursor);
    return vulkan_frame_status::rendered;
}
