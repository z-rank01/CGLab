#include "render_graph_vulkan/vulkan_backend_internal.h"

#include <span>

namespace
{
    constexpr VkDeviceSize geometry_arena_capacity = 128ull * 1024ull * 1024ull;
}

bool vulkan_backend::create_geometry_arena()
{
    const auto created = runtime->create_buffer(render_graph::buffer_desc{
        .size = geometry_arena_capacity,
        .usage = render_graph::buffer_usage::TRANSFER_DST |
                 render_graph::buffer_usage::VERTEX_BUFFER |
                 render_graph::buffer_usage::INDEX_BUFFER,
        .memory = render_graph::memory_domain::device_local,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::persistent,
    }, geometry_resource);
    if (!created)
    {
        Logger::LogError("Failed to create RG geometry arena: " + created.error);
        return false;
    }
    geometry_buffer = runtime->buffer(geometry_resource);

    vertex_input_binding_description.binding = 0;
    vertex_input_binding_description.stride = sizeof(engine::vertex);
    vertex_input_binding_description.inputRate = vk::VertexInputRate::eVertex;
    vertex_input_attributes = {
        {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(engine::vertex, position)},
        {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(engine::vertex, color)},
        {.location = 2, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(engine::vertex, normal)},
        {.location = 3, .binding = 0, .format = vk::Format::eR32G32B32A32Sfloat, .offset = offsetof(engine::vertex, tangent)},
        {.location = 4, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(engine::vertex, uv0)},
        {.location = 5, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(engine::vertex, uv1)},
    };
    return true;
}

bool vulkan_backend::stage_runtime_geometry(const std::vector<engine::geometry_primitive>& primitives, staged_geometry& output)
{
    if (primitives.empty())
    {
        Logger::LogError("stage_runtime_geometry: empty primitive list");
        return false;
    }
    staged_geometry staged;
    staged.draws.reserve(primitives.size());
    staged.slices.reserve(primitives.size() * 2);
    for (const auto& primitive : primitives)
    {
        if (primitive.vertices.empty() || primitive.indices.empty())
        {
            Logger::LogError("stage_runtime_geometry: primitive has empty vertex or index data");
            return false;
        }
        const VkDeviceSize vertex_bytes = sizeof(engine::vertex) * primitive.vertices.size();
        const VkDeviceSize index_bytes = sizeof(uint32_t) * primitive.indices.size();
        render_graph::vk_buffer_slice vertex_slice;
        render_graph::vk_buffer_slice index_slice;
        if (!runtime->allocate_buffer_slice(geometry_resource, vertex_bytes, sizeof(engine::vertex), vertex_slice) ||
            !runtime->allocate_buffer_slice(geometry_resource, index_bytes, sizeof(uint32_t), index_slice))
        {
            Logger::LogError("stage_runtime_geometry: " + runtime->last_error());
            return false;
        }
        const auto vertex_source = std::as_bytes(std::span(primitive.vertices));
        const auto index_source = std::as_bytes(std::span(primitive.indices));
        if (!runtime->stage_buffer_upload(vertex_slice, vertex_source) ||
            !runtime->stage_buffer_upload(index_slice, index_source))
        {
            Logger::LogError("stage_runtime_geometry: " + runtime->last_error());
            return false;
        }
        staged.slices.push_back(vertex_slice);
        staged.slices.push_back(index_slice);
        staged.draws.push_back(engine::draw_range{
            .first_index = static_cast<uint32_t>(index_slice.offset / sizeof(uint32_t)),
            .index_count = static_cast<uint32_t>(primitive.indices.size()),
            .vertex_offset = static_cast<int32_t>(vertex_slice.offset / sizeof(engine::vertex)),
            .material_index = primitive.material_index,
        });
    }
    output = std::move(staged);
    return true;
}

void vulkan_backend::retire_runtime_geometry(staged_geometry geometry)
{
    const uint64_t gate = runtime->frames().next_submission;
    for (const auto slice : geometry.slices) runtime->release_buffer_slice(slice, gate);
}

void vulkan_backend::collect_deferred_resources()
{
    runtime->collect_retired();
}
