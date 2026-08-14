#include "apps/gltf_render_recipe.h"
#include "apps/lights_table.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
        constexpr uint64_t geometry_capacity = 256ull * 1024ull * 1024ull;
        constexpr uint32_t max_draws = 65536;
        constexpr uint32_t max_materials = 4096;
        // 光源表上传容量（SoA 三列连续布局：positions | colors | intensities）
        constexpr uint32_t max_lights = 64;

        struct frame_uniform { glm::mat4 model{1.0F}; glm::mat4 view{1.0F}; glm::mat4 projection{1.0F}; };
        struct transform_row { glm::mat4 model{1.0F}; glm::uvec4 metadata{}; };
        struct material_gpu_row
        {
            glm::vec4 base_color{1.0F};
            glm::vec4 emissive_metallic{0.0F, 0.0F, 0.0F, 1.0F};
            glm::vec4 roughness_alpha{1.0F, 0.5F, 0.0F, 0.0F};
            glm::vec4 texture_scales{1.0F, 1.0F, 0.0F, 0.0F};
            glm::uvec4 image_slots{};
            glm::uvec4 sampler_slots{};
            glm::uvec4 texcoords{};
            glm::uvec4 emissive_texture{};
        };
        struct push_constants
        {
            uint32_t frame_uniform_slot = 0;
            uint32_t transform_buffer_slot = 0;
            uint32_t material_buffer_slot = 0;
            uint32_t lights_buffer_slot = 0; // 光源表所在 storage buffer 表 slot
            uint32_t light_count = 0;        // 本帧光源数（0 = 无光源，shader 回退硬编码方向光）
        };
        struct draw_candidate
        {
            glm::mat4 model{1.0F};
            engine::draw_range range;
            float distance_squared = 0.0F;
        };

        struct recipe_state
        {
            render_graph::device_buffer_handle geometry;
            render_graph::device_buffer_handle transforms;
            render_graph::device_buffer_handle indirect;
            render_graph::device_buffer_handle materials;
            render_graph::device_buffer_handle lights; // 光源表（positions | colors | intensities 三列连续）
            std::vector<render_graph::device_buffer_handle> frame_uniforms;
            std::array<render_graph::device_pipeline_handle, 4> pipelines;
            uint32_t transform_slot = 0;
            uint32_t material_slot = 0;
            uint32_t lights_slot = 0;
            std::vector<uint32_t> frame_slots;
            // geometry 列（CSR，与 scene_registry 同款模式）：扁平 draw 列 + 每 mesh
            // begin/count 切片 + 存活列；mesh handle 直接索引列槽（build_frame 热路径直读列）。
            std::vector<engine::draw_range> geometry_draws;
            std::vector<std::uint32_t> geometry_draw_begins;
            std::vector<std::uint32_t> geometry_draw_counts;
            std::vector<std::uint8_t> geometry_alive;
            std::vector<material_gpu_row> material_rows{1};
            uint64_t geometry_cursor = 0;
            std::vector<transform_row> transform_rows;
            std::vector<render_graph::indexed_indirect_command> commands;
            // 加载帧的 staging 上传计数（build_frame 回填后清零）
            uint64_t staged_buffer_upload_count = 0;
            uint64_t staged_image_upload_count = 0;
            std::array<render_graph::draw_indexed_indirect_row, 4> draws;
            // 每帧分组 scratch（帧间复用，clear 后重建，稳态零分配）
            std::array<std::vector<draw_candidate>, 4> group_scratch;
            push_constants push;
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

        render_graph::sampler_address_mode address_mode(engine::sampler_wrap value)
        {
            if (value == engine::sampler_wrap::clamp_to_edge) return render_graph::sampler_address_mode::clamp_to_edge;
            if (value == engine::sampler_wrap::mirrored_repeat) return render_graph::sampler_address_mode::mirrored_repeat;
            return render_graph::sampler_address_mode::repeat;
        }

        engine::result<bool> initialize(void* value,
                                        render_graph::render_device& device,
                                        const engine::backend_config& config)
        {
            auto& state = *static_cast<recipe_state*>(value);
            std::vector<render_graph::buffer_create_row> buffers{
                {{.size = geometry_capacity,
                  .usage = render_graph::buffer_usage::TRANSFER_DST | render_graph::buffer_usage::VERTEX_BUFFER |
                           render_graph::buffer_usage::INDEX_BUFFER,
                  .memory = render_graph::memory_domain::device_local,
                  .aliasing = render_graph::aliasing_policy::forbidden,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
                {{.size = sizeof(transform_row) * max_draws,
                  .usage = render_graph::buffer_usage::STORAGE_BUFFER,
                  .memory = render_graph::memory_domain::upload,
                  .mapping = render_graph::mapping_policy::persistent,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
                {{.size = sizeof(render_graph::indexed_indirect_command) * max_draws,
                  .usage = render_graph::buffer_usage::INDIRECT_BUFFER,
                  .memory = render_graph::memory_domain::upload,
                  .mapping = render_graph::mapping_policy::persistent,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
                {{.size = sizeof(material_gpu_row) * max_materials,
                  .usage = render_graph::buffer_usage::STORAGE_BUFFER,
                  .memory = render_graph::memory_domain::upload,
                  .mapping = render_graph::mapping_policy::persistent,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
                // 光源表 buffer（positions | colors | intensities 三列连续，std430 布局）
                {{.size = sizeof(glm::vec4) * max_lights * 2 + sizeof(float) * max_lights,
                  .usage = render_graph::buffer_usage::STORAGE_BUFFER,
                  .memory = render_graph::memory_domain::upload,
                  .mapping = render_graph::mapping_policy::persistent,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
            };
            for (uint32_t frame = 0; frame < config.frames_in_flight; ++frame)
                buffers.push_back({{.size = sizeof(frame_uniform),
                                    .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
                                    .memory = render_graph::memory_domain::upload,
                                    .mapping = render_graph::mapping_policy::persistent,
                                    .lifetime = render_graph::resource_lifetime_class::persistent}});

            std::vector<uint32_t> vertex_shader;
            std::vector<uint32_t> fragment_shader;
            const auto shader_path = std::filesystem::path(config.working_directory) / "src" / "shader";
            if (!read_spirv(shader_path / "gltf.vert.spv", vertex_shader) ||
                !read_spirv(shader_path / "gltf.frag.spv", fragment_shader))
                return {.error = "Failed to read glTF Render Graph shaders"};
            std::vector<render_graph::graphics_pipeline_create_row> pipelines;
            for (uint32_t group = 0; group < 4; ++group)
            {
                render_graph::graphics_pipeline_desc pipeline;
                pipeline.shaders = {
                    {.stage = render_graph::shader_stage::vertex, .binary = vertex_shader},
                    {.stage = render_graph::shader_stage::fragment, .binary = fragment_shader},
                };
                pipeline.vertex_bindings = {{.binding = 0, .stride = sizeof(engine::vertex)}};
                pipeline.vertex_attributes = {
                    {.location = 0, .binding = 0, .format = render_graph::vertex_format::float3, .offset = offsetof(engine::vertex, position)},
                    {.location = 1, .binding = 0, .format = render_graph::vertex_format::float4, .offset = offsetof(engine::vertex, color)},
                    {.location = 2, .binding = 0, .format = render_graph::vertex_format::float3, .offset = offsetof(engine::vertex, normal)},
                    {.location = 3, .binding = 0, .format = render_graph::vertex_format::float4, .offset = offsetof(engine::vertex, tangent)},
                    {.location = 4, .binding = 0, .format = render_graph::vertex_format::float2, .offset = offsetof(engine::vertex, uv0)},
                    {.location = 5, .binding = 0, .format = render_graph::vertex_format::float2, .offset = offsetof(engine::vertex, uv1)},
                };
                pipeline.cull = (group & 1u) != 0 ? render_graph::cull_mode::none : render_graph::cull_mode::back;
                pipeline.blend = group >= 2;
                pipeline.depth_write = group < 2;
                pipeline.color_formats = {render_graph::format::UNDEFINED};
                pipeline.depth_format = render_graph::format::D32_SFLOAT;
                pipeline.push_constants = {{.stage_mask = render_graph::shader_stage_vertex_bit |
                                                           render_graph::shader_stage_fragment_bit,
                                            .size = sizeof(push_constants)}};
                pipelines.push_back({std::move(pipeline)});
            }
            auto created = device.apply_resource_changes({.buffer_creates = buffers,
                                                           .graphics_pipeline_creates = pipelines});
            if (!created) return {.error = created.error};
            state.geometry = created.buffers[0];
            state.transforms = created.buffers[1];
            state.indirect = created.buffers[2];
            state.materials = created.buffers[3];
            state.lights = created.buffers[4];
            state.frame_uniforms.assign(created.buffers.begin() + 5, created.buffers.end());
            std::copy_n(created.graphics_pipelines.begin(), 4, state.pipelines.begin());

            std::vector<render_graph::bindless_publish_row> publishes{
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.transforms, .size = sizeof(transform_row) * max_draws},
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.materials, .size = sizeof(material_gpu_row) * max_materials},
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.lights, .size = sizeof(glm::vec4) * max_lights * 2 + sizeof(float) * max_lights},
            };
            for (const auto buffer : state.frame_uniforms)
                publishes.push_back({.table = render_graph::bindless_table_kind::uniform_buffers,
                                     .buffer = buffer, .size = sizeof(frame_uniform)});
            const render_graph::buffer_upload_row default_material{
                state.materials, 0, std::as_bytes(std::span(state.material_rows))};
            auto bound = device.apply_resource_changes({.buffer_uploads = std::span(&default_material, 1),
                                                         .bindless_publishes = publishes});
            if (!bound) return {.error = bound.error};
            state.transform_slot = bound.bindless_slots[0];
            state.material_slot = bound.bindless_slots[1];
            state.lights_slot = bound.bindless_slots[2];
            state.frame_slots.assign(bound.bindless_slots.begin() + 3, bound.bindless_slots.end());
            return {.value = true};
        }

        engine::result<uint32_t> upload_materials(recipe_state& state,
                                                   render_graph::render_device& device,
                                                   const engine::asset_database& asset)
        {
            if (state.material_rows.size() + asset.materials.size() > max_materials)
                return {.error = "GPU material table capacity exhausted"};
            std::vector<render_graph::image_create_row> image_creates;
            for (const auto& image : asset.images)
                image_creates.push_back({{.fmt = render_graph::format::R8G8B8A8_UNORM,
                                          .extent = {image.width, image.height, 1},
                                          .usage = render_graph::image_usage::TRANSFER_DST |
                                                   render_graph::image_usage::SAMPLED,
                                          .memory = render_graph::memory_domain::device_local,
                                          .aliasing = render_graph::aliasing_policy::forbidden,
                                          .lifetime = render_graph::resource_lifetime_class::persistent}});
            std::vector<render_graph::sampler_create_row> sampler_creates;
            for (const auto& sampler : asset.samplers)
                sampler_creates.push_back({{
                    .min_filter = sampler.min_filter == engine::sampler_filter::nearest
                                      ? render_graph::sampler_filter::nearest : render_graph::sampler_filter::linear,
                    .mag_filter = sampler.mag_filter == engine::sampler_filter::nearest
                                      ? render_graph::sampler_filter::nearest : render_graph::sampler_filter::linear,
                    .address_u = address_mode(sampler.wrap_u),
                    .address_v = address_mode(sampler.wrap_v),
                }});
            auto created = device.apply_resource_changes({.image_creates = image_creates,
                                                           .sampler_creates = sampler_creates});
            if (!created) return {.error = created.error};
            std::vector<render_graph::image_upload_row> image_uploads;
            std::vector<render_graph::bindless_publish_row> publishes;
            for (uint32_t index = 0; index < asset.images.size(); ++index)
            {
                const auto& image = asset.images[index];
                image_uploads.push_back({created.images[index], image.width, image.height, 0, image.pixels});
                publishes.push_back({.table = render_graph::bindless_table_kind::sampled_images,
                                     .image = created.images[index]});
            }
            for (const auto sampler : created.samplers)
                publishes.push_back({.table = render_graph::bindless_table_kind::samplers, .sampler = sampler});
            auto published = device.apply_resource_changes({.image_uploads = image_uploads,
                                                             .bindless_publishes = publishes});
            if (!published) return {.error = published.error};
            const auto image_slot = [&](const engine::texture_ref& ref, uint32_t fallback = 0u)
            { return ref.image < asset.images.size() ? published.bindless_slots[ref.image] : fallback; };
            const auto sampler_slot = [&](const engine::texture_ref& ref)
            {
                const auto base = asset.images.size();
                return ref.sampler < asset.samplers.size() ? published.bindless_slots[base + ref.sampler] : 0u;
            };
            const uint32_t base = static_cast<uint32_t>(state.material_rows.size());
            for (const auto& source : asset.materials)
            {
                const float alpha = source.alpha == engine::alpha_mode::mask ? 1.0F
                                  : source.alpha == engine::alpha_mode::blend ? 2.0F : 0.0F;
                state.material_rows.push_back({
                    .base_color = source.base_color_factor,
                    .emissive_metallic = {source.emissive_factor, source.metallic_factor},
                    .roughness_alpha = {source.roughness_factor, source.alpha_cutoff, alpha,
                                        source.double_sided ? 1.0F : 0.0F},
                    .texture_scales = {source.normal_texture.scale, source.occlusion_texture.scale, 0.0F, 0.0F},
                    .image_slots = {image_slot(source.base_color_texture), image_slot(source.metallic_roughness_texture),
                                    image_slot(source.normal_texture, 1), image_slot(source.occlusion_texture)},
                    .sampler_slots = {sampler_slot(source.base_color_texture), sampler_slot(source.metallic_roughness_texture),
                                      sampler_slot(source.normal_texture), sampler_slot(source.occlusion_texture)},
                    .texcoords = {source.base_color_texture.texcoord, source.metallic_roughness_texture.texcoord,
                                  source.normal_texture.texcoord, source.occlusion_texture.texcoord},
                    .emissive_texture = {image_slot(source.emissive_texture), sampler_slot(source.emissive_texture),
                                         source.emissive_texture.texcoord, 0},
                });
            }
            const render_graph::buffer_upload_row upload{
                state.materials, sizeof(material_gpu_row) * base,
                std::as_bytes(std::span(state.material_rows).subspan(base))};
            const auto updated = device.apply_resource_changes({.buffer_uploads = std::span(&upload, 1)});
            if (!updated) return {.error = updated.error};
            state.staged_buffer_upload_count += 1;
            state.staged_image_upload_count += image_uploads.size();
            return {.value = base};
        }

        engine::result<engine::resource_change_result> apply_changes(
            void* value, render_graph::render_device& device, engine::resource_change_batch batch)
        {
            auto& state = *static_cast<recipe_state*>(value);
            engine::result<engine::resource_change_result> output;
            for (const auto& row : batch.material_uploads)
            {
                if (!row.asset) return {.error = "glTF material upload row is empty"};
                const auto uploaded = upload_materials(state, device, *row.asset);
                if (!uploaded) return {.error = uploaded.error};
                output.value.material_bases.push_back(uploaded.value);
            }
            for (const auto& row : batch.geometry_uploads)
            {
                if (!row.asset) return {.error = "glTF geometry upload row is empty"};
                // 批量行 → 纯函数布局计划 → 零拷贝上传（span 直接引用共享 blob）
                const auto plan = engine::plan_geometry_uploads(*row.asset, row.first_mesh, row.mesh_count,
                                                                row.material_base, geometry_capacity, state.geometry_cursor);
                if (!plan) return {.error = plan.error};
                std::vector<render_graph::buffer_upload_row> uploads;
                uploads.reserve(plan.primitives.size() * 2);
                // 先建本地列，上传成功后才并入 state（失败不污染状态）
                std::vector<engine::draw_range> created_draws;
                created_draws.reserve(plan.primitives.size());
                std::vector<std::uint32_t> created_begins;
                created_begins.reserve(row.mesh_count);
                std::vector<std::uint32_t> created_counts;
                created_counts.reserve(row.mesh_count);
                std::size_t primitive_index = 0;
                for (std::uint32_t mesh = 0; mesh < row.mesh_count; ++mesh)
                {
                    const std::uint32_t draw_count = plan.mesh_primitive_counts[mesh];
                    created_begins.push_back(static_cast<std::uint32_t>(created_draws.size()));
                    created_counts.push_back(draw_count);
                    for (std::uint32_t k = 0; k < draw_count; ++k)
                    {
                        const auto& pp = plan.primitives[primitive_index++];
                        uploads.push_back({state.geometry, pp.vertex_byte_offset,
                                           std::as_bytes(std::span(row.asset->vertex_blob)
                                                             .subspan(pp.vertex_element_offset, pp.vertex_count))});
                        uploads.push_back({state.geometry, pp.index_byte_offset,
                                           std::as_bytes(std::span(row.asset->index_blob)
                                                             .subspan(pp.index_element_offset, pp.index_count))});
                        created_draws.push_back({.first_index = pp.first_index,
                                                  .index_count = pp.index_count,
                                                  .vertex_offset = pp.vertex_offset,
                                                  .material_index = pp.material_index});
                    }
                }
                const auto uploaded = device.apply_resource_changes({.buffer_uploads = uploads});
                if (!uploaded) return {.error = uploaded.error};
                state.staged_buffer_upload_count += uploads.size();
                state.geometry_cursor = plan.cursor;
                const std::size_t first_draw_index = state.geometry_draws.size();
                state.geometry_draws.insert(state.geometry_draws.end(), created_draws.begin(), created_draws.end());
                for (std::size_t i = 0; i < created_begins.size(); ++i)
                {
                    output.value.geometry_handles.push_back(
                        static_cast<engine::geometry_handle>(state.geometry_draw_begins.size()));
                    state.geometry_draw_begins.push_back(
                        static_cast<std::uint32_t>(first_draw_index + created_begins[i]));
                    state.geometry_draw_counts.push_back(created_counts[i]);
                    state.geometry_alive.push_back(1);
                }
            }
            for (const auto& row : batch.geometry_retires)
                if (row.handle < state.geometry_alive.size()) state.geometry_alive[row.handle] = 0;
            return output;
        }

        render_graph::frame_build_result build_frame(
            void* value, render_graph::render_device& device, const engine::render_frame_packet& packet,
            const render_graph::frame_environment& environment, render_graph::frame_plan& plan)
        {
            auto& state = *static_cast<recipe_state*>(value);
            // 帧通道取用（缺失返回空 span——编写者责任）
            const auto camera_rows = packet.channels->find_rows<engine::camera_row>();
            const auto instance_rows = packet.channels->find_rows<engine::instance_row>();
            const auto transform_rows = packet.channels->find_rows<glm::mat4>();
            if (camera_rows.empty()) return {.error = "glTF frame has no camera"};
            // 分组 scratch 帧间复用（clear 后重建，稳态零分配）
            std::array<std::vector<draw_candidate>, 4>& groups = state.group_scratch;
            for (auto& group : groups)
                group.clear();
            const glm::vec3 camera_position = glm::vec3(glm::inverse(camera_rows.front().view)[3]);
            uint32_t candidate_count = 0;
            for (const auto& instance : instance_rows)
            {
                if (instance.mesh >= state.geometry_draw_begins.size() || instance.transform >= transform_rows.size())
                    continue;
                if (state.geometry_alive[instance.mesh] == 0) continue;
                const auto& model = transform_rows[instance.transform];
                // CSR 直读列：mesh handle → 扁平 draw 列切片（无堆指针追逐）
                const std::uint32_t draw_end = state.geometry_draw_begins[instance.mesh] +
                                               state.geometry_draw_counts[instance.mesh];
                for (std::uint32_t draw_slot = state.geometry_draw_begins[instance.mesh]; draw_slot < draw_end;
                     ++draw_slot)
                {
                    if (++candidate_count > max_draws) return {.error = "GPU draw table capacity exhausted"};
                    const auto& range = state.geometry_draws[draw_slot];
                    const uint32_t material_index = range.material_index < state.material_rows.size()
                                                      ? range.material_index : 0;
                    const auto& material = state.material_rows[material_index];
                    const bool blend = material.roughness_alpha.z == 2.0F;
                    const bool double_sided = material.roughness_alpha.w != 0.0F;
                    const uint32_t group = (blend ? 2u : 0u) + (double_sided ? 1u : 0u);
                    const glm::vec3 position = glm::vec3(model[3]);
                    groups[group].push_back({model, range,
                        glm::dot(position - camera_position, position - camera_position)});
                }
            }
            for (uint32_t group = 2; group < groups.size(); ++group)
                std::stable_sort(groups[group].begin(), groups[group].end(),
                    [](const auto& left, const auto& right)
                    { return left.distance_squared > right.distance_squared; });

            state.transform_rows.clear();
            state.commands.clear();
            uint64_t command_offset = 0;
            for (uint32_t group = 0; group < groups.size(); ++group)
            {
                state.draws[group] = {
                    .pipeline = state.pipelines[group],
                    .vertex_buffer = state.geometry,
                    .index_buffer = state.geometry,
                    .indirect_buffer = state.indirect,
                    .indirect_offset = command_offset,
                    .draw_count = static_cast<uint32_t>(groups[group].size()),
                    .stride = sizeof(render_graph::indexed_indirect_command),
                };
                for (const auto& candidate : groups[group])
                {
                    const uint32_t draw_index = static_cast<uint32_t>(state.commands.size());
                    state.transform_rows.push_back({.model = candidate.model,
                                                    .metadata = {candidate.range.material_index, 0, 0, 0}});
                    state.commands.push_back({candidate.range.index_count, 1, candidate.range.first_index,
                                              candidate.range.vertex_offset, draw_index});
                }
                command_offset = state.commands.size() * sizeof(render_graph::indexed_indirect_command);
            }
            frame_uniform uniform{.view = camera_rows.front().view,
                                  .projection = camera_rows.front().projection};
            uniform.projection[1][1] *= -1.0F;
            // 帧通道消费光源表（缺失 → 无光源，shader 回退硬编码方向光——编写者责任）。
            const apps::lights_table* lights = packet.channels->find_state<apps::lights_table>();
            const std::uint32_t light_count = (lights != nullptr && lights->count() <= max_lights)
                                                  ? static_cast<std::uint32_t>(lights->count())
                                                  : 0U;
            std::vector<glm::vec4> gpu_positions;
            std::vector<glm::vec4> gpu_colors;
            std::vector<float> gpu_intensities;
            if (light_count > 0)
            {
                gpu_positions.reserve(light_count);
                gpu_colors.reserve(light_count);
                gpu_intensities.reserve(light_count);
                for (std::uint32_t i = 0; i < light_count; ++i)
                {
                    gpu_positions.push_back(glm::vec4(lights->positions[i], 0.0F));
                    gpu_colors.push_back(glm::vec4(lights->colors[i], 1.0F));
                    gpu_intensities.push_back(lights->intensities[i]);
                }
            }
            std::array<render_graph::buffer_upload_row, 6> uploads{
                render_graph::buffer_upload_row{state.frame_uniforms[environment.frame_index], 0,
                                                std::as_bytes(std::span(&uniform, 1))},
                render_graph::buffer_upload_row{state.transforms, 0,
                                                std::as_bytes(std::span(state.transform_rows))},
                render_graph::buffer_upload_row{state.indirect, 0,
                                                std::as_bytes(std::span(state.commands))},
                render_graph::buffer_upload_row{state.lights, 0,
                                                std::as_bytes(std::span(gpu_positions))},
                render_graph::buffer_upload_row{state.lights, sizeof(glm::vec4) * max_lights,
                                                std::as_bytes(std::span(gpu_colors))},
                render_graph::buffer_upload_row{state.lights, sizeof(glm::vec4) * max_lights * 2,
                                                std::as_bytes(std::span(gpu_intensities))},
            };
            const std::uint32_t upload_count = light_count > 0 ? 6U : 3U;
            const auto updated = device.apply_resource_changes(
                {.buffer_uploads = std::span(uploads.data(), upload_count)});
            if (!updated) return {.error = updated.error};
            if (packet.counters)
            {
                // draw/upload 计数回填：本帧命令数 + 加载帧的 staging 行数
                packet.counters->draw_commands = state.commands.size();
                packet.counters->buffer_upload_count =
                    upload_count + std::exchange(state.staged_buffer_upload_count, 0);
                packet.counters->image_upload_count = std::exchange(state.staged_image_upload_count, 0);
            }
            state.push = {
                .frame_uniform_slot = state.frame_slots[environment.frame_index],
                .transform_buffer_slot = state.transform_slot,
                .material_buffer_slot = state.material_slot,
                .lights_buffer_slot = state.lights_slot,
                .light_count = light_count,
            };
            plan.cache_key = 0x474c544650425200ull;
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
                {.name = "GltfSponzaPass", .kind = render_graph::pass_kind::raster,
                 .buffer_accesses = {0, 3}, .attachments = {0, 2},
                 .indexed_indirect_draws = {0, 4},
                 .push_constant_size = sizeof(state.push),
                 .push_constant_stage_mask = render_graph::shader_stage_vertex_bit |
                                              render_graph::shader_stage_fragment_bit},
            }};
            plan.resources = state.frame_resources;
            plan.passes = state.frame_passes;
            plan.buffer_accesses = state.frame_buffer_accesses;
            plan.attachments = state.frame_attachments;
            plan.push_constants = std::as_bytes(std::span(&state.push, 1));
            plan.indexed_indirect_draws = state.draws;
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

    engine::render_driver create_gltf_render_driver()
    {
        return platform::vulkan::create_render_graph_driver({new recipe_state, &recipe_api});
    }
} // namespace apps
