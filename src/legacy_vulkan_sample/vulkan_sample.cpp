#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS

#include "vulkan_sample.h"

#include <cstdint>
#include <iostream>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>

#include "_callable/callable.h"
#include "_interface/camera_system.h"
#include "_templates/common.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

using namespace templates;

static vulkan_sample* instance = nullptr;

vulkan_sample::vulkan_sample(engine_config in_config) : config(std::move(in_config))
{
    // only one engine initialization is allowed with the application.
    assert(instance == nullptr);
    instance = this;
}

void vulkan_sample::initialize()
{
    // initialize mvp matrices
    mvp_matrices =
        std::vector<mvp_matrix>(config.frame_count, {.model = glm::mat4(1.0F), .view = glm::mat4(1.0F), .projection = glm::mat4(1.0F)});

    // initialize SDL, vulkan, and camera
    initialize_vulkan_hpp();
    initialize_vulkan();
}

void vulkan_sample::set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data,
                                      std::vector<uint32_t> all_indices,
                                      std::vector<gltf::Vertex> all_vertices)
{
    per_draw_call_data_list = std::move(per_draw_call_data);
    indices                 = std::move(all_indices);
    vertices                = std::move(all_vertices);
}

void vulkan_sample::set_mesh_list(const std::vector<gltf::PerMeshData>& all_mesh_list)
{
    this->mesh_list = all_mesh_list;
}

vulkan_sample::~vulkan_sample()
{
    // 等待设备空闲，确保没有正在进行的操作
    comm_vk_logical_device.waitIdle();

    // 销毁深度资源
    if (depth_image_view != VK_NULL_HANDLE)
    {
        comm_vk_logical_device.destroyImageView(depth_image_view);
        depth_image_view = VK_NULL_HANDLE;
    }

    if (depth_image != VK_NULL_HANDLE)
    {
        comm_vk_logical_device.destroyImage(depth_image);
        depth_image = VK_NULL_HANDLE;
    }

    if (depth_memory != VK_NULL_HANDLE)
    {
        comm_vk_logical_device.freeMemory(depth_memory, nullptr);
        depth_memory = VK_NULL_HANDLE;
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
    if (uniform_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, uniform_buffer, uniform_buffer_allocation);
        uniform_buffer = VK_NULL_HANDLE;
    }
    if (local_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, local_buffer, local_buffer_allocation);
        local_buffer = VK_NULL_HANDLE;
    }
    if (staging_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(vma_allocator, staging_buffer, staging_buffer_allocation);
        staging_buffer = VK_NULL_HANDLE;
    }
    if (vma_allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(vma_allocator);
        vma_allocator = VK_NULL_HANDLE;
    }

    // destroy swapchain related resources

    for (auto image_view : comm_vk_swapchain_context.swapchain_image_views_)
    {
        comm_vk_logical_device.destroyImageView(image_view);
    }
    comm_vk_logical_device.destroySwapchainKHR(comm_vk_swapchain);

    // release unique pointer

    vk_shader_helper.reset();
    vk_renderpass_helper.reset();
    vk_pipeline_helper.reset();
    vk_frame_buffer_helper.reset();
    vk_command_buffer_helper.reset();
    vk_synchronization_helper.reset();

    // destroy comm test data

    vkDestroyDevice(comm_vk_logical_device, nullptr);
    vkDestroySurfaceKHR(comm_vk_instance, surface, nullptr);
    vkDestroyInstance(comm_vk_instance, nullptr);

    // 重置单例指针
    instance = nullptr;
}

void vulkan_sample::initialize_vulkan_hpp()
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
}

