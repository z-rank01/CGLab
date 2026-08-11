#include "renderer/vulkan/vulkan_backend_internal.h"

#include <span>

namespace
{
    constexpr std::uint32_t max_gpu_draw_rows = 65536;
}

bool vulkan_backend::create_gpu_scene_tables()
{
    const auto transforms = runtime->create_buffer(render_graph::buffer_desc{
        .size = sizeof(glm::mat4) * max_gpu_draw_rows,
        .usage = render_graph::buffer_usage::STORAGE_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, transform_resource);
    const auto indirect = runtime->create_buffer(render_graph::buffer_desc{
        .size = sizeof(VkDrawIndexedIndirectCommand) * max_gpu_draw_rows,
        .usage = render_graph::buffer_usage::INDIRECT_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, indirect_resource);
    if (!transforms || !indirect)
    {
        Logger::LogError("Failed to create GPU scene buffers: " +
                         (transforms ? indirect.error : transforms.error));
        return false;
    }
    transform_buffer = runtime->buffer(transform_resource);
    indirect_buffer = runtime->buffer(indirect_resource);
    const auto slot = runtime->allocate_storage_buffer(transform_resource,
                                                        0,
                                                        sizeof(glm::mat4) * max_gpu_draw_rows,
                                                        transform_buffer_slot);
    if (!slot)
    {
        Logger::LogError("Failed to allocate GPU transform table slot: " + slot.error);
        return false;
    }
    return true;
}

bool vulkan_backend::update_gpu_scene_tables()
{
    assert(current_snapshot != nullptr);
    std::vector<glm::mat4> transforms;
    std::vector<VkDrawIndexedIndirectCommand> commands;
    for (const engine::render_object& object : current_snapshot->objects)
    {
        const auto allocation = geometry_allocations.find(object.geometry);
        if (allocation == geometry_allocations.end()) continue;
        for (const engine::draw_range& range : allocation->second.draws)
        {
            if (commands.size() >= max_gpu_draw_rows)
            {
                Logger::LogError("GPU draw table capacity exhausted");
                return false;
            }
            const std::uint32_t draw_index = static_cast<std::uint32_t>(commands.size());
            transforms.push_back(object.model);
            commands.push_back(VkDrawIndexedIndirectCommand{
                .indexCount = range.index_count,
                .instanceCount = 1,
                .firstIndex = range.first_index,
                .vertexOffset = range.vertex_offset,
                .firstInstance = draw_index,
            });
        }
    }
    indirect_draw_count = static_cast<std::uint32_t>(commands.size());
    if (commands.empty()) return true;
    const auto transform_bytes = std::as_bytes(std::span(transforms));
    const auto command_bytes = std::as_bytes(std::span(commands));
    if (!runtime->update_buffer(transform_resource, 0, transform_bytes) ||
        !runtime->update_buffer(indirect_resource, 0, command_bytes))
    {
        Logger::LogError("Failed to update GPU scene tables: " + runtime->last_error());
        return false;
    }
    return true;
}
