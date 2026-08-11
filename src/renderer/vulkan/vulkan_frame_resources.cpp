#include "renderer/vulkan/vulkan_backend_internal.h"

#include <span>

void vulkan_backend::generate_frame_structs()
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
bool vulkan_backend::create_command_pool()
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

bool vulkan_backend::create_uniform_buffers()
{
    const VkDeviceSize alignment = comm_vk_physical_device_context.device_properties_.limits.minUniformBufferOffsetAlignment;
    uniform_stride = (sizeof(mvp_matrix) + alignment - 1) / alignment * alignment;
    const auto created = runtime->create_buffer(render_graph::buffer_desc{
        .size = uniform_stride * config.frame_count,
        .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, uniform_resource);
    if (!created)
    {
        Logger::LogError("Failed to create RG uniform table: " + created.error);
        return false;
    }
    uniform_buffer = runtime->buffer(uniform_resource);
    return uniform_buffer != VK_NULL_HANDLE;
}

bool vulkan_backend::create_and_write_descriptor_relatives()
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

bool vulkan_backend::create_pipeline()
{
    // create shader
    vk_shader_helper = std::make_unique<VulkanShaderHelper>(comm_vk_logical_device);

    std::vector<SVulkanShaderConfig> shader_configs;
    const std::filesystem::path shader_path =
        std::filesystem::path(config.working_directory) / "src" / "shader";
    // std::string vertex_shader_path = shader_path + "triangle.vert.spv";
    // std::string fragment_shader_path = shader_path + "triangle.frag.spv";
    std::string vertex_shader_path   = (shader_path / "gltf.vert.spv").string();
    std::string fragment_shader_path = (shader_path / "gltf.frag.spv").string();
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

    // create pipeline
    SVulkanPipelineConfig pipeline_config{
        .swap_chain_extent                   = comm_vk_swapchain_context.swapchain_info_.extent_,
        .shader_module_map                   = {{EShaderType::kVertexShader, vk_shader_helper->GetShaderModule(EShaderType::kVertexShader)},
                                                {EShaderType::kFragmentShader, vk_shader_helper->GetShaderModule(EShaderType::kFragmentShader)}},
        .color_format                       = comm_vk_swapchain_context.swapchain_info_.surface_format_.format,
        .depth_format                       = vk::Format::eD32Sfloat,
        .vertex_input_binding_description    = vertex_input_binding_description,
        .vertex_input_attribute_descriptions = vertex_input_attributes,
        .descriptor_set_layouts              = {descriptor_set_layout},
        .push_constant_ranges               = {vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}}};
    vk_pipeline_helper = std::make_unique<VulkanPipelineHelper>(pipeline_config);
    return vk_pipeline_helper->CreatePipeline(comm_vk_logical_device);
}

bool vulkan_backend::allocate_per_frame_command_buffer()
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

bool vulkan_backend::create_synchronization_objects()
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

void vulkan_backend::update_uniform_buffer(uint32_t current_frame_index)
{
    mvp_matrices[current_frame_index].model        = glm::mat4(1.0F);
    assert(current_snapshot != nullptr);
    mvp_matrices[current_frame_index].view = current_snapshot->view;
    mvp_matrices[current_frame_index].projection = current_snapshot->projection;
    // reverse the Y-axis in Vulkan's NDC coordinate system
    mvp_matrices[current_frame_index].projection[1][1] *= -1;

    const auto bytes = std::as_bytes(std::span(&mvp_matrices[current_frame_index], 1));
    if (!runtime->update_buffer(uniform_resource, uniform_stride * current_frame_index, bytes))
        Logger::LogError("Failed to update RG uniform table: " + runtime->last_error());
}
