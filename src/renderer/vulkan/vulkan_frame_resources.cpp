#include "renderer/vulkan/vulkan_backend_internal.h"

#include <fstream>
#include <span>

namespace
{
    bool read_spirv(const std::filesystem::path& path, std::vector<uint32_t>& words)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto size = input.tellg();
        if (size <= 0 || size % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) return false;
        words.resize(static_cast<size_t>(size) / sizeof(uint32_t));
        input.seekg(0);
        return static_cast<bool>(input.read(reinterpret_cast<char*>(words.data()), size));
    }
}

bool vulkan_backend::create_uniform_buffers()
{
    const VkDeviceSize alignment = comm_vk_physical_device.getProperties().limits.minUniformBufferOffsetAlignment;
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
    if (uniform_buffer == VK_NULL_HANDLE) return false;
    frame_uniform_slots.resize(config.frame_count);
    for (uint32_t frame = 0; frame < config.frame_count; frame++)
    {
        const auto allocated = runtime->allocate_uniform_buffer(uniform_resource,
                                                                uniform_stride * frame,
                                                                sizeof(mvp_matrix),
                                                                frame_uniform_slots[frame]);
        if (!allocated)
        {
            Logger::LogError("Failed to allocate frame uniform bindless slot: " + allocated.error);
            return false;
        }
    }
    return true;
}

bool vulkan_backend::create_pipeline()
{
    const std::filesystem::path shader_path =
        std::filesystem::path(config.working_directory) / "src" / "shader";
    std::vector<uint32_t> vertex_shader;
    std::vector<uint32_t> fragment_shader;
    if (!read_spirv(shader_path / "gltf.vert.spv", vertex_shader) ||
        !read_spirv(shader_path / "gltf.frag.spv", fragment_shader))
    {
        Logger::LogError("Failed to read glTF SPIR-V shaders from " + shader_path.string());
        return false;
    }

    render_graph::vk_graphics_pipeline_desc desc;
    desc.shaders = {
        {.stage = VK_SHADER_STAGE_VERTEX_BIT, .spirv = std::move(vertex_shader)},
        {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .spirv = std::move(fragment_shader)},
    };
    desc.vertex_layout.bindings.push_back(VkVertexInputBindingDescription{
        .binding = vertex_input_binding_description.binding,
        .stride = vertex_input_binding_description.stride,
        .inputRate = static_cast<VkVertexInputRate>(vertex_input_binding_description.inputRate),
    });
    for (const auto& attribute : vertex_input_attributes)
    {
        desc.vertex_layout.attributes.push_back(VkVertexInputAttributeDescription{
            .location = attribute.location,
            .binding = attribute.binding,
            .format = static_cast<VkFormat>(attribute.format),
            .offset = attribute.offset,
        });
    }
    desc.color_formats = {runtime->swapchain_images().format};
    desc.depth_format = VK_FORMAT_D32_SFLOAT;
    desc.push_constants = {VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(object_push_constants),
    }};
    const auto created = runtime->create_graphics_pipeline(desc, graphics_pipeline);
    if (!created)
    {
        Logger::LogError("Failed to create RG graphics pipeline: " + created.error);
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
