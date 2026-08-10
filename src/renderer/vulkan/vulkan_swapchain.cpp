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

    try
    {
        // wait for the device to be idle
        comm_vk_logical_device.waitIdle();
    }
    catch (const vk::SystemError& error)
    {
        Logger::LogError(std::string("Failed to wait for resize: ") + error.what());
        return false;
    }

    completed_frame = submitted_frame > 0 ? submitted_frame - 1 : 0;
    std::fill(frame_submission_ids.begin(), frame_submission_ids.end(), completed_frame);
    vk_synchronization_helper.reset();
    vk_pipeline_helper.reset();

    // destroy old vulkan objects

    for (auto image_view : comm_vk_swapchain_context.swapchain_image_views_)
    {
        comm_vk_logical_device.destroyImageView(image_view, nullptr);
    }
    comm_vk_swapchain_context.swapchain_image_views_.clear();
    comm_vk_swapchain_context.swapchain_images_.clear();
    // Note: Don't destroy swapchain images as they are owned by the swapchain
    comm_vk_logical_device.destroySwapchainKHR(comm_vk_swapchain, nullptr);
    comm_vk_swapchain = VK_NULL_HANDLE;

    // reset window size
    config.width  = width;
    config.height = height;

    // create new swapchain
    if (!create_swapchain())
    {
        Logger::LogError("Failed to recreate Vulkan swap chain");
        return false;
    }
    if (!create_pipeline())
    {
        Logger::LogError("Failed to recreate Vulkan pipeline");
        return false;
    }
    if (!create_synchronization_objects())
    {
        Logger::LogError("Failed to recreate Vulkan synchronization objects");
        return false;
    }

    resize_request = false;
    return true;
}
