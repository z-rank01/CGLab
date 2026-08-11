#include "renderer/vulkan/vulkan_backend_internal.h"

bool vulkan_backend::create_swapchain()
{
    // create swapchain

    auto swapchain_chain = common::swapchain::create_swapchain_context(comm_vk_logical_device_context, surface) |
                           common::swapchain::set_surface_format(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear) |
                           common::swapchain::set_present_mode(vk::PresentModeKHR::eFifo) | common::swapchain::set_image_count(2, 3) |
                           common::swapchain::set_desired_extent(static_cast<uint32_t>(config.width),
                                                                 static_cast<uint32_t>(config.height)) |
                           common::swapchain::query_surface_support() | common::swapchain::select_swapchain_settings() |
                           common::swapchain::create_swapchain();

    auto result = swapchain_chain.evaluate();
    if (!callable::is_ok(result))
    {
        std::string error_msg = std::get<std::string>(result);
        std::cerr << "Failed to create Vulkan swapchain: " << error_msg << '\n';
        return false;
    }
    std::cout << "Successfully created Vulkan swapchain." << '\n';
    auto tmp_swapchain_ctx = std::get<common::CommVkSwapchainContext>(result);

    // create swapchain images and image views

    auto swapchain_image_related_chain =
        callable::make_chain(std::move(tmp_swapchain_ctx)) | common::swapchain::get_swapchain_images() | common::swapchain::create_image_views();
    auto swapchain_image_result = swapchain_image_related_chain.evaluate();
    if (!callable::is_ok(swapchain_image_result))
    {
        std::string error_msg = std::get<std::string>(swapchain_image_result);
        std::cerr << "Failed to create Vulkan swapchain image views: " << error_msg << '\n';
        return false;
    }
    std::cout << "Successfully created Vulkan swapchain image views." << '\n';

    // get final swapchain context
    comm_vk_swapchain_context = std::get<common::CommVkSwapchainContext>(swapchain_image_result);
    comm_vk_swapchain         = comm_vk_swapchain_context.vk_swapchain_;
    swapchain_image_states.reset(comm_vk_swapchain_context.swapchain_images_.size());

    return true;
}
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
    vk_pipeline_helper.reset();
    comm_vk_swapchain_context.swapchain_image_views_.clear();
    comm_vk_swapchain_context.swapchain_images_.clear();
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