void vulkan_sample::initialize_vulkan()
{
    generate_frame_structs();

    if (!create_instance())
    {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }

    if (!create_surface())
    {
        throw std::runtime_error("Failed to create Vulkan surface.");
    }

    if (!create_physical_device())
    {
        throw std::runtime_error("Failed to create Vulkan physical device.");
    }

    if (!create_logical_device())
    {
        throw std::runtime_error("Failed to create Vulkan logical device.");
    }

    if (!create_swapchain())
    {
        throw std::runtime_error("Failed to create Vulkan swap chain.");
    }

    if (!create_vma_vra_objects())
    {
        throw std::runtime_error("Failed to create Vulkan vra and vma objects.");
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

    if (!create_frame_buffer())
    {
        throw std::runtime_error("Failed to create Vulkan frame buffer.");
    }

    if (!create_command_pool())
    {
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }

    if (!allocate_per_frame_command_buffer())
    {
        throw std::runtime_error("Failed to allocate Vulkan command buffer.");
    }

    if (!create_synchronization_objects())
    {
        throw std::runtime_error("Failed to create Vulkan synchronization objects.");
    }
}

void vulkan_sample::tick()
{
    if (resize_request)
    {
        resize_swapchain();
    }

    // update the view matrix
    update_uniform_buffer(frame_index);

    // render a frame
    draw();
}

// Main render loop
void vulkan_sample::draw()
{
    draw_frame();
}

// -------------------------------------
// private function to create the engine
// -------------------------------------

void vulkan_sample::generate_frame_structs()
{
    output_frames.resize(config.frame_count);
    for (int i = 0; i < config.frame_count; ++i)
    {
        output_frames[i].image_index                  = i;
        output_frames[i].queue_id                     = "graphic_queue";
        output_frames[i].command_buffer_id            = "graphic_command_buffer_" + std::to_string(i);
        output_frames[i].image_available_semaphore_id = "image_available_semaphore_" + std::to_string(i);
        output_frames[i].render_finished_semaphore_id = "render_finished_semaphore_" + std::to_string(i);
        output_frames[i].fence_id                     = "in_flight_fence_" + std::to_string(i);
    }
}

bool vulkan_sample::create_instance()
{
    auto extensions     = window->get_required_instance_extensions();
    auto instance_chain = common::instance::create_context() | common::instance::set_application_name("My Vulkan App") |
                          common::instance::set_engine_name("My Engine") | common::instance::set_api_version(1, 3, 0) |
                          common::instance::add_validation_layers({"VK_LAYER_KHRONOS_validation"}) | common::instance::add_extensions(extensions) |
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
    std::cout << "Successfully created Vulkan instance." << '\n';
    return true;
}

bool vulkan_sample::create_surface()
{
    return window->create_vulkan_surface(comm_vk_instance, &surface);
}

bool vulkan_sample::create_physical_device()
{
    // vulkan 1.3 features - 用于检查硬件支持
    vk::PhysicalDeviceVulkan13Features features_13{.synchronization2 = vk::True};

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

bool vulkan_sample::create_logical_device()
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

bool vulkan_sample::create_swapchain()
{
    // create swapchain

    auto swapchain_chain = common::swapchain::create_swapchain_context(comm_vk_logical_device_context, surface) |
                           common::swapchain::set_surface_format(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear) |
                           common::swapchain::set_present_mode(vk::PresentModeKHR::eFifo) | common::swapchain::set_image_count(2, 3) |
                           common::swapchain::set_desired_extent(static_cast<uint32_t>(config.window_config.width),
                                                                 static_cast<uint32_t>(config.window_config.height)) |
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

    return true;
}

bool vulkan_sample::create_command_pool()
{
    vk_command_buffer_helper = std::make_unique<VulkanCommandBufferHelper>();

    auto queue_family_index = common::logicaldevice::find_optimal_queue_family(comm_vk_logical_device_context, vk::QueueFlagBits::eGraphics);
    if (!queue_family_index.has_value())
    {
        std::cerr << "Failed to find any suitable graphics queue family." << '\n';
        return false;
    }
    return vk_command_buffer_helper->CreateCommandPool(comm_vk_logical_device, queue_family_index.value());
}

bool vulkan_sample::create_uniform_buffers()
{
    for (int i = 0; i < config.frame_count; ++i)
    {
        auto current_mvp_matrix = mvp_matrices[i];
        vk::BufferCreateInfo buffer_create_info;
        buffer_create_info.setSize(sizeof(mvp_matrix)).setUsage(vk::BufferUsageFlagBits::eUniformBuffer).setSharingMode(vk::SharingMode::eExclusive);
        vra::VraDataDesc data_desc{vra::VraDataMemoryPattern::CPU_GPU, vra::VraDataUpdateRate::Frequent, buffer_create_info};
        vra::VraRawData raw_data{.pData_ = &current_mvp_matrix, .size_ = sizeof(mvp_matrix)};
        uniform_buffer_id.push_back(0);
        vra_data_batcher->Collect(data_desc, raw_data, uniform_buffer_id.back());
    }

    uniform_batch_handle = vra_data_batcher->Batch();

    // get buffer create info
    const auto& uniform_buffer_create_info = uniform_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Frequently].data_desc.GetBufferCreateInfo();

    VmaAllocationCreateInfo allocation_create_info = {};
    allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocation_create_info.flags = vra_data_batcher->GetSuggestVmaMemoryFlags(vra::VraDataMemoryPattern::CPU_GPU, vra::VraDataUpdateRate::Frequent);
    allocation_create_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return Logger::LogWithVkResult(vmaCreateBuffer(vma_allocator,
                                                   &uniform_buffer_create_info,
                                                   &allocation_create_info,
                                                   reinterpret_cast<VkBuffer*>(&uniform_buffer),
                                                   &uniform_buffer_allocation,
                                                   &uniform_buffer_allocation_info),
                                   "Failed to create uniform buffer",
                                   "Succeeded in creating uniform buffer");
}

bool vulkan_sample::create_and_write_descriptor_relatives()
{
    vk::DescriptorPoolSize pool_size = 
    {
        .type = vk::DescriptorType::eUniformBufferDynamic, 
        .descriptorCount = 1
    };
    vk::DescriptorPoolCreateInfo descriptor_pool_create_info;
    descriptor_pool_create_info.setPoolSizes(pool_size).setPoolSizeCount(1).setMaxSets(1);

    descriptor_pool = comm_vk_logical_device.createDescriptorPool(descriptor_pool_create_info, nullptr);
    if (!descriptor_pool)
        return false;

    // create descriptor set layout

    vk::DescriptorSetLayoutBinding layout_binding;
    layout_binding.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    vk::DescriptorSetLayoutCreateInfo layout_create_info;
    layout_create_info.setBindingCount(1).setPBindings(&layout_binding);

    descriptor_set_layout = comm_vk_logical_device.createDescriptorSetLayout(layout_create_info, nullptr);
    if (!descriptor_set_layout)
        return false;

    // allocate descriptor set

    vk::DescriptorSetAllocateInfo alloc_info;
    alloc_info.setDescriptorPool(descriptor_pool).setDescriptorSetCount(1).setPSetLayouts(&descriptor_set_layout);

    descriptor_sets = comm_vk_logical_device.allocateDescriptorSets(alloc_info);
    if (descriptor_sets.empty())
        return false;

    // write descriptor set
    vk::DescriptorBufferInfo descriptor_buffer_info{.buffer = uniform_buffer, .offset = 0, .range = sizeof(mvp_matrix)};
    std::vector<vk::WriteDescriptorSet> write_descriptor_sets(descriptor_sets.size());
    for (size_t i = 0; i < descriptor_sets.size(); ++i)
    {
        write_descriptor_sets[i] = {.dstSet          = descriptor_sets[i],
                                    .dstBinding      = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType  = vk::DescriptorType::eUniformBufferDynamic,
                                    .pBufferInfo     = &descriptor_buffer_info};
    }
    comm_vk_logical_device.updateDescriptorSets(write_descriptor_sets, {});
    return true;
}

bool vulkan_sample::create_vma_vra_objects()
{
    // vra and vma members
    vra_data_batcher = std::make_unique<vra::VraDataBatcher>(comm_vk_physical_device);

    VmaAllocatorCreateInfo allocator_create_info = {};
    allocator_create_info.flags                  = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocator_create_info.vulkanApiVersion       = VK_API_VERSION_1_3;
    allocator_create_info.physicalDevice         = comm_vk_physical_device;
    allocator_create_info.device                 = comm_vk_logical_device;
    allocator_create_info.instance               = comm_vk_instance;

    return Logger::LogWithVkResult(vmaCreateAllocator(&allocator_create_info, &vma_allocator),
                                   "Failed to create Vulkan vra and vma objects",
                                   "Succeeded in creating Vulkan vra and vma objects");
}

// 查找支持的深度格式
vk::Format vulkan_sample::find_supported_depth_format()
{
    // 按优先级尝试不同的深度格式
    std::vector<vk::Format> candidates = {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint};

    for (vk::Format format : candidates)
    {
        vk::FormatProperties props;
        comm_vk_physical_device.getFormatProperties(format, &props);

        // 检查该格式是否支持作为深度附件的最佳平铺格式
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        {
            // 如果支持，则返回该格式
            Logger::LogInfo("Supported depth format found: " + vk::to_string(format));
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported depth format");
}

// 创建深度资源
bool vulkan_sample::create_depth_resources()
{
    // 获取深度格式
    depth_format = find_supported_depth_format();

    // 创建深度图像
    vk::ImageCreateInfo image_info;
    image_info.setImageType(vk::ImageType::e2D)
        .setExtent({.width  = comm_vk_swapchain_context.swapchain_info_.extent_.width,
                    .height = comm_vk_swapchain_context.swapchain_info_.extent_.height,
                    .depth  = 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(depth_format)
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    // 创建图像
    depth_image = comm_vk_logical_device.createImage(image_info, nullptr);
    if (!depth_image)
    {
        Logger::LogError("Failed to create depth image");
        return false;
    }

    // 获取内存需求
    vk::MemoryRequirements mem_requirements = comm_vk_logical_device.getImageMemoryRequirements(depth_image);

    // 分配内存
    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.setAllocationSize(mem_requirements.size);

    // 查找适合的内存类型
    uint32_t memory_type_index                        = 0;
    vk::PhysicalDeviceMemoryProperties mem_properties = comm_vk_physical_device.getMemoryProperties();

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
    {
        if (((mem_requirements.memoryTypeBits & (1 << i)) != 0U) &&
            (mem_properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal))
        {
            memory_type_index = i;
            break;
        }
    }

    alloc_info.memoryTypeIndex = memory_type_index;

    // 分配内存
    depth_memory = comm_vk_logical_device.allocateMemory(alloc_info, nullptr);
    if (!depth_memory)
    {
        Logger::LogError("Failed to allocate depth image memory");
        return false;
    }

    // 绑定内存到图像
    comm_vk_logical_device.bindImageMemory(depth_image, depth_memory, 0);

    // 创建图像视图
    vk::ImageViewCreateInfo view_info{};
    view_info.setImage(depth_image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(depth_format)
        .setSubresourceRange(vk::ImageSubresourceRange()
                                 .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                 .setBaseMipLevel(0)
                                 .setLevelCount(1)
                                 .setBaseArrayLayer(0)
                                 .setLayerCount(1));

    if (comm_vk_logical_device.createImageView(&view_info, nullptr, &depth_image_view) != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to create depth image view");
        return false;
    }

    return true;
}

bool vulkan_sample::create_frame_buffer()
{
    // 创建深度资源
    if (!create_depth_resources())
    {
        throw std::runtime_error("Failed to create depth resources.");
    }

    // 创建帧缓冲
    SVulkanFrameBufferConfig framebuffer_config(
        comm_vk_swapchain_context.swapchain_info_.extent_, comm_vk_swapchain_context.swapchain_image_views_, depth_image_view);

    vk_frame_buffer_helper = std::make_unique<VulkanFrameBufferHelper>(comm_vk_logical_device, framebuffer_config);

    return vk_frame_buffer_helper->CreateFrameBuffer(vk_renderpass_helper->GetRenderpass());
}

bool vulkan_sample::create_pipeline()
{
    // create shader
    vk_shader_helper = std::make_unique<VulkanShaderHelper>(comm_vk_logical_device);

    std::vector<SVulkanShaderConfig> shader_configs;
    std::string shader_path = config.general_config.working_directory + "src\\shader\\";
    // std::string vertex_shader_path = shader_path + "triangle.vert.spv";
    // std::string fragment_shader_path = shader_path + "triangle.frag.spv";
    std::string vertex_shader_path   = shader_path + "gltf.vert.spv";
    std::string fragment_shader_path = shader_path + "gltf.frag.spv";
    shader_configs.push_back({.shader_type = EShaderType::kVertexShader, .shader_path = vertex_shader_path.c_str()});
    shader_configs.push_back({.shader_type = EShaderType::kFragmentShader, .shader_path = fragment_shader_path.c_str()});

    for (const auto& shader_config : shader_configs)
    {
        std::vector<uint32_t> shader_code;
        if (!vk_shader_helper->ReadShaderCode(shader_config.shader_path, shader_code))
        {
            Logger::LogError("Failed to read shader code from " + std::string(shader_config.shader_path));
            return false;
        }

        if (!vk_shader_helper->CreateShaderModule(comm_vk_logical_device, shader_code, shader_config.shader_type))
        {
            Logger::LogError("Failed to create shader module for " + std::string(shader_config.shader_path));
            return false;
        }
    }

    // create renderpass
    SVulkanRenderpassConfig renderpass_config{
        .color_format = comm_vk_swapchain_context.swapchain_info_.surface_format_.format,
        .depth_format = vk::Format::eD32Sfloat,     // TODO: Make configurable
        .sample_count = vk::SampleCountFlagBits::e1 // TODO: Make configurable
    };
    vk_renderpass_helper = std::make_unique<VulkanRenderpassHelper>(renderpass_config);
    if (!vk_renderpass_helper->CreateRenderpass(comm_vk_logical_device))
    {
        return false;
    }

    // create pipeline
    SVulkanPipelineConfig pipeline_config{
        .swap_chain_extent                   = comm_vk_swapchain_context.swapchain_info_.extent_,
        .shader_module_map                   = {{EShaderType::kVertexShader, vk_shader_helper->GetShaderModule(EShaderType::kVertexShader)},
                                                {EShaderType::kFragmentShader, vk_shader_helper->GetShaderModule(EShaderType::kFragmentShader)}},
        .renderpass                          = vk_renderpass_helper->GetRenderpass(),
        .vertex_input_binding_description    = vertex_input_binding_description,
        .vertex_input_attribute_descriptions = vertex_input_attributes,
        .descriptor_set_layouts              = {descriptor_set_layout}};
    vk_pipeline_helper = std::make_unique<VulkanPipelineHelper>(pipeline_config);
    return vk_pipeline_helper->CreatePipeline(comm_vk_logical_device);
}

bool vulkan_sample::allocate_per_frame_command_buffer()
{
    for (int i = 0; i < config.frame_count; ++i)
    {
        if (!vk_command_buffer_helper->AllocateCommandBuffer({.command_buffer_level = vk::CommandBufferLevel::ePrimary, .command_buffer_count = 1},
                                                              output_frames[i].command_buffer_id))
        {
            Logger::LogError("Failed to allocate command buffer for frame " + std::to_string(i));
            return false;
        }
    }
    return true;
}

bool vulkan_sample::create_synchronization_objects()
{
    vk_synchronization_helper = std::make_unique<VulkanSynchronizationHelper>(comm_vk_logical_device);

    // Create synchronization objects per frame-in-flight
    for (int i = 0; i < config.frame_count; ++i)
    {
        if (!vk_synchronization_helper->CreateVkSemaphore(output_frames[i].image_available_semaphore_id))
            return false;

        // Note: Do NOT create the per-frame render_finished_semaphore here anymore.
        // We will use per-image semaphores instead.
        // if (!vk_synchronization_helper_->CreateVkSemaphore(output_frames_[i].render_finished_semaphore_id))
        //    return false;

        if (!vk_synchronization_helper->CreateFence(output_frames[i].fence_id))
            return false;
    }

    // Create render_finished semaphores per swapchain image
    const auto swapchain_images = comm_vk_logical_device.getSwapchainImagesKHR(comm_vk_swapchain);
    for (size_t i = 0; i < swapchain_images.size(); ++i)
    {
        // Use a unique ID based on the image index
        std::string id = "render_finished_semaphore_image_" + std::to_string(i);
        if (!vk_synchronization_helper->CreateVkSemaphore(id))
            return false;
    }

    return true;
}

// ----------------------------------
// private function to draw the frame
// ----------------------------------

void vulkan_sample::draw_frame()
{
    // get current resource
    auto current_fence_id                     = output_frames[frame_index].fence_id;
    auto current_image_available_semaphore_id = output_frames[frame_index].image_available_semaphore_id;
    // REMOVE or IGNORE the per-frame render_finished_semaphore_id
    auto current_command_buffer_id = output_frames[frame_index].command_buffer_id;

    // wait for the last frame to finish
    if (!vk_synchronization_helper->WaitForFence(current_fence_id))
        return;

    // get semaphores
    auto image_available_semaphore = vk_synchronization_helper->GetSemaphore(current_image_available_semaphore_id);
    auto in_flight_fence           = vk_synchronization_helper->GetFence(current_fence_id);

    // acquire next image
    auto acquire_result = comm_vk_logical_device.acquireNextImageKHR(comm_vk_swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE);

    // ... (error checks for acquire_result) ...
    if (acquire_result.result != vk::Result::eSuccess && acquire_result.result != vk::Result::eSuboptimalKHR)
    {
        // Handle resize or error
        if (acquire_result.result == vk::Result::eErrorOutOfDateKHR)
            resize_request = true;
        return;
    }

    // [FIX] Get the semaphore associated with the ACQUIRED IMAGE INDEX
    uint32_t image_index               = acquire_result.value;
    std::string render_finished_sem_id = "render_finished_semaphore_image_" + std::to_string(image_index);
    auto render_finished_semaphore     = vk_synchronization_helper->GetSemaphore(render_finished_sem_id);

    // reset fence before submitting
    if (!vk_synchronization_helper->ResetFence(current_fence_id))
        return;

    // record command buffer
    if (!vk_command_buffer_helper->ResetCommandBuffer(current_command_buffer_id))
        return;
    if (!record_command(image_index, current_command_buffer_id))
        return;

    // submit command buffer
    vk::CommandBufferSubmitInfo command_buffer_submit_info{.commandBuffer = vk_command_buffer_helper->GetCommandBuffer(current_command_buffer_id)};

    vk::SemaphoreSubmitInfo wait_semaphore_info{.semaphore = image_available_semaphore, .value = 1};

    // Use the per-image semaphore here
    vk::SemaphoreSubmitInfo signal_semaphore_info{.semaphore = render_finished_semaphore, .value = 1};

    vk::SubmitInfo2 submit_info{
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &wait_semaphore_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &command_buffer_submit_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_semaphore_info,
    };
    comm_vk_graphics_queue.submit2(submit_info, in_flight_fence);

    // present the image
    vk::PresentInfoKHR present_info{.waitSemaphoreCount = 1,
                                    .pWaitSemaphores    = &render_finished_semaphore, // Use the per-image semaphore here too
                                    .swapchainCount     = 1,
                                    .pSwapchains        = &comm_vk_swapchain,
                                    .pImageIndices      = &acquire_result.value};

    auto res = comm_vk_graphics_queue.presentKHR(&present_info);
    if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR)
    {
        resize_request = true;
        return;
    }
    if (res != vk::Result::eSuccess)
    {
        Logger::LogError("Failed to present image");
        return;
    }
    // Logger::LogInfo("Succeeded in presenting image");

    // update frame index
    frame_index = (frame_index + 1) % config.frame_count;
}

void vulkan_sample::resize_swapchain()
{
    // wait for the device to be idle
    comm_vk_logical_device.waitIdle();

    // destroy old vulkan objects

    for (auto image_view : comm_vk_swapchain_context.swapchain_image_views_)
    {
        comm_vk_logical_device.destroyImageView(image_view, nullptr);
    }
    // Note: Don't destroy swapchain images as they are owned by the swapchain
    comm_vk_logical_device.destroySwapchainKHR(comm_vk_swapchain, nullptr);
    vk_frame_buffer_helper.reset();

    // reset window size
    int width  = 0;
    int height = 0;
    window->get_extent(width, height);
    config.window_config.width  = width;
    config.window_config.height = height;

    // create new swapchain
    if (!create_swapchain())
    {
        throw std::runtime_error("Failed to create Vulkan swap chain.");
    }

    // recreate framebuffers
    if (!create_frame_buffer())
    {
        throw std::runtime_error("Failed to create Vulkan frame buffer.");
    }

    resize_request = false;
}

bool vulkan_sample::record_command(uint32_t image_index, const std::string& command_buffer_id)
{
    // 更新当前帧的 Uniform Buffer
    update_uniform_buffer(image_index);

    // begin command recording
    if (!vk_command_buffer_helper->BeginCommandBufferRecording(command_buffer_id, vk::CommandBufferUsageFlagBits::eOneTimeSubmit))
        return false;

    // collect needed objects
    auto command_buffer = vk_command_buffer_helper->GetCommandBuffer(command_buffer_id);

    // 从暂存缓冲区复制到本地缓冲区
    vk::BufferCopy buffer_copy_info{
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = local_host_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Rarely].consolidated_data.size(),
    };
    command_buffer.copyBuffer(staging_buffer, local_buffer, 1, &buffer_copy_info);

    // 设置内存屏障以确保复制完成
    vk::BufferMemoryBarrier2 buffer_memory_barrier{
        .srcStageMask  = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask  = vk::PipelineStageFlagBits2::eVertexInput,
        .dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead,
        .buffer        = local_buffer,
        .offset        = 0,
        .size          = VK_WHOLE_SIZE,
    };

    vk::DependencyInfo dependency_info{.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &buffer_memory_barrier};
    command_buffer.pipelineBarrier2(&dependency_info);

    // begin renderpass

    vk::ClearValue clear_values_color{.color = {std::array<float, 4>{0.1F, 0.1F, 0.1F, 1.0F}}};
    vk::ClearValue clear_value_depth{.depthStencil = {.depth = 1.0F, .stencil = 0}};
    std::vector<vk::ClearValue> clear_values = {clear_values_color, clear_value_depth};

    vk::RenderPassBeginInfo renderpass_info{.renderPass  = vk_renderpass_helper->GetRenderpass(),
                                            .framebuffer = (*vk_frame_buffer_helper->GetFramebuffers())[image_index],
                                            .renderArea  = {.offset = {.x = 0, .y = 0}, .extent = comm_vk_swapchain_context.swapchain_info_.extent_},
                                            .clearValueCount = static_cast<uint32_t>(clear_values.size()),
                                            .pClearValues    = clear_values.data()};

    command_buffer.beginRenderPass(renderpass_info, vk::SubpassContents::eInline);

    // bind pipeline
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vk_pipeline_helper->GetPipeline());

    // bind descriptor set
    auto offset         = uniform_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Frequently].offsets[uniform_buffer_id[frame_index]];
    auto dynamic_offset = static_cast<uint32_t>(offset);
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, vk_pipeline_helper->GetPipelineLayout(), 0, 1, descriptor_sets.data(), 1, &dynamic_offset);

    // dynamic state update
    vk::Viewport viewport{.x        = 0.0F,
                          .y        = 0.0F,
                          .width    = static_cast<float>(comm_vk_swapchain_context.swapchain_info_.extent_.width),
                          .height   = static_cast<float>(comm_vk_swapchain_context.swapchain_info_.extent_.height),
                          .minDepth = 0.0F,
                          .maxDepth = 1.0F};
    command_buffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor{.offset = {.x = 0, .y = 0}, .extent = comm_vk_swapchain_context.swapchain_info_.extent_};
    command_buffer.setScissor(0, 1, &scissor);

    // 绑定顶点和索引缓冲区
    command_buffer.bindVertexBuffers(
        0, 1, &local_buffer, &local_host_batch_handle[vra::VraBuiltInBatchIds::GPU_Only].offsets[vertex_buffer_id]);
    command_buffer.bindIndexBuffer(
        local_buffer, local_host_batch_handle[vra::VraBuiltInBatchIds::GPU_Only].offsets[index_buffer_id], vk::IndexType::eUint32);

    // 遍历每个 mesh 进行绘制
    for (const auto& mesh : mesh_list)
    {
        for (const auto& primitive : mesh.primitives)
        {
            // 绘制当前图元
            command_buffer.drawIndexed(primitive.index_count,
                                       // 使用实际的索引数量
                                       1,
                                       primitive.first_index,
                                       // 使用实际的索引偏移量
                                       0,
                                       0);
        }
    }

    // end renderpass
    command_buffer.endRenderPass();

    // end command recording
    return vk_command_buffer_helper->EndCommandBufferRecording(command_buffer_id);
}

void vulkan_sample::update_uniform_buffer(uint32_t current_frame_index)
{
    mvp_matrices[current_frame_index].model        = glm::mat4(1.0F);
    mvp_matrices[current_frame_index].view         = interface::get_view_matrix(camera_container->transforms[camera_entity_index]);
    mvp_matrices[current_frame_index].projection   = interface::get_projection_matrix(camera_container->transforms[camera_entity_index], camera_container->configs[camera_entity_index]);
    // reverse the Y-axis in Vulkan's NDC coordinate system
    mvp_matrices[current_frame_index].projection[1][1] *= -1;

    // map vulkan host memory to update the uniform buffer
    uniform_buffer_mapped_data = nullptr;
    vmaMapMemory(vma_allocator, uniform_buffer_allocation, &uniform_buffer_mapped_data);

    // get the offset of the current frame in the uniform buffer
    auto offset            = uniform_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Frequently].offsets[uniform_buffer_id[current_frame_index]];
    uint8_t* data_location = static_cast<uint8_t*>(uniform_buffer_mapped_data) + offset;

    // copy the data to the mapped memory
    std::memcpy(data_location, &mvp_matrices[current_frame_index], sizeof(mvp_matrix));

    // unmap the memory
    vmaUnmapMemory(vma_allocator, uniform_buffer_allocation);
    uniform_buffer_mapped_data = nullptr;
}

