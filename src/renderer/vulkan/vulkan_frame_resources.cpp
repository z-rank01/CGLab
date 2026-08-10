#include "renderer/vulkan/vulkan_backend_internal.h"

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

void vulkan_backend::create_drawcall_list_buffer()
{
    vra::VraRawData vertex_buffer_data{.pData_ = vertices.data(), .size_ = sizeof(engine::vertex) * vertices.size()};
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
    vertex_input_binding_description.stride    = sizeof(engine::vertex);
    vertex_input_binding_description.inputRate = vk::VertexInputRate::eVertex;

    // 设置顶点属性描述
    vertex_input_attributes.clear();

    // 使用更安全的偏移量计算，确保 offsetof 计算正确
    // position
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(engine::vertex, position)});
    // color
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 1, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(engine::vertex, color)});
    // normal
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 2, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(engine::vertex, normal)});
    // tangent
    vertex_input_attributes.emplace_back(vk::VertexInputAttributeDescription{
        .location = 3, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(engine::vertex, tangent)});
    // uv0
    vertex_input_attributes.emplace_back(
        vk::VertexInputAttributeDescription{.location = 4, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(engine::vertex, uv0)});
    // uv1
    vertex_input_attributes.emplace_back(
        vk::VertexInputAttributeDescription{.location = 5, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(engine::vertex, uv1)});
}
