#include "apps/gltf_render_recipe.h"
#include "apps/debug_view.h"
#include "apps/lights_table.h"
#include "apps/sun_light.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
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
        // 阴影图固定分辨率（独立于 swapchain；per-pass render_area 指定）
        constexpr uint32_t shadow_map_size = 2048;
        constexpr std::uint64_t debug_quad_vertex_bytes = sizeof(engine::vertex) * 6;
        constexpr std::uint64_t debug_quad_index_bytes = sizeof(std::uint32_t) * 6;

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
        // 平行光 UBO（std140 布局，与 shader 的 LightUniform 块一致）：
        // 光 view_proj 供 shadow pass 变换顶点与主 pass 采样阴影图。
        struct light_uniform
        {
            glm::mat4 view_proj{1.0F};
            glm::vec4 direction{0.0F};        // 光入射方向（光源 → 场景）
            glm::vec4 color{1.0F};
            float intensity = 0.0F;           // 0 = 无平行光（frag 跳过阴影采样）
            float pad0 = 0.0F;                // R2 后 PCF 由硬件完成，texel size 字段已删
            float pad1 = 0.0F;
            float pad2 = 0.0F;
        };
        // 统一 push constant（32 字节 = 8 uint）。字段顺序即内存布局，shadow
        // pass 推前 8 字节（light_uniform_slot + transform_buffer_slot），
        // 主 pass 推全部；两个 shader 的 ObjectPush 块与之一一对应。
        struct push_constants
        {
            uint32_t light_uniform_slot = 0;
            uint32_t transform_buffer_slot = 0;
            uint32_t frame_uniform_slot = 0;
            uint32_t material_buffer_slot = 0;
            uint32_t lights_buffer_slot = 0; // 点光源表所在 storage buffer 表 slot
            uint32_t light_count = 0;        // 本帧点光源数（0 = 无点光）
            uint32_t shadow_map_slot = 0;    // 阴影图在 sampled_images 表 slot
            uint32_t shadow_sampler_slot = 0; // 阴影采样器在 samplers 表 slot
        };
        // shadow pass 的 push 切片（前 8 字节，vertex stage）
        struct shadow_push
        {
            uint32_t light_uniform_slot = 0;
            uint32_t transform_buffer_slot = 0;
        };
        // 调试视图 push（R4/M3）：放在统一 push blob 的 32 字节偏移之后，
        // 与主 pass 的 push 切片互不重叠（不同管线各读各的切片）。
        struct debug_push
        {
            uint32_t image_slot = 0;
            uint32_t sampler_slot = 0;
            uint32_t mode = 0; // 0=原始深度 1=线性化热力图（debug_view_mode 语义）
            float near_plane = 0.1F;
            float far_plane = 240.0F;
        };
        struct draw_candidate
        {
            glm::mat4 model{1.0F};
            engine::draw_range range;
            float distance_squared = 0.0F;
            std::uint32_t arena = 0; // 几何所在 arena 池下标（draw 行按 (组, arena) 分段）
        };

        struct recipe_state
        {
            // 几何 arena 池（M2）：按需开新 256MB arena，几何句柄按 arena 分化；
            // 单 arena 时与单缓冲布局逐字节一致。arena 0 在 initialize 创建，
            // 后续 arena 在 apply_changes 发现容量不足时创建。
            std::vector<render_graph::device_buffer_handle> geometry_arenas;
            // 每个 arena 的高水位；与 geometry_arenas 并行，用于 M2 容量/利用率观测。
            std::vector<std::uint64_t> geometry_arena_used_bytes;
            render_graph::device_buffer_handle transforms;
            render_graph::device_buffer_handle indirect;
            render_graph::device_buffer_handle materials;
            render_graph::device_buffer_handle lights; // 光源表（positions | colors | intensities 三列连续）
            render_graph::device_image_handle shadow_map;
            render_graph::device_sampler_handle shadow_sampler;
            // 调试视图 raw 采样器（R2 新增）：comparison sampler 禁止非比较读取，
            // debug shader 读原始深度必须用独立普通采样器。
            render_graph::device_sampler_handle debug_sampler;
            // 调试视图（R4/M3）：NDC quad 几何 + 专用管线（采样阴影图到角落 inset）
            render_graph::device_buffer_handle debug_quad;
            render_graph::device_pipeline_handle debug_pipeline;
            std::vector<render_graph::device_buffer_handle> frame_uniforms;
            std::vector<render_graph::device_buffer_handle> light_uniforms;
            std::array<render_graph::device_pipeline_handle, 4> pipelines;
            render_graph::device_pipeline_handle shadow_pipeline;
            uint32_t transform_slot = 0;
            uint32_t material_slot = 0;
            uint32_t lights_slot = 0;
            uint32_t shadow_map_slot = 0;
            uint32_t shadow_sampler_slot = 0;
            uint32_t debug_sampler_slot = 0;
            std::vector<uint32_t> frame_slots;
            std::vector<uint32_t> light_uniform_slots;
            // geometry 列（CSR，与 scene_registry 同款模式）：扁平 draw 列 + 每 mesh
            // begin/count 切片 + 存活列；mesh handle 直接索引列槽（build_frame 热路径直读列）。
            // geometry_draw_arenas 与 draw 列并行（每 draw 所在 arena，draw 行分段用）。
            std::vector<engine::draw_range> geometry_draws;
            std::vector<std::uint32_t> geometry_draw_begins;
            std::vector<std::uint32_t> geometry_draw_counts;
            std::vector<std::uint8_t> geometry_alive;
            std::vector<std::uint32_t> geometry_draw_arenas;
            std::vector<material_gpu_row> material_rows{1};
            uint32_t geometry_arena = 0;      // 当前 arena（geometry_cursor 归属）
            uint64_t geometry_cursor = 0;     // 当前 arena 内游标
            std::vector<transform_row> transform_rows;
            std::vector<render_graph::indexed_indirect_command> commands;
            // 加载帧的 staging 上传计数（build_frame 回填后清零）
            uint64_t staged_buffer_upload_count = 0;
            uint64_t staged_image_upload_count = 0;
            std::vector<render_graph::draw_indexed_indirect_row> draws;
            // 每帧分组 scratch（帧间复用，clear 后重建，稳态零分配）
            std::array<std::vector<draw_candidate>, 4> group_scratch;
            push_constants push;
            // frame_plan 只保存 span，数据必须活到 backend 完成本帧录制；不能使用
            // build_frame 局部数组，否则返回后主/shadow/debug pass 都会读悬空 push 数据。
            std::array<std::byte, sizeof(push_constants) + sizeof(debug_push)> push_blob{};
            std::array<render_graph::frame_resource_row, 7> frame_resources;
            std::array<render_graph::frame_buffer_access_row, 4> frame_buffer_accesses;
            std::array<render_graph::frame_image_access_row, 2> frame_image_accesses;
            std::array<render_graph::frame_attachment_row, 4> frame_attachments;
            std::array<render_graph::frame_pass_row, 3> frame_passes;
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

        // 几何 arena 描述（M2）：池内每个 arena 同一形态——大 buffer 子分配语义，
        // 按需开新实例消除固定容量墙。
        render_graph::buffer_create_row geometry_arena_desc()
        {
            return {{.size = geometry_capacity,
                     .usage = render_graph::buffer_usage::TRANSFER_DST | render_graph::buffer_usage::VERTEX_BUFFER |
                              render_graph::buffer_usage::INDEX_BUFFER,
                     .memory = render_graph::memory_domain::device_local,
                     .aliasing = render_graph::aliasing_policy::forbidden,
                     .lifetime = render_graph::resource_lifetime_class::persistent}};
        }

        // 调试视图 NDC quad（R4/M3）：6 顶点两三角，位置即 NDC（-1..1），
        // uv 由顶点着色器从位置推导（含 Y 翻转），其余属性不消费。
        std::array<engine::vertex, 6> debug_quad_vertices()
        {
            const glm::vec4 color{1.0F};
            const glm::vec3 normal{0.0F, 0.0F, 1.0F};
            const glm::vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
            const glm::vec2 uv0{};
            const glm::vec2 uv1{};
            return {{
                {{-1.0F, -1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
                {{ 1.0F, -1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
                {{ 1.0F,  1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
                {{-1.0F, -1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
                {{ 1.0F,  1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
                {{-1.0F,  1.0F, 0.0F}, color, normal, tangent, uv0, uv1},
            }};
        }

        constexpr std::array<std::uint32_t, 6> debug_quad_indices()
        {
            return {0, 1, 2, 3, 4, 5};
        }

        engine::result<bool> initialize(void* value,
                                        render_graph::render_device& device,
                                        const engine::backend_config& config)
        {
            auto& state = *static_cast<recipe_state*>(value);
            std::vector<render_graph::buffer_create_row> buffers{
                geometry_arena_desc(),
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
                // 调试视图 quad 几何（NDC 两三角，R4/M3）
                {{.size = debug_quad_vertex_bytes + debug_quad_index_bytes,
                  .usage = render_graph::buffer_usage::TRANSFER_DST | render_graph::buffer_usage::VERTEX_BUFFER |
                           render_graph::buffer_usage::INDEX_BUFFER,
                  .memory = render_graph::memory_domain::device_local,
                  .aliasing = render_graph::aliasing_policy::forbidden,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
            };
            for (uint32_t frame = 0; frame < config.frames_in_flight; ++frame)
                buffers.push_back({{.size = sizeof(frame_uniform),
                                    .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
                                    .memory = render_graph::memory_domain::upload,
                                    .mapping = render_graph::mapping_policy::persistent,
                                    .lifetime = render_graph::resource_lifetime_class::persistent}});
            // 每帧 in-flight 一个平行光 UBO（阴影光 view_proj + 参数）
            for (uint32_t frame = 0; frame < config.frames_in_flight; ++frame)
                buffers.push_back({{.size = sizeof(light_uniform),
                                    .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
                                    .memory = render_graph::memory_domain::upload,
                                    .mapping = render_graph::mapping_policy::persistent,
                                    .lifetime = render_graph::resource_lifetime_class::persistent}});
            // 持久阴影图（DEPTH|SAMPLED）：shadow pass 作 depth 附件写，主 pass 采样读
            std::vector<render_graph::image_create_row> images{
                {{.fmt = render_graph::format::D32_SFLOAT,
                  .extent = {shadow_map_size, shadow_map_size, 1},
                  .usage = render_graph::image_usage::DEPTH_STENCIL_ATTACHMENT |
                           render_graph::image_usage::SAMPLED,
                  .memory = render_graph::memory_domain::device_local,
                  .aliasing = render_graph::aliasing_policy::forbidden,
                  .lifetime = render_graph::resource_lifetime_class::persistent}},
            };
            // 阴影采样器（R2）：comparison sampler（LESS_OR_EQUAL）+ linear 滤波，
            // shader 单次 dref 采样即硬件 2×2 PCF；clamp_to_edge 让 uv 越界采样落
            // 在视锥边缘而不是环绕。
            // 调试视图 raw 采样器：nearest + clamp、无比较——debug shader 用
            // 普通 texture() 读原始深度，comparison sampler 禁止非比较读取。
            std::vector<render_graph::sampler_create_row> samplers{
                {{.min_filter = render_graph::sampler_filter::linear,
                  .mag_filter = render_graph::sampler_filter::linear,
                  .address_u = render_graph::sampler_address_mode::clamp_to_edge,
                  .address_v = render_graph::sampler_address_mode::clamp_to_edge,
                  .compare_op = render_graph::sampler_compare_op::less_or_equal}},
                {{.min_filter = render_graph::sampler_filter::nearest,
                  .mag_filter = render_graph::sampler_filter::nearest,
                  .address_u = render_graph::sampler_address_mode::clamp_to_edge,
                  .address_v = render_graph::sampler_address_mode::clamp_to_edge}},
            };

            std::vector<uint32_t> vertex_shader;
            std::vector<uint32_t> fragment_shader;
            std::vector<uint32_t> shadow_vertex_shader;
            std::vector<uint32_t> debug_vertex_shader;
            std::vector<uint32_t> debug_fragment_shader;
            const auto shader_path = std::filesystem::path(config.working_directory) / "src" / "shader";
            if (!read_spirv(shader_path / "gltf.vert.spv", vertex_shader) ||
                !read_spirv(shader_path / "gltf.frag.spv", fragment_shader) ||
                !read_spirv(shader_path / "gltf_shadow.vert.spv", shadow_vertex_shader) ||
                !read_spirv(shader_path / "debug_view.vert.spv", debug_vertex_shader) ||
                !read_spirv(shader_path / "debug_view.frag.spv", debug_fragment_shader))
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
            // depth-only 阴影 pipeline：无 fragment shader、无 color format，
            // front-face culling 防 peter-panning；推 shadow_push 切片（vertex stage）。
            // 顶点属性只保留 location 0（position）——depth 输出不消费其余属性。
            render_graph::graphics_pipeline_desc shadow_pipeline;
            shadow_pipeline.shaders = {
                {.stage = render_graph::shader_stage::vertex, .binary = shadow_vertex_shader},
            };
            shadow_pipeline.vertex_bindings = {{.binding = 0, .stride = sizeof(engine::vertex)}};
            shadow_pipeline.vertex_attributes = {
                {.location = 0, .binding = 0, .format = render_graph::vertex_format::float3, .offset = offsetof(engine::vertex, position)},
            };
            shadow_pipeline.cull = render_graph::cull_mode::front;
            shadow_pipeline.depth_test = true;
            shadow_pipeline.depth_write = true;
            shadow_pipeline.depth_format = render_graph::format::D32_SFLOAT;
            shadow_pipeline.push_constants = {{.stage_mask = render_graph::shader_stage_vertex_bit,
                                               .size = sizeof(shadow_push)}};
            pipelines.push_back({std::move(shadow_pipeline)});
            // 调试视图管线（R4/M3）：采样中间 RT 画到角落 inset；无深度测试/写入，
            // 无 cull（NDC quad），push 只走 fragment stage。
            render_graph::graphics_pipeline_desc debug_pipeline;
            debug_pipeline.shaders = {
                {.stage = render_graph::shader_stage::vertex, .binary = debug_vertex_shader},
                {.stage = render_graph::shader_stage::fragment, .binary = debug_fragment_shader},
            };
            debug_pipeline.vertex_bindings = {{.binding = 0, .stride = sizeof(engine::vertex)}};
            debug_pipeline.vertex_attributes = {
                {.location = 0, .binding = 0, .format = render_graph::vertex_format::float3, .offset = offsetof(engine::vertex, position)},
            };
            debug_pipeline.cull = render_graph::cull_mode::none;
            debug_pipeline.depth_test = false;
            debug_pipeline.depth_write = false;
            debug_pipeline.color_formats = {render_graph::format::UNDEFINED};
            debug_pipeline.push_constants = {{.stage_mask = render_graph::shader_stage_fragment_bit,
                                              .size = sizeof(debug_push)}};
            pipelines.push_back({std::move(debug_pipeline)});
            auto created = device.apply_resource_changes({.buffer_creates = buffers,
                                                           .image_creates = images,
                                                           .sampler_creates = samplers,
                                                           .graphics_pipeline_creates = pipelines});
            if (!created) return {.error = created.error};
            state.geometry_arenas.push_back(created.buffers[0]);
            state.geometry_arena_used_bytes.push_back(0);
            state.transforms = created.buffers[1];
            state.indirect = created.buffers[2];
            state.materials = created.buffers[3];
            state.lights = created.buffers[4];
            state.debug_quad = created.buffers[5];
            const std::size_t uniform_begin = 6;
            state.frame_uniforms.assign(created.buffers.begin() + uniform_begin,
                                        created.buffers.begin() + uniform_begin + config.frames_in_flight);
            state.light_uniforms.assign(created.buffers.begin() + uniform_begin + config.frames_in_flight,
                                        created.buffers.end());
            state.shadow_map = created.images[0];
            state.shadow_sampler = created.samplers[0];
            state.debug_sampler = created.samplers[1];
            std::copy_n(created.graphics_pipelines.begin(), 4, state.pipelines.begin());
            state.shadow_pipeline = created.graphics_pipelines[4];
            state.debug_pipeline = created.graphics_pipelines[5];

            std::vector<render_graph::bindless_publish_row> publishes{
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.transforms, .size = sizeof(transform_row) * max_draws},
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.materials, .size = sizeof(material_gpu_row) * max_materials},
                {.table = render_graph::bindless_table_kind::storage_buffers,
                 .buffer = state.lights, .size = sizeof(glm::vec4) * max_lights * 2 + sizeof(float) * max_lights},
                {.table = render_graph::bindless_table_kind::sampled_images, .image = state.shadow_map},
                {.table = render_graph::bindless_table_kind::samplers, .sampler = state.shadow_sampler},
                {.table = render_graph::bindless_table_kind::samplers, .sampler = state.debug_sampler},
            };
            for (const auto buffer : state.frame_uniforms)
                publishes.push_back({.table = render_graph::bindless_table_kind::uniform_buffers,
                                     .buffer = buffer, .size = sizeof(frame_uniform)});
            for (const auto buffer : state.light_uniforms)
                publishes.push_back({.table = render_graph::bindless_table_kind::uniform_buffers,
                                     .buffer = buffer, .size = sizeof(light_uniform)});
            const render_graph::buffer_upload_row default_material{
                state.materials, 0, std::as_bytes(std::span(state.material_rows))};
            // 调试视图 quad 几何（一次性上传）
            const auto quad_vertices = debug_quad_vertices();
            const auto quad_indices = debug_quad_indices();
            const std::array debug_quad_uploads{
                render_graph::buffer_upload_row{state.debug_quad, 0,
                                                std::as_bytes(std::span(quad_vertices))},
                render_graph::buffer_upload_row{state.debug_quad, debug_quad_vertex_bytes,
                                                std::as_bytes(std::span(quad_indices))},
            };
            std::vector<render_graph::buffer_upload_row> initial_uploads{
                default_material, debug_quad_uploads[0], debug_quad_uploads[1]};
            auto bound = device.apply_resource_changes({.buffer_uploads = initial_uploads,
                                                         .bindless_publishes = publishes});
            if (!bound) return {.error = bound.error};
            state.transform_slot = bound.bindless_slots[0];
            state.material_slot = bound.bindless_slots[1];
            state.lights_slot = bound.bindless_slots[2];
            state.shadow_map_slot = bound.bindless_slots[3];
            state.shadow_sampler_slot = bound.bindless_slots[4];
            state.debug_sampler_slot = bound.bindless_slots[5];
            const std::size_t frame_slot_begin = 6;
            state.frame_slots.assign(bound.bindless_slots.begin() + frame_slot_begin,
                                     bound.bindless_slots.begin() + frame_slot_begin + config.frames_in_flight);
            state.light_uniform_slots.assign(bound.bindless_slots.begin() + frame_slot_begin + config.frames_in_flight,
                                             bound.bindless_slots.end());
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
                // 批量行 → 纯函数布局计划 → 零拷贝上传（span 直接引用共享 blob）。
                // arena 池（M2）：先按当前 arena 剩余空间布局；不足则开新 arena
                // 从头布局（游标归零）。单个资产超过 arena 容量（256MB）仍报错。
                const auto plan_begin = std::chrono::steady_clock::now();
                engine::result<engine::geometry_upload_plan> planned = engine::plan_geometry_uploads(
                    *row.asset, row.first_mesh, row.mesh_count, row.material_base,
                    geometry_capacity, state.geometry_cursor);
                std::uint32_t arena_index = state.geometry_arena;
                bool needs_new_arena = false;
                if (!planned)
                {
                    planned = engine::plan_geometry_uploads(*row.asset, row.first_mesh, row.mesh_count,
                                                            row.material_base, geometry_capacity, 0);
                    if (!planned) return {.error = planned.error};
                    arena_index = static_cast<std::uint32_t>(state.geometry_arenas.size());
                    needs_new_arena = true;
                }
                output.value.geometry_plan_us += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - plan_begin).count());
                if (needs_new_arena)
                {
                    const render_graph::buffer_create_row arena_desc = geometry_arena_desc();
                    const auto allocation_begin = std::chrono::steady_clock::now();
                    const auto created = device.apply_resource_changes({.buffer_creates = std::span(&arena_desc, 1)});
                    output.value.geometry_arena_allocation_us += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - allocation_begin).count());
                    if (!created) return {.error = created.error};
                    state.geometry_arenas.push_back(created.buffers[0]);
                    state.geometry_arena_used_bytes.push_back(0);
                    ++output.value.geometry_arenas_created;
                }
                const engine::geometry_upload_plan& layout = planned.value;
                state.geometry_arena = arena_index;
                state.geometry_cursor = layout.cursor;
                std::vector<render_graph::buffer_upload_row> uploads;
                uploads.reserve(layout.primitive_plan_rows.size() * 2);
                // 先建本地列，上传成功后才并入 state（失败不污染状态）
                std::vector<engine::draw_range> created_draws;
                created_draws.reserve(layout.primitive_plan_rows.size());
                std::vector<std::uint32_t> created_draw_arenas;
                created_draw_arenas.reserve(layout.primitive_plan_rows.size());
                std::vector<std::uint32_t> created_begins;
                created_begins.reserve(row.mesh_count);
                std::vector<std::uint32_t> created_counts;
                created_counts.reserve(row.mesh_count);
                std::size_t primitive_index = 0;
                for (std::uint32_t mesh = 0; mesh < row.mesh_count; ++mesh)
                {
                    const std::uint32_t draw_count = layout.mesh_primitive_counts[mesh];
                    created_begins.push_back(static_cast<std::uint32_t>(created_draws.size()));
                    created_counts.push_back(draw_count);
                    for (std::uint32_t k = 0; k < draw_count; ++k)
                    {
                        const auto& pp = layout.primitive_plan_rows[primitive_index++];
                        uploads.push_back({state.geometry_arenas[arena_index], pp.vertex_byte_offset,
                                           std::as_bytes(std::span(row.asset->vertex_blob)
                                                             .subspan(pp.vertex_element_offset, pp.vertex_count))});
                        uploads.push_back({state.geometry_arenas[arena_index], pp.index_byte_offset,
                                           std::as_bytes(std::span(row.asset->index_blob)
                                                             .subspan(pp.index_element_offset, pp.index_count))});
                        created_draws.push_back(pp.draw);
                        created_draw_arenas.push_back(arena_index);
                    }
                }
                const auto transfer_begin = std::chrono::steady_clock::now();
                const auto uploaded = device.apply_resource_changes({.buffer_uploads = uploads});
                output.value.geometry_transfer_us += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - transfer_begin).count());
                if (!uploaded) return {.error = uploaded.error};
                state.staged_buffer_upload_count += uploads.size();
                const std::size_t first_draw_index = state.geometry_draws.size();
                state.geometry_draws.insert(state.geometry_draws.end(), created_draws.begin(), created_draws.end());
                state.geometry_draw_arenas.insert(state.geometry_draw_arenas.end(),
                                                  created_draw_arenas.begin(), created_draw_arenas.end());
                state.geometry_arena_used_bytes[arena_index] = layout.cursor;
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
            output.value.geometry_arena_count = static_cast<std::uint32_t>(state.geometry_arenas.size());
            output.value.geometry_arena_reserved_bytes = state.geometry_arenas.size() * geometry_capacity;
            for (const std::uint64_t used : state.geometry_arena_used_bytes)
                output.value.geometry_arena_used_bytes += used;
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
            // 调试视图请求（R4/M3）：缺失 = off；mode 决定 debug pass 是否存在，
            // 折叠进 cache key（切换模式触发重编译）。
            const apps::debug_view_request* debug = packet.channels->find_state<apps::debug_view_request>();
            const uint32_t debug_mode = debug != nullptr && debug->mode <= 2U ? debug->mode : 0U;
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
                        glm::dot(position - camera_position, position - camera_position),
                        state.geometry_draw_arenas[draw_slot]});
                }
            }
            for (uint32_t group = 2; group < groups.size(); ++group)
                std::stable_sort(groups[group].begin(), groups[group].end(),
                    [](const auto& left, const auto& right)
                    { return left.distance_squared > right.distance_squared; });

            state.transform_rows.clear();
            state.commands.clear();
            // 命令按 (组, arena) 分段：组内候选按 arena 分桶（每段保持原距离序），
            // 每段一条 draw 行引用对应 arena 的 vertex/index 缓冲。单 arena 时每组
            // 恰一段，命令布局与单缓冲时代逐字节一致（多 arena 仅在几何 >256MB 时出现）。
            struct arena_segment
            {
                std::uint32_t arena = 0;
                std::uint64_t indirect_offset = 0;
                std::uint32_t draw_count = 0;
            };
            std::array<std::vector<arena_segment>, 4> group_segments;
            for (auto& segments : group_segments)
                segments.clear();
            for (uint32_t group = 0; group < groups.size(); ++group)
            {
                std::vector<arena_segment>& segments = group_segments[group];
                for (const auto& candidate : groups[group])
                {
                    if (segments.empty() || segments.back().arena != candidate.arena)
                    {
                        segments.push_back({.arena = candidate.arena,
                                            .indirect_offset = state.commands.size() *
                                                               sizeof(render_graph::indexed_indirect_command),
                                            .draw_count = 0});
                    }
                    const uint32_t draw_index = static_cast<uint32_t>(state.commands.size());
                    state.transform_rows.push_back({.model = candidate.model,
                                                    .metadata = {candidate.range.material_index, 0, 0, 0}});
                    state.commands.push_back({candidate.range.index_count, 1, candidate.range.first_index,
                                              candidate.range.vertex_offset, draw_index});
                    ++segments.back().draw_count;
                }
            }
            // 调试视图 quad 命令（追加在组命令之后；debug shader 不读 transform 表，
            // draw_index 指向一个占位 transform 行保持索引语义一致）。
            uint64_t debug_indirect_offset = 0;
            if (debug_mode != 0U)
            {
                const uint32_t draw_index = static_cast<uint32_t>(state.transform_rows.size());
                state.transform_rows.push_back({.model = glm::mat4{1.0F}, .metadata = {0, 0, 0, 0}});
                debug_indirect_offset = state.commands.size() * sizeof(render_graph::indexed_indirect_command);
                state.commands.push_back({6, 1, 0, 0, draw_index});
            }
            // shadow pass：group 0 的各 arena 段（不透明单面组，depth-only pipeline，
            // front cull 防 peter-panning；double-sided 材质不投影是常见简化）。
            state.draws.clear();
            for (const auto& segment : group_segments[0])
                state.draws.push_back({
                    .pipeline = state.shadow_pipeline,
                    .vertex_buffer = state.geometry_arenas[segment.arena],
                    .index_buffer = state.geometry_arenas[segment.arena],
                    .indirect_buffer = state.indirect,
                    .indirect_offset = segment.indirect_offset,
                    .draw_count = segment.draw_count,
                    .stride = sizeof(render_graph::indexed_indirect_command),
                });
            const std::uint32_t main_draw_begin = static_cast<std::uint32_t>(state.draws.size());
            // 主 pass：四组 × 各 arena 段（含透明，深度写入已按组关闭）
            for (uint32_t group = 0; group < 4; ++group)
                for (const auto& segment : group_segments[group])
                    state.draws.push_back({
                        .pipeline = state.pipelines[group],
                        .vertex_buffer = state.geometry_arenas[segment.arena],
                        .index_buffer = state.geometry_arenas[segment.arena],
                        .indirect_buffer = state.indirect,
                        .indirect_offset = segment.indirect_offset,
                        .draw_count = segment.draw_count,
                        .stride = sizeof(render_graph::indexed_indirect_command),
                    });
            const std::uint32_t main_draw_end = static_cast<std::uint32_t>(state.draws.size());
            // 调试视图 draw（R4/M3）：NDC quad 画进角落 inset（load=load 保留主输出）
            if (debug_mode != 0U)
                state.draws.push_back({
                    .pipeline = state.debug_pipeline,
                    .vertex_buffer = state.debug_quad,
                    .index_buffer = state.debug_quad,
                    .index_offset = debug_quad_vertex_bytes,
                    .indirect_buffer = state.indirect,
                    .indirect_offset = debug_indirect_offset,
                    .draw_count = 1,
                    .stride = sizeof(render_graph::indexed_indirect_command),
                });
            frame_uniform uniform{.view = camera_rows.front().view,
                                  .projection = camera_rows.front().projection};
            uniform.projection[1][1] *= -1.0F;
            // 帧通道消费点光源表（缺失 → 无点光，shader 只留平行光——编写者责任）。
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
            // 帧通道消费平行光（缺失 → intensity=0，frag 跳过阴影采样——编写者责任）。
            const apps::sun_light* sun = packet.channels->find_state<apps::sun_light>();
            light_uniform light{};
            if (sun != nullptr)
            {
                light.view_proj = sun->view_proj;
                light.direction = glm::vec4(sun->direction, 0.0F);
                light.color = glm::vec4(sun->color, 1.0F);
                light.intensity = sun->intensity;
            }
            std::array<render_graph::buffer_upload_row, 7> uploads{
                render_graph::buffer_upload_row{state.light_uniforms[environment.frame_index], 0,
                                                std::as_bytes(std::span(&light, 1))},
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
            const std::uint32_t upload_count = light_count > 0 ? 7U : 4U;
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
                .light_uniform_slot = state.light_uniform_slots[environment.frame_index],
                .transform_buffer_slot = state.transform_slot,
                .frame_uniform_slot = state.frame_slots[environment.frame_index],
                .material_buffer_slot = state.material_slot,
                .lights_buffer_slot = state.lights_slot,
                .light_count = light_count,
                .shadow_map_slot = state.shadow_map_slot,
                .shadow_sampler_slot = state.shadow_sampler_slot,
            };
            // 统一 push blob：主 push 在 [0, 32)，调试 push 在 [32, 52)——不同
            // 管线各按自己的切片读取（主 pass 32 字节、shadow 前 8 字节、debug 20 字节）。
            std::memcpy(state.push_blob.data(), &state.push, sizeof(state.push));
            const debug_push debug_state{
                .image_slot = state.shadow_map_slot,
                // 原始深度读取必须用独立普通采样器（R2：comparison sampler 禁非比较读取）
                .sampler_slot = state.debug_sampler_slot,
                .mode = debug_mode == 0U ? 0U : debug_mode - 1U,
                .near_plane = sun != nullptr ? sun->ortho_near : 0.1F,
                .far_plane = sun != nullptr ? sun->ortho_far : 240.0F,
            };
            std::memcpy(state.push_blob.data() + sizeof(state.push), &debug_state, sizeof(debug_state));
            // debug pass 会改变图结构（pass/附件/访问行），mode 折叠进 cache key 保证
            // 切换模式时触发重编译；同模式帧间 key 稳定，plan cache 照常命中。
            plan.cache_key = 0x474c544650425200ull ^ (static_cast<uint64_t>(debug_mode) << 32);
            state.frame_resources = {{
                // Geometry 行声明 arena 0（首个 arena）；后续 arena 经 draw 行句柄
                // 直接引用（持久 device-local 顶点/索引缓冲无 barrier 需求）。
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Geometry", .buffer = state.geometry_arenas.front()},
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Transforms", .buffer = state.transforms},
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "Indirect", .buffer = state.indirect},
                {.source = render_graph::frame_resource_source::persistent_image,
                 .name = "ShadowMap", .image = state.shadow_map},
                {.source = render_graph::frame_resource_source::swapchain_image, .name = "Swapchain"},
                {.source = render_graph::frame_resource_source::transient_image, .name = "Depth",
                 .image_description = {.fmt = render_graph::format::D32_SFLOAT,
                                       .extent = environment.extent,
                                       .usage = render_graph::image_usage::DEPTH_STENCIL_ATTACHMENT,
                                       .memory = render_graph::memory_domain::device_local,
                                       .lifetime = render_graph::resource_lifetime_class::transient}},
                // 调试视图 quad 几何（仅 debug pass 引用）
                {.source = render_graph::frame_resource_source::persistent_buffer,
                 .name = "DebugQuad", .buffer = state.debug_quad},
            }};
            state.frame_buffer_accesses = {{
                {{0}, render_graph::buffer_usage::VERTEX_BUFFER, render_graph::access_type::read},
                {{0}, render_graph::buffer_usage::INDEX_BUFFER, render_graph::access_type::read},
                {{2}, render_graph::buffer_usage::INDIRECT_BUFFER, render_graph::access_type::read},
                {{6}, render_graph::buffer_usage::VERTEX_BUFFER | render_graph::buffer_usage::INDEX_BUFFER,
                 render_graph::access_type::read},
            }};
            // 主 pass 以 shader 读方式采样阴影图（shadow pass 的 depth 写已在
            // attachment 事件中表达；编译器自动推导 write → read barrier）。
            // range.aspects=depth：D32 图的 barrier/视图必须用 DEPTH aspect。
            state.frame_image_accesses = {{
                {{3}, render_graph::image_usage::SAMPLED, render_graph::access_type::read,
                 {.aspects = render_graph::image_aspect::depth}},
                {{3}, render_graph::image_usage::SAMPLED, render_graph::access_type::read,
                 {.aspects = render_graph::image_aspect::depth}},
            }};
            state.frame_attachments = {{
                {.resource = {3}, .kind = render_graph::frame_attachment_kind::depth_stencil,
                 .store = render_graph::attachment_store_op::store,
                 .clear = {.depth = 1.0F}},
                {.resource = {4}, .kind = render_graph::frame_attachment_kind::color,
                 .clear = {.color = {0.03F, 0.04F, 0.08F, 1.0F}}},
                {.resource = {5}, .kind = render_graph::frame_attachment_kind::depth_stencil,
                 .store = render_graph::attachment_store_op::dont_care,
                 .clear = {.depth = 1.0F}},
                // 调试视图：复用 swapchain 颜色附件，load=load 保留主 pass 输出
                {.resource = {4}, .kind = render_graph::frame_attachment_kind::color,
                 .load = render_graph::attachment_load_op::load,
                 .store = render_graph::attachment_store_op::store},
            }};
            state.frame_passes = {{
                {.name = "ShadowPass", .kind = render_graph::pass_kind::raster,
                 .attachments = {0, 1}, .indexed_indirect_draws = {0, main_draw_begin},
                 .push_constant_size = sizeof(shadow_push),
                 .push_constant_stage_mask = render_graph::shader_stage_vertex_bit,
                 .area = {.width = shadow_map_size, .height = shadow_map_size}},
                {.name = "GltfSponzaPass", .kind = render_graph::pass_kind::raster,
                 .buffer_accesses = {0, 3}, .image_accesses = {0, 1},
                 .attachments = {1, 2},
                 .indexed_indirect_draws = {main_draw_begin,
                                            static_cast<std::uint32_t>(main_draw_end - main_draw_begin)},
                 .push_constant_size = sizeof(state.push),
                 .push_constant_stage_mask = render_graph::shader_stage_vertex_bit |
                                              render_graph::shader_stage_fragment_bit},
                {.name = "DebugViewPass", .kind = render_graph::pass_kind::raster,
                 .buffer_accesses = {3, 1}, .image_accesses = {1, 1},
                 .attachments = {3, 1},
                 .indexed_indirect_draws = {main_draw_end,
                                            static_cast<std::uint32_t>(state.draws.size() - main_draw_end)},
                 .push_constant_offset = static_cast<uint32_t>(sizeof(state.push)),
                 .push_constant_size = sizeof(debug_push),
                 .push_constant_stage_mask = render_graph::shader_stage_fragment_bit,
                 .area = {.x = 16, .y = 16, .width = 320, .height = 180}},
            }};
            plan.resources = state.frame_resources;
            // debug off 时只发布前两个 pass（DebugViewPass 行仍有效但被切片排除，
            // 保证默认路径与旧契约一致：每帧恰好 2 个 raster pass）。
            const std::size_t pass_count = debug_mode != 0U ? state.frame_passes.size() : 2U;
            plan.passes = std::span(state.frame_passes).first(pass_count);
            plan.buffer_accesses = state.frame_buffer_accesses;
            plan.image_accesses = state.frame_image_accesses;
            plan.attachments = state.frame_attachments;
            plan.push_constants = state.push_blob;
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
