#include "apps/triangle_render_recipe.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <span>

#include <glm/glm.hpp>

#include "platform/vulkan/render_graph_driver.h"

namespace apps
{
    namespace
    {
        constexpr uint64_t geometry_capacity = 4ull * 1024ull * 1024ull;
        constexpr uint32_t max_draws = 1024;

        struct frame_uniform { glm::mat4 model{1.0F}; glm::mat4 view{1.0F}; glm::mat4 projection{1.0F}; };
        struct transform_row { glm::mat4 model{1.0F}; glm::uvec4 metadata{}; };
        struct push_constants { uint32_t frame_slot = 0; uint32_t transform_slot = 0; };
        struct geometry_row { std::vector<engine::draw_range> draws; bool alive = true; };

        struct recipe_state
        {
            render_graph::device_buffer_handle geometry;
            render_graph::device_buffer_handle transforms;
            render_graph::device_buffer_handle indirect;
            std::vector<render_graph::device_buffer_handle> frame_uniforms;
            render_graph::device_pipeline_handle pipeline;
            uint32_t transform_slot = 0;
            std::vector<uint32_t> frame_slots;
            std::vector<geometry_row> geometries;
            uint64_t geometry_cursor = 0;
            std::vector<transform_row> transform_rows;
            std::vector<render_graph::indexed_indirect_command> commands;
            push_constants push;
            render_graph::draw_indexed_indirect_row draw;
        };

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

        uint64_t align_up(uint64_t value, uint64_t alignment)
        { return (value + alignment - 1) / alignment * alignment; }

        engine::result<bool> initialize(void* value,
                                        render_graph::render_device& device,
                                        const engine::backend_config& config)
        {
            auto& state = *static_cast<recipe_state*>(value);
            std::vector<render_graph::buffer_create_row> buffers;
            buffers.push_back({{.size = geometry_capacity,
                                .usage = render_graph::buffer_usage::TRANSFER_DST |
                                         render_graph::buffer_usage::VERTEX_BUFFER |
                                         render_graph::buffer_usage::INDEX_BUFFER,
                                .memory = render_graph::memory_domain::device_local,
                                .aliasing = render_graph::aliasing_policy::forbidden,
                                .lifetime = render_graph::resource_lifetime_class::persistent}});
            buffers.push_back({{.size = sizeof(transform_row) * max_draws,
                                .usage = render_graph::buffer_usage::STORAGE_BUFFER,
                                .memory = render_graph::memory_domain::upload,
                                .mapping = render_graph::mapping_policy::persistent,
                                .lifetime = render_graph::resource_lifetime_class::persistent}});
            buffers.push_back({{.size = sizeof(render_graph::indexed_indirect_command) * max_draws,
                                .usage = render_graph::buffer_usage::INDIRECT_BUFFER,
                                .memory = render_graph::memory_domain::upload,
                                .mapping = render_graph::mapping_policy::persistent,
                                .lifetime = render_graph::resource_lifetime_class::persistent}});
            for (uint32_t frame = 0; frame < config.frames_in_flight; ++frame)
                buffers.push_back({{.size = sizeof(frame_uniform),
                                    .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
                                    .memory = render_graph::memory_domain::upload,
                                    .mapping = render_graph::mapping_policy::persistent,
                                    .lifetime = render_graph::resource_lifetime_class::persistent}});

            std::vector<uint32_t> vertex_shader;
            std::vector<uint32_t> fragment_shader;
            const auto shader_path = std::filesystem::path(config.working_directory) / "src" / "shader";
            if (!read_spirv(shader_path / "triangle.vert.spv", vertex_shader) ||
                !read_spirv(shader_path / "triangle.frag.spv", fragment_shader))
                return {.error = "Failed to read Triangle Render Graph shaders"};
            render_graph::graphics_pipeline_desc pipeline;
            pipeline.shaders = {
                {.stage = render_graph::shader_stage::vertex, .binary = std::move(vertex_shader)},
                {.stage = render_graph::shader_stage::fragment, .binary = std::move(fragment_shader)},
            };
            pipeline.vertex_bindings = {{.binding = 0, .stride = sizeof(engine::vertex)}};
            pipeline.vertex_attributes = {
                {.location = 0, .binding = 0, .format = render_graph::vertex_format::float3, .offset = offsetof(engine::vertex, position)},
                {.location = 1, .binding = 0, .format = render_graph::vertex_format::float4, .offset = offsetof(engine::vertex, color)},
            };
            pipeline.color_formats = {render_graph::format::UNDEFINED};
            pipeline.depth_format = render_graph::format::D32_SFLOAT;
            pipeline.push_constants = {{.stage_mask = render_graph::shader_stage_vertex_bit,
                                        .size = sizeof(push_constants)}};
            const render_graph::pipeline_create_row pipeline_row{std::move(pipeline)};
            auto created = device.apply_resource_changes({
                .buffer_creates = buffers,
                .pipeline_creates = std::span(&pipeline_row, 1),
            });
            if (!created) return {.error = created.error};
            state.geometry = created.buffers[0];
            state.transforms = created.buffers[1];
            state.indirect = created.buffers[2];
            state.frame_uniforms.assign(created.buffers.begin() + 3, created.buffers.end());
            state.pipeline = created.pipelines.front();

            std::vector<render_graph::bindless_publish_row> publishes;
            publishes.push_back({.table = render_graph::bindless_table_kind::storage_buffers,
                                 .buffer = state.transforms, .size = sizeof(transform_row) * max_draws});
            for (const auto buffer : state.frame_uniforms)
                publishes.push_back({.table = render_graph::bindless_table_kind::uniform_buffers,
                                     .buffer = buffer, .size = sizeof(frame_uniform)});
            auto bound = device.apply_resource_changes({.bindless_publishes = publishes});
            if (!bound) return {.error = bound.error};
            state.transform_slot = bound.bindless_slots.front();
            state.frame_slots.assign(bound.bindless_slots.begin() + 1, bound.bindless_slots.end());
            return {.value = true};
        }

