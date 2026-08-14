#include "apps/triangle_render_recipe.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

#include <glm/glm.hpp>

#include "platform/vulkan/render_graph_driver.h"
#include "engine/geometry_upload_plan.h"

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
            // A0：加载帧的 staging 上传行数（build_frame 回填后清零）
            uint64_t staged_buffer_upload_rows = 0;
            uint64_t staged_image_upload_rows = 0;
            std::array<render_graph::frame_resource_row, 5> frame_resources;
            std::array<render_graph::frame_buffer_access_row, 3> frame_buffer_accesses;
            std::array<render_graph::frame_attachment_row, 2> frame_attachments;
            std::array<render_graph::frame_pass_row, 1> frame_passes;
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
            const render_graph::graphics_pipeline_create_row pipeline_row{std::move(pipeline)};
            auto created = device.apply_resource_changes({
                .buffer_creates = buffers,
                .graphics_pipeline_creates = std::span(&pipeline_row, 1),
            });
            if (!created) return {.error = created.error};
            state.geometry = created.buffers[0];
            state.transforms = created.buffers[1];
            state.indirect = created.buffers[2];
            state.frame_uniforms.assign(created.buffers.begin() + 3, created.buffers.end());
            state.pipeline = created.graphics_pipelines.front();

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
                // A2：批量行 → 纯函数布局计划 → 零拷贝上传（span 直接引用共享 blob）
                const auto plan = engine::plan_geometry_uploads(*row.asset, row.first_mesh, row.mesh_count,
                                                                row.material_base, geometry_capacity, state.geometry_cursor);
                if (!plan) return {.error = plan.error};
                std::vector<render_graph::buffer_upload_row> uploads;
                uploads.reserve(plan.primitives.size() * 2);
                std::vector<geometry_row> created;
                created.reserve(row.mesh_count);
                std::size_t primitive_index = 0;
                for (std::uint32_t mesh = 0; mesh < row.mesh_count; ++mesh)
                {
                    geometry_row geometry;
                    geometry.draws.reserve(plan.mesh_primitive_counts[mesh]);
                    for (std::uint32_t k = 0; k < plan.mesh_primitive_counts[mesh]; ++k)
                    {
                        const auto& pp = plan.primitives[primitive_index++];
                        uploads.push_back({state.geometry, pp.vertex_byte_offset,
                                           std::as_bytes(std::span(row.asset->vertex_blob)
                                                             .subspan(pp.vertex_element_offset, pp.vertex_count))});
                        uploads.push_back({state.geometry, pp.index_byte_offset,
                                           std::as_bytes(std::span(row.asset->index_blob)
                                                             .subspan(pp.index_element_offset, pp.index_count))});
                        geometry.draws.push_back({.first_index = pp.first_index,
                                                  .index_count = pp.index_count,
                                                  .vertex_offset = pp.vertex_offset,
                                                  .material_index = pp.material_index});
                    }
                    created.push_back(std::move(geometry));
                }
                const auto uploaded = device.apply_resource_changes({.buffer_uploads = uploads});
                if (!uploaded) return {.error = uploaded.error};
                state.staged_buffer_upload_rows += uploads.size();
                state.geometry_cursor = plan.cursor;
                for (auto& geometry : created)
                {
                    output.value.geometry_handles.push_back(static_cast<engine::geometry_handle>(state.geometries.size()));
                    state.geometries.push_back(std::move(geometry));
                }
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
            if (packet.counters)
            {
                // draw/upload 计数回填（A0）：本帧命令数 + 加载帧的 staging 行数
                packet.counters->draw_commands = state.commands.size();
                packet.counters->buffer_upload_rows =
                    uploads.size() + std::exchange(state.staged_buffer_upload_rows, 0);
                packet.counters->image_upload_rows = std::exchange(state.staged_image_upload_rows, 0);
            }
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
            state.frame_resources = {{
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Geometry", .buffer = state.geometry},
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Transforms", .buffer = state.transforms},
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Indirect", .buffer = state.indirect},
                {.source = render_graph::frame_resource_source::swapchain_image, .name = "Swapchain"},
                {.source = render_graph::frame_resource_source::transient_image, .name = "Depth",
                 .image_description = {.fmt = render_graph::format::D32_SFLOAT,
                                       .extent = environment.extent,
                                       .usage = render_graph::image_usage::DEPTH_STENCIL_ATTACHMENT,
                                       .memory = render_graph::memory_domain::device_local,
                                       .lifetime = render_graph::resource_lifetime_class::transient}},
            }};
            state.frame_buffer_accesses = {{
                {{0}, render_graph::buffer_usage::VERTEX_BUFFER, render_graph::access_type::read},
                {{0}, render_graph::buffer_usage::INDEX_BUFFER, render_graph::access_type::read},
                {{2}, render_graph::buffer_usage::INDIRECT_BUFFER, render_graph::access_type::read},
            }};
            state.frame_attachments = {{
                {.resource = {3}, .kind = render_graph::frame_attachment_kind::color,
                 .clear = {.color = {0.03F, 0.04F, 0.08F, 1.0F}}},
                {.resource = {4}, .kind = render_graph::frame_attachment_kind::depth_stencil,
                 .store = render_graph::attachment_store_op::dont_care,
                 .clear = {.depth = 1.0F}},
            }};
            state.frame_passes = {{
                {.name = "TrianglePass", .kind = render_graph::pass_kind::raster,
                 .buffer_accesses = {0, 3}, .attachments = {0, 2},
                 .indexed_indirect_draws = {0, state.commands.empty() ? 0u : 1u},
                 .push_constant_size = sizeof(state.push),
                 .push_constant_stage_mask = render_graph::shader_stage_vertex_bit},
            }};
            plan.resources = state.frame_resources;
            plan.passes = state.frame_passes;
            plan.buffer_accesses = state.frame_buffer_accesses;
            plan.attachments = state.frame_attachments;
            plan.push_constants = std::as_bytes(std::span(&state.push, 1));
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