void vulkan_sample::create_drawcall_list_buffer()
{
    vra::VraRawData vertex_buffer_data{.pData_ = vertices.data(), .size_ = sizeof(gltf::Vertex) * vertices.size()};
    vra::VraRawData index_buffer_data{.pData_ = indices.data(), .size_ = sizeof(uint32_t) * indices.size()};

    // 顶点缓冲区创建信息
    vk::BufferCreateInfo vertex_buffer_create_info{.usage       = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                   .sharingMode = vk::SharingMode::eExclusive};
    vra::VraDataDesc vertex_buffer_desc{vra::VraDataMemoryPattern::GPU_Only, vra::VraDataUpdateRate::RarelyOrNever, vertex_buffer_create_info};

    // 索引缓冲区创建信息
    vk::BufferCreateInfo index_buffer_create_info{.usage       = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                  .sharingMode = vk::SharingMode::eExclusive};
    vra::VraDataDesc index_buffer_desc{vra::VraDataMemoryPattern::GPU_Only, vra::VraDataUpdateRate::RarelyOrNever, index_buffer_create_info};

    // 暂存缓冲区创建信息
    vk::BufferCreateInfo staging_buffer_create_info{.usage = vk::BufferUsageFlagBits::eTransferSrc, .sharingMode = vk::SharingMode::eExclusive};
    vra::VraDataDesc staging_vertex_buffer_desc{
        vra::VraDataMemoryPattern::CPU_GPU, vra::VraDataUpdateRate::RarelyOrNever, staging_buffer_create_info};
    vra::VraDataDesc staging_index_buffer_desc{vra::VraDataMemoryPattern::CPU_GPU, vra::VraDataUpdateRate::RarelyOrNever, staging_buffer_create_info};

    if (!vra_data_batcher->Collect(vertex_buffer_desc, vertex_buffer_data, vertex_buffer_id))
    {
        Logger::LogError("Failed to collect vertex buffer data");
        return;
    }
    if (!vra_data_batcher->Collect(index_buffer_desc, index_buffer_data, index_buffer_id))
    {
        Logger::LogError("Failed to collect index buffer data");
        return;
    }
    if (!vra_data_batcher->Collect(staging_vertex_buffer_desc, vertex_buffer_data, staging_vertex_buffer_id))
    {
        Logger::LogError("Failed to collect staging vertex buffer data");
        return;
    }
    if (!vra_data_batcher->Collect(staging_index_buffer_desc, index_buffer_data, staging_index_buffer_id))
    {
        Logger::LogError("Failed to collect staging index buffer data");
        return;
    }

    // 执行批处理
    local_host_batch_handle = vra_data_batcher->Batch();

    // 创建本地缓冲区
    auto local_buffer_create_info = local_host_batch_handle[vra::VraBuiltInBatchIds::GPU_Only].data_desc.GetBufferCreateInfo();
    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateBuffer(vma_allocator,
                    &local_buffer_create_info,
                    &allocation_create_info,
                    reinterpret_cast<VkBuffer*>(&local_buffer),
                    &local_buffer_allocation,
                    &local_buffer_allocation_info);

    // 创建暂存缓冲区
    auto host_buffer_create_info = local_host_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Rarely].data_desc.GetBufferCreateInfo();
    VmaAllocationCreateInfo staging_allocation_create_info{};
    staging_allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
    staging_allocation_create_info.flags =
        vra_data_batcher->GetSuggestVmaMemoryFlags(vra::VraDataMemoryPattern::CPU_GPU, vra::VraDataUpdateRate::RarelyOrNever);
    vmaCreateBuffer(vma_allocator,
                    &host_buffer_create_info,
                    &staging_allocation_create_info,
                    reinterpret_cast<VkBuffer*>(&staging_buffer),
                    &staging_buffer_allocation,
                    &staging_buffer_allocation_info);

    // 复制数据到暂存缓冲区
    auto consolidate_data = local_host_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Rarely].consolidated_data;
    void* data            = nullptr;
    vmaInvalidateAllocation(vma_allocator, staging_buffer_allocation, 0, VK_WHOLE_SIZE);
    vmaMapMemory(vma_allocator, staging_buffer_allocation, &data);
    std::memcpy(data, consolidate_data.data(), consolidate_data.size());
    vmaUnmapMemory(vma_allocator, staging_buffer_allocation);
    vmaFlushAllocation(vma_allocator, staging_buffer_allocation, 0, VK_WHOLE_SIZE);

    // 设置顶点输入绑定描述
    vertex_input_binding_description.binding   = 0;
    vertex_input_binding_description.stride    = sizeof(gltf::Vertex);
    vertex_input_binding_description.inputRate = vk::VertexInputRate::eVertex;

    // 设置顶点属性描述
    vertex_input_attributes.clear();

    // 使用更安全的偏移量计算，确保 offsetof 计算正确
    // position
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(gltf::Vertex, position)});
    // color
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 1, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(gltf::Vertex, color)});
    // normal
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 2, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(gltf::Vertex, normal)});
    // tangent
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 3, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(gltf::Vertex, tangent)});
    // uv0
    vertex_input_attributes.emplace_back(
        vk::VertexInputAttributeDescription{.location = 4, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(gltf::Vertex, uv0)});
    // uv1
    vertex_input_attributes.emplace_back(
        vk::VertexInputAttributeDescription{.location = 5, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(gltf::Vertex, uv1)});
}