        engine::result<engine::resource_change_result> apply_changes(
            void* value, render_graph::render_device& device, engine::resource_change_batch batch)
        {
            auto& state = *static_cast<recipe_state*>(value);
            engine::result<engine::resource_change_result> output;
            for ([[maybe_unused]] const auto& row : batch.material_uploads)
                output.value.material_bases.push_back(0);
            for (const auto& row : batch.geometry_uploads)
            {
                if (!row.asset) return {.error = "Triangle geometry upload row is empty"};
                geometry_row geometry;
                std::vector<render_graph::buffer_upload_row> uploads;
                for (const auto& primitive : row.asset->primitives)
                {
                    const uint64_t vertex_offset = align_up(state.geometry_cursor, alignof(engine::vertex));
                    const uint64_t vertex_size = primitive.vertices.size() * sizeof(engine::vertex);
                    const uint64_t index_offset = align_up(vertex_offset + vertex_size, alignof(uint32_t));
                    const uint64_t index_size = primitive.indices.size() * sizeof(uint32_t);
                    if (index_offset + index_size > geometry_capacity) return {.error = "Triangle geometry arena exhausted"};
                    uploads.push_back({state.geometry, vertex_offset, std::as_bytes(std::span(primitive.vertices))});
                    uploads.push_back({state.geometry, index_offset, std::as_bytes(std::span(primitive.indices))});
                    geometry.draws.push_back({
                        .first_index = static_cast<uint32_t>(index_offset / sizeof(uint32_t)),
                        .index_count = static_cast<uint32_t>(primitive.indices.size()),
                        .vertex_offset = static_cast<int32_t>(vertex_offset / sizeof(engine::vertex)),
                        .material_index = primitive.material_index,
                    });
                    state.geometry_cursor = index_offset + index_size;
                }
                const auto uploaded = device.apply_resource_changes({.buffer_uploads = uploads});
                if (!uploaded) return {.error = uploaded.error};
                output.value.geometry_handles.push_back(static_cast<engine::geometry_handle>(state.geometries.size()));
                state.geometries.push_back(std::move(geometry));
            }
            for (const auto& row : batch.geometry_retires)
                if (row.handle < state.geometries.size()) state.geometries[row.handle].alive = false;
            return output;
        }

        render_graph::frame_build_result build_frame(
            void* value, render_graph::render_device& device, const engine::render_frame_packet& packet,
            const render_graph::frame_environment& environment, render_graph::frame_plan& plan)
        {
            auto& state = *static_cast<recipe_state*>(value);
            if (packet.camera_rows.empty()) return {.error = "Triangle frame has no camera"};
            state.transform_rows.clear();
            state.commands.clear();
            for (const auto& instance : packet.instance_rows)
            {
                if (instance.mesh >= state.geometries.size() || instance.transform >= packet.transform_rows.size()) continue;
                const auto& geometry = state.geometries[instance.mesh];
                if (!geometry.alive) continue;
                for (const auto& range : geometry.draws)
                {
                    const uint32_t draw_index = static_cast<uint32_t>(state.commands.size());
                    state.transform_rows.push_back({.model = packet.transform_rows[instance.transform]});
                    state.commands.push_back({range.index_count, 1, range.first_index,
                                              range.vertex_offset, draw_index});
                }
            }
            frame_uniform uniform{.view = packet.camera_rows.front().view,
                                  .projection = packet.camera_rows.front().projection};
            uniform.projection[1][1] *= -1.0F;
            std::array<render_graph::buffer_upload_row, 3> uploads{
                render_graph::buffer_upload_row{state.frame_uniforms[environment.frame_index], 0,
                                                std::as_bytes(std::span(&uniform, 1))},
                render_graph::buffer_upload_row{state.transforms, 0,
                                                std::as_bytes(std::span(state.transform_rows))},
                render_graph::buffer_upload_row{state.indirect, 0,
                                                std::as_bytes(std::span(state.commands))},
            };
            const auto updated = device.apply_resource_changes({.buffer_uploads = uploads});
            if (!updated) return {.error = updated.error};
            state.push = {.frame_slot = state.frame_slots[environment.frame_index],
                          .transform_slot = state.transform_slot};
            state.draw = {
                .pipeline = state.pipeline,
                .vertex_buffer = state.geometry,
                .index_buffer = state.geometry,
                .indirect_buffer = state.indirect,
                .draw_count = static_cast<uint32_t>(state.commands.size()),
                .stride = sizeof(render_graph::indexed_indirect_command),
            };
            plan.cache_key = 0x545249414e474c45ull;
            plan.pass_name = "TrianglePass";
            plan.clear_color = {0.03F, 0.04F, 0.08F, 1.0F};
            plan.push_constants = std::as_bytes(std::span(&state.push, 1));
            plan.push_constant_stage_mask = render_graph::shader_stage_vertex_bit;
            plan.indexed_indirect_draws = std::span(&state.draw, state.commands.empty() ? 0u : 1u);
            return {};
        }

        const platform::vulkan::render_recipe_api recipe_api{
            .initialize = &initialize,
            .apply_resource_changes = &apply_changes,
            .build_frame = &build_frame,
            .shutdown = [](void*, render_graph::render_device&) noexcept {},
            .destroy = [](void* value) noexcept { delete static_cast<recipe_state*>(value); },
        };
    } // namespace

    engine::render_driver create_triangle_render_driver()
    {
        return platform::vulkan::create_render_graph_driver({new recipe_state, &recipe_api});
    }
} // namespace apps
