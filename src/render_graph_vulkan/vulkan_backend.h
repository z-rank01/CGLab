#pragma once

#include <deque>
#include <array>
#include <map>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <unordered_set>
#include <vulkan/vulkan.hpp>

#include "_interface/window.h"
#include "render_graph/system.h"
#include "render_graph/backend/vulkan/graph_backend.h"
#include "render_graph/backend/vulkan/runtime.h"
#include "swapchain_image_state.h"
#include "engine/render_backend.h"
#include "render_graph_vulkan/render_program.h"

struct vulkan_backend_config
{
    int width = 0;
    int height = 0;
    std::string application_name;
    std::string working_directory;
    uint8_t frame_count = 3;
    bool use_validation_layers = false;
};

struct mvp_matrix
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

struct object_push_constants
{
    std::uint32_t frame_uniform_slot = 0;
    std::uint32_t transform_buffer_slot = 0;
    std::uint32_t material_buffer_slot = 0;
};

struct alignas(16) gpu_transform_row
{
    glm::mat4 model{1.0F};
    glm::uvec4 metadata{};
};

struct alignas(16) gpu_material_row
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

using vulkan_frame_status = engine::frame_status;
using vulkan_run_statistics = engine::render_statistics;

// P2：一次运行时几何上传的登记/回收句柄（draws 供 registry 绘制，spans 供 arena 回收）
struct staged_geometry
{
    std::vector<engine::draw_range> draws;
    std::vector<render_graph::vk_buffer_slice> slices;
};

class vulkan_backend final
{
public:
    explicit vulkan_backend(engine::vulkan::render_program program);
    ~vulkan_backend();
    void request_resize() noexcept { resize_request = true; }
    [[nodiscard]] vulkan_run_statistics statistics() const noexcept { return run_statistics; }

    [[nodiscard]] engine::result<bool> initialize(interface::window& render_window,
                                                  const engine::backend_config& backend_config);
    [[nodiscard]] engine::result<engine::geometry_handle> upload_geometry(const engine::geometry_asset& asset);
    [[nodiscard]] engine::result<std::uint32_t> upload_materials(const engine::asset_database& asset);
    void retire_geometry(engine::geometry_handle handle);
    [[nodiscard]] engine::frame_status render(const engine::render_frame_packet& packet);
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t validation_error_count() const noexcept
    {
        return runtime ? runtime->validation_error_count() : 0;
    }
private:
#define FRAME_INDEX_TO_UNIFORM_BUFFER_ID(frame_index) ((frame_index) + 4)
    // engine members
    uint8_t frame_index = 0;
    bool resize_request = false;
    vulkan_backend_config config;

    // uniform data and buffer
    std::vector<mvp_matrix> mvp_matrices;
    std::vector<uint64_t> frame_submission_ids;
    void* uniform_buffer_mapped_data = nullptr;
    vk::Buffer uniform_buffer = VK_NULL_HANDLE;
    VkDeviceSize uniform_stride = sizeof(mvp_matrix);
    VmaAllocator vma_allocator = VK_NULL_HANDLE;
    render_graph::vk_buffer_resource_handle uniform_resource;
    std::vector<render_graph::vk_bindless_handle> frame_uniform_slots;

    // pipeline input rows
    vk::VertexInputBindingDescription vertex_input_binding_description;
    std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes;

    // vulkan helper members (old oop version)
    // TODO: remove these helper classes with dod version instead in the future
    interface::window* window                     = nullptr;
    std::array<render_graph::vk_pipeline_handle, 4> graphics_pipelines;

    // --- Vulkan Initialization Steps ---

    void initialize();
    [[nodiscard]] vulkan_frame_status tick();
    [[nodiscard]] vulkan_frame_status draw();
    void initialize_vulkan();

    bool create_pipeline();
    bool create_graph_allocator_bridge();
    bool create_uniform_buffers();

    bool create_render_graph_runtime();
    void sync_runtime_context_views();

    // ------------------------------------

    // --- Vulkan Draw Steps ---

    [[nodiscard]] vulkan_frame_status draw_frame();
    [[nodiscard]] bool resize_swapchain();
    bool record_command(uint32_t image_index, VkCommandBuffer command_buffer);
    bool build_render_graph(uint32_t image_index);
    void update_uniform_buffer(uint32_t current_frame_index);

    // --- P2 geometry arena / runtime upload ---
    bool create_geometry_arena();
    bool create_gpu_scene_tables();
    bool update_gpu_scene_tables();
    void collect_deferred_resources();
    [[nodiscard]] bool stage_runtime_geometry(const std::vector<engine::geometry_primitive>& primitives,
                                              staged_geometry& out);
    void retire_runtime_geometry(staged_geometry geometry);


    // -------------------------


    using frame_render_graph = render_graph::render_graph_system<render_graph::vk_backend>;
    std::unique_ptr<frame_render_graph> frame_graph;
    render_graph::image_handle rg_swapchain{};
    render_graph::image_handle rg_depth{};
    render_graph::buffer_handle rg_upload{};
    render_graph::buffer_handle rg_geometry{};
    render_graph::buffer_handle rg_uniform{};
    render_graph::buffer_handle rg_transforms{};
    render_graph::buffer_handle rg_indirect{};
    render_graph::buffer_handle rg_materials{};
    swapchain_image_state_tracker swapchain_image_states;
    vulkan_run_statistics run_statistics;
    // --- P2 scene system / geometry arena ---
    const engine::render_frame_packet* current_packet = nullptr;
    std::map<engine::geometry_handle, staged_geometry> geometry_allocations;
    engine::geometry_handle next_geometry_handle = 0;
    bool shutdown_requested = false;
    engine::vulkan::render_program render_program;

    // 运行时对象的 device-local 大块显存（bump 分配 + 空闲链表回收）
    vk::Buffer geometry_buffer = VK_NULL_HANDLE;
    render_graph::vk_buffer_resource_handle geometry_resource;
    render_graph::vk_buffer_resource_handle transform_resource;
    render_graph::vk_buffer_resource_handle indirect_resource;
    render_graph::vk_buffer_resource_handle material_resource;
    render_graph::vk_bindless_handle transform_buffer_slot;
    render_graph::vk_bindless_handle material_buffer_slot;
    vk::Buffer transform_buffer = VK_NULL_HANDLE;
    vk::Buffer indirect_buffer = VK_NULL_HANDLE;
    vk::Buffer material_buffer = VK_NULL_HANDLE;
    std::uint32_t indirect_draw_count = 0;
    std::array<std::uint32_t, 4> indirect_group_offsets{};
    std::array<std::uint32_t, 4> indirect_group_counts{};
    std::vector<gpu_material_row> material_rows;
    std::vector<render_graph::vk_image_resource_handle> texture_resources;
    std::vector<render_graph::vk_bindless_handle> texture_slots;
    std::vector<render_graph::vk_bindless_handle> sampler_slots;

    uint64_t submitted_frame = 1;
    uint64_t completed_frame = 0;
    std::unique_ptr<render_graph::vk_runtime> runtime;
};
