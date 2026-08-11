#include "renderer/vulkan/vulkan_backend_internal.h"

#include "platform/vulkan/sdl_vulkan_surface_adapter.h"

void vulkan_backend::initialize_vulkan_hpp()
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
}
void vulkan_backend::initialize_vulkan()
{
    generate_frame_structs();

    if (!create_render_graph_runtime())
    {
        throw std::runtime_error("Failed to create Render Graph Vulkan runtime.");
    }

    if (!create_vma_vra_objects())
    {
        throw std::runtime_error("Failed to create Vulkan vra and vma objects.");
    }

    if (!create_geometry_arena())
    {
        throw std::runtime_error("Failed to create geometry arena.");
    }

    create_drawcall_list_buffer();

    if (!create_uniform_buffers())
    {
        throw std::runtime_error("Failed to create Vulkan uniform buffers.");
    }

    if (!create_and_write_descriptor_relatives())
    {
        throw std::runtime_error("Failed to create Vulkan descriptor relatives.");
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
    comm_vk_instance = runtime->devices().instance;
    comm_vk_logical_device = runtime->devices().device;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(comm_vk_instance);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(comm_vk_logical_device);
    sync_runtime_context_views();
    return true;
}

void vulkan_backend::sync_runtime_context_views()
{
    const auto& devices = runtime->devices();
    const auto& queues = runtime->queues();
    const auto& swapchain = runtime->swapchain_images();
    comm_vk_instance = devices.instance;
    surface = devices.surface;
    comm_vk_physical_device = devices.physical_device;
    comm_vk_logical_device = devices.device;
    comm_vk_graphics_queue = queues.graphics.queue;
    comm_vk_transfer_queue = queues.copy.queue;
    comm_vk_swapchain = swapchain.swapchain;
    vma_allocator = devices.allocator;

    comm_vk_physical_device_context = {};
    comm_vk_physical_device_context.vk_instance_ = comm_vk_instance;
    comm_vk_physical_device_context.vk_physical_device_ = comm_vk_physical_device;
    comm_vk_physical_device_context.selection_criteria_.surface_ = surface;
    comm_vk_physical_device_context.device_properties_ = comm_vk_physical_device.getProperties();
    comm_vk_physical_device_context.device_features_ = comm_vk_physical_device.getFeatures();
    comm_vk_physical_device_context.memory_properties_ = comm_vk_physical_device.getMemoryProperties();
    comm_vk_physical_device_context.queue_family_properties_ = comm_vk_physical_device.getQueueFamilyProperties();

    comm_vk_logical_device_context = {};
    comm_vk_logical_device_context.vk_physical_device_ = comm_vk_physical_device;
    comm_vk_logical_device_context.vk_logical_device_ = comm_vk_logical_device;
    comm_vk_logical_device_context.queue_family_properties_ = comm_vk_physical_device_context.queue_family_properties_;
    comm_vk_logical_device_context.named_queues_["main_graphics"] = comm_vk_graphics_queue;
    comm_vk_logical_device_context.named_queues_["upload"] = comm_vk_transfer_queue;
    comm_vk_logical_device_context.named_queues_["compute_async"] = queues.compute.queue;
    const auto add_queue = [this](const char* name, const render_graph::vk_queue_row& row)
    {
        comm_vk_logical_device_context.queue_infos_.push_back({
            .queue_family_index_ = row.family,
            .queue_count_ = 1,
            .queue_priorities_ = {1.0F},
            .queue_flags_ = comm_vk_physical_device_context.queue_family_properties_[row.family].queueFlags,
            .queue_name_ = name,
        });
        comm_vk_logical_device_context.family_queues_[row.family] = {row.queue};
    };
    add_queue("main_graphics", queues.graphics);
    if (queues.copy.family != queues.graphics.family) add_queue("upload", queues.copy);
    if (queues.compute.family != queues.graphics.family && queues.compute.family != queues.copy.family)
        add_queue("compute_async", queues.compute);

    comm_vk_swapchain_context = {};
    comm_vk_swapchain_context.vk_logical_device_ = comm_vk_logical_device;
    comm_vk_swapchain_context.vk_physical_device_ = comm_vk_physical_device;
    comm_vk_swapchain_context.vk_surface_ = surface;
    comm_vk_swapchain_context.vk_swapchain_ = comm_vk_swapchain;
    comm_vk_swapchain_context.swapchain_info_.surface_format_ = {
        static_cast<vk::Format>(swapchain.format), static_cast<vk::ColorSpaceKHR>(swapchain.color_space)};
    comm_vk_swapchain_context.swapchain_info_.extent_ = swapchain.extent;
    comm_vk_swapchain_context.swapchain_info_.image_count_ = static_cast<uint32_t>(swapchain.rows.size());
    for (const auto& row : swapchain.rows)
    {
        comm_vk_swapchain_context.swapchain_images_.push_back(row.image);
        comm_vk_swapchain_context.swapchain_image_views_.push_back(row.view);
    }
    swapchain_image_states.reset(swapchain.rows.size());
    config.width = static_cast<int>(swapchain.extent.width);
    config.height = static_cast<int>(swapchain.extent.height);
}

bool vulkan_backend::create_instance()
{
    const interface::native_window_handle native = window->native_handle();
    if (native.kind != interface::native_window_kind::sdl3 || native.value == nullptr)
    {
        Logger::LogError("Vulkan renderer requires an SDL3 native window handle");
        return false;
    }
    uint32_t extension_count = 0;
    const char* const* extension_names = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    std::vector<const char*> extensions(extension_names, extension_names + extension_count);
    std::vector<const char*> validation_layers;
    if (config.use_validation_layers)
    {
        validation_layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    auto instance_chain = common::instance::create_context() | common::instance::set_application_name("My Vulkan App") |
                          common::instance::set_engine_name("My Engine") | common::instance::set_api_version(1, 3, 0) |
                          common::instance::add_validation_layers(validation_layers) | common::instance::add_extensions(extensions) |
                          common::instance::validate_context() | common::instance::create_vk_instance();

    auto result = instance_chain.evaluate();

    if (!callable::is_ok(result))
    {
        std::string error_msg = std::get<std::string>(result);
        std::cerr << "Failed to create Vulkan instance: " << error_msg << '\n';
        return false;
    }
    auto context      = std::get<templates::common::CommVkInstanceContext>(result);
    comm_vk_instance = context.vk_instance_;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(comm_vk_instance); // a must for loading all other function pointers!
    if (config.use_validation_layers && !create_debug_messenger())
    {
        std::cerr << "Failed to create Vulkan validation debug messenger.\n";
        return false;
    }
    std::cout << "Successfully created Vulkan instance." << '\n';
    return true;
}

bool vulkan_backend::create_debug_messenger()
{
    const auto create_messenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(comm_vk_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create_messenger == nullptr)
    {
        return false;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = &vulkan_backend::validation_callback;
    create_info.pUserData = validation_errors.get();
    return create_messenger(comm_vk_instance, &create_info, nullptr, &debug_messenger) == VK_SUCCESS;
}

void vulkan_backend::destroy_debug_messenger() noexcept
{
    if (comm_vk_instance == VK_NULL_HANDLE || debug_messenger == VK_NULL_HANDLE)
    {
        return;
    }
    const auto destroy_messenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(comm_vk_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy_messenger != nullptr)
    {
        destroy_messenger(comm_vk_instance, debug_messenger, nullptr);
    }
    debug_messenger = VK_NULL_HANDLE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_backend::validation_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    auto* errors = static_cast<std::atomic_uint32_t*>(user_data);
    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 && errors != nullptr)
    {
        errors->fetch_add(1, std::memory_order_relaxed);
    }
    if (callback_data != nullptr && callback_data->pMessage != nullptr)
    {
        std::cerr << "[Vulkan Validation] " << callback_data->pMessage << '\n';
    }
    return VK_FALSE;
}

bool vulkan_backend::create_surface()
{
    const interface::native_window_handle native = window->native_handle();
    if (native.kind != interface::native_window_kind::sdl3 || native.value == nullptr)
    {
        return false;
    }
    return SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(native.value), comm_vk_instance, nullptr, &surface);
}

bool vulkan_backend::create_physical_device()
{
    // vulkan 1.3 features - 用于检查硬件支持
    vk::PhysicalDeviceVulkan13Features features_13{.synchronization2 = vk::True, .dynamicRendering = vk::True};

    auto physical_device_chain = common::physicaldevice::create_physical_device_context(comm_vk_instance) |
                                 common::physicaldevice::set_surface(surface) | common::physicaldevice::require_api_version(1, 3, 0) |
                                 common::physicaldevice::require_features_13(features_13) |
                                 common::physicaldevice::require_queue(vk::QueueFlagBits::eGraphics, 1, true) |
                                 common::physicaldevice::prefer_discrete_gpu() | common::physicaldevice::select_physical_device();

    auto result = physical_device_chain.evaluate();

    if (!callable::is_ok(result))
    {
        std::string error_msg = std::get<std::string>(result);
        std::cerr << "Failed to create Vulkan physical device: " << error_msg << '\n';
        return false;
    }

    comm_vk_physical_device_context = std::get<templates::common::CommVkPhysicalDeviceContext>(result);
    comm_vk_physical_device         = comm_vk_physical_device_context.vk_physical_device_;
    std::cout << "Successfully created Vulkan physical device." << '\n';
    return true;
}

bool vulkan_backend::create_logical_device()
{
    auto device_chain = common::logicaldevice::create_logical_device_context(comm_vk_physical_device_context) |
                        common::logicaldevice::require_extensions({vk::KHRSwapchainExtensionName}) |
                        common::logicaldevice::add_graphics_queue("main_graphics", surface) | common::logicaldevice::add_transfer_queue("upload") |
                        common::logicaldevice::add_compute_queue("compute_async") | common::logicaldevice::validate_device_configuration() |
                        common::logicaldevice::create_logical_device();

    auto result = device_chain.evaluate();
    if (!callable::is_ok(result))
    {
        std::string error_msg = std::get<std::string>(result);
        std::cerr << "Failed to create Vulkan logical device: " << error_msg << '\n';
        return false;
    }
    comm_vk_logical_device_context = std::get<common::CommVkLogicalDeviceContext>(result);
    comm_vk_logical_device         = comm_vk_logical_device_context.vk_logical_device_;
    comm_vk_graphics_queue         = common::logicaldevice::get_queue(comm_vk_logical_device_context, "main_graphics");
    comm_vk_transfer_queue         = common::logicaldevice::get_queue(comm_vk_logical_device_context, "upload");
    std::cout << "Successfully created Vulkan logical device." << '\n';
    return true;
}

bool vulkan_backend::create_vma_vra_objects()
{
    // vra and vma members
    vra_data_batcher = std::make_unique<vra::VraDataBatcher>(comm_vk_physical_device);

    if (vma_allocator == VK_NULL_HANDLE)
    {
        Logger::LogError("Render Graph Vulkan runtime did not provide a VMA allocator");
        return false;
    }
    const auto graphics_family = common::logicaldevice::find_optimal_queue_family(
        comm_vk_logical_device_context, vk::QueueFlagBits::eGraphics).value_or(0);
    frame_graph = std::make_unique<frame_render_graph>();
    frame_graph->set_queue_availability({.compute = false, .copy = false});
    frame_graph->set_backend_context(static_cast<VkPhysicalDevice>(comm_vk_physical_device),
                                     static_cast<VkDevice>(comm_vk_logical_device),
                                     vma_allocator,
                                     render_graph::vk_queue_family_indices{
                                         .graphics = graphics_family,
                                         .compute = graphics_family,
                                         .copy = graphics_family,
                                     },
                                     config.frame_count);
    return true;
}
