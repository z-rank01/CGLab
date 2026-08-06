#pragma once

#include <VkBootstrap.h>

#include <atomic>
#include <deque>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <unordered_set>

#include <gltf/gltf_data.h>
#include "_interface/camera_system.h"
#include "_interface/sdl_window.h" // For default implementation
#include "_interface/window.h"
#include "_old/vulkan_commandbuffer.h"
#include "_old/vulkan_pipeline.h"
#include "_old/vulkan_shader.h"
#include "_old/vulkan_synchronization.h"
#include "_templates/common.hpp"
#include "_vra/vra.h"
#include "utility/config_reader.h"
#include "render_graph/system.h"
#include "render_graph/vk_backend.h"
#include "swapchain_image_state.h"
#include "scene/scene_registry.h"

struct window_config
{
    int width;
    int height;
    std::string title;
};

struct engine_config
{
    window_config window_config;
    general_config general_config;
    uint8_t frame_count;
    bool use_validation_layers;
};

struct output_frame
{
    uint32_t image_index;
    std::string queue_id;
    std::string command_buffer_id;
    std::string image_available_semaphore_id;
    std::string render_finished_semaphore_id;
    std::string fence_id;
};

struct mvp_matrix
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

enum class vulkan_frame_status
{
    rendered,
    skipped,
    failed,
};

struct vulkan_run_statistics
{
    std::uint64_t upload_pass_executions = 0;
    std::uint64_t draw_pass_executions = 0;
    std::uint64_t presented_frames = 0;
};

// P2：一次运行时几何上传的登记/回收句柄（draws 供 registry 绘制，spans 供 arena 回收）
struct staged_geometry
{
    std::vector<scene::draw_range> draws;
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> vertex_spans; // 字节区间（offset, size）
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> index_spans;
};

// 单批次 staging 上传（staging buffer + copy regions）
struct runtime_upload
{
    vk::Buffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    std::vector<VkBufferCopy> vertex_copies; // staging -> arena vertex
    std::vector<VkBufferCopy> index_copies;  // staging -> arena index
    VkDeviceSize staging_size = 0;
};

// frames-in-flight 门控的 arena 区间回收
struct deferred_arena_free
{
    std::uint64_t gate_frame = 0;
    staged_geometry geometry;
};

class vulkan_sample
{
public:
    vulkan_sample() = delete;
    vulkan_sample(engine_config config);
    ~vulkan_sample();

    [[nodiscard]] vulkan_frame_status tick();
    [[nodiscard]] vulkan_frame_status draw();
    void request_resize() noexcept { resize_request = true; }
    [[nodiscard]] std::shared_ptr<std::atomic_uint32_t> validation_counter() const noexcept { return validation_errors; }
    [[nodiscard]] vulkan_run_statistics statistics() const noexcept { return run_statistics; }

    void initialize();
    void set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data,
                               std::vector<uint32_t> indices,
                               std::vector<gltf::Vertex> vertices);
    void set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list);

    void set_window(interface::window* sdl_window) { this->window = sdl_window; }
    void set_camera_container(interface::camera_container* container) { camera_container = container; }
    void set_camera_index(size_t index) { camera_entity_index = index; }

    // --- P2 scene system ---
    // 场景注册表由 app_sample 持有；渲染侧只读（仅主线程在帧边界外无并发写）。
    void set_scene_registry(scene::scene_registry* registry) { scene_objects = registry; }

    // 把一组图元（CPU 数据）staging 进 geometry arena：分配区间、创建 staging、入队待上传批次。
    // 成功后 draws/spans 回填，对象登记进 registry 后即可被 DrawPass 双轨绘制。
    // arena 容量不足返回 false（已分配区间会回滚）。
    [[nodiscard]] bool stage_runtime_geometry(const std::vector<gltf::PerDrawCallData>& primitives, staged_geometry& out);

    // 回收运行时几何区间（frames-in-flight 门控，不立即复用）。
    void retire_runtime_geometry(staged_geometry geometry);


private:
#define FRAME_INDEX_TO_UNIFORM_BUFFER_ID(frame_index) ((frame_index) + 4)
    // engine members
    uint8_t frame_index = 0;
    bool resize_request = false;
    engine_config config;
    std::vector<output_frame> output_frames;

    // mesh data members
    std::vector<gltf::PerMeshData> mesh_list;

    // uniform data and buffer
    std::vector<mvp_matrix> mvp_matrices;
    std::vector<uint64_t> frame_submission_ids;
    void* uniform_buffer_mapped_data = nullptr;
    vk::Buffer uniform_buffer = VK_NULL_HANDLE;
    VmaAllocator vma_allocator = VK_NULL_HANDLE;
    VmaAllocation uniform_buffer_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo uniform_buffer_allocation_info{};
    std::unique_ptr<vra::VraDataBatcher> vra_data_batcher;
    std::map<vra::BatchId, vra::VraDataBatcher::VraBatchHandle> uniform_batch_handle;
    std::vector<vra::ResourceId> uniform_buffer_id;

    // surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // descriptor
    vk::DescriptorPool descriptor_pool = VK_NULL_HANDLE;
    vk::DescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    std::vector<vk::DescriptorSet> descriptor_sets;
    vk::VertexInputBindingDescription vertex_input_binding_description;
    std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes;

    // vulkan helper members (old oop version)
    // TODO: remove these helper classes with dod version instead in the future
    interface::window* window                     = nullptr;
    interface::camera_container* camera_container = nullptr;
    size_t camera_entity_index                    = 0;
    std::unique_ptr<VulkanShaderHelper> vk_shader_helper;
    std::unique_ptr<VulkanPipelineHelper> vk_pipeline_helper;
    std::unique_ptr<VulkanCommandBufferHelper> vk_command_buffer_helper;
    std::unique_ptr<VulkanSynchronizationHelper> vk_synchronization_helper;

    // --- Vulkan Initialization Steps ---

    void initialize_vulkan_hpp();
    void initialize_vulkan();

    void generate_frame_structs();
    bool create_instance();
    bool create_debug_messenger();
    void destroy_debug_messenger() noexcept;
    static VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_types,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
        void* user_data);
    bool create_surface();
    bool create_physical_device();
    bool create_logical_device();
    bool create_swapchain();
    bool create_pipeline();
    bool create_command_pool();
    bool create_and_write_descriptor_relatives();
    bool create_vma_vra_objects();
    void create_drawcall_list_buffer();
    bool create_uniform_buffers();

    bool allocate_per_frame_command_buffer();
    bool create_synchronization_objects();

    // ------------------------------------

    // --- Vulkan Draw Steps ---

    [[nodiscard]] vulkan_frame_status draw_frame();
    [[nodiscard]] bool resize_swapchain();
    bool record_command(uint32_t image_index, const std::string& command_buffer_id);
    bool build_render_graph(uint32_t image_index);
    void update_uniform_buffer(uint32_t current_frame_index);

    // --- P2 geometry arena / runtime upload ---
    bool create_geometry_arena();
    void collect_deferred_resources();
    static void destroy_runtime_upload(VmaAllocator allocator, runtime_upload& upload) noexcept;


    // -------------------------

    // --- Common Templates ---

    vk::Instance comm_vk_instance = VK_NULL_HANDLE;
    vk::PhysicalDevice comm_vk_physical_device = VK_NULL_HANDLE;
    vk::Device comm_vk_logical_device = VK_NULL_HANDLE;
    vk::Queue comm_vk_graphics_queue = VK_NULL_HANDLE;
    vk::Queue comm_vk_transfer_queue = VK_NULL_HANDLE;
    vk::SwapchainKHR comm_vk_swapchain = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    std::shared_ptr<std::atomic_uint32_t> validation_errors = std::make_shared<std::atomic_uint32_t>(0);
    templates::common::CommVkInstanceContext comm_vk_instance_context;
    templates::common::CommVkPhysicalDeviceContext comm_vk_physical_device_context;
    templates::common::CommVkLogicalDeviceContext comm_vk_logical_device_context;
    templates::common::CommVkSwapchainContext comm_vk_swapchain_context;

    std::vector<gltf::PerDrawCallData> per_draw_call_data_list;
    std::vector<uint32_t> indices;
    std::vector<gltf::Vertex> vertices;

    vk::Buffer local_buffer = VK_NULL_HANDLE;
    vk::Buffer staging_buffer = VK_NULL_HANDLE;

    VmaAllocation local_buffer_allocation = VK_NULL_HANDLE;
    VmaAllocation staging_buffer_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo local_buffer_allocation_info{};
    VmaAllocationInfo staging_buffer_allocation_info{};

    vra::ResourceId vertex_buffer_id;
    vra::ResourceId index_buffer_id;
    vra::ResourceId staging_vertex_buffer_id;
    vra::ResourceId staging_index_buffer_id;

    std::map<vra::BatchId, vra::VraDataBatcher::VraBatchHandle> local_host_batch_handle;

    vk::Format depth_format        = vk::Format::eD32Sfloat;

    using frame_render_graph = render_graph::render_graph_system<render_graph::vk_backend>;
    std::unique_ptr<frame_render_graph> frame_graph;
    render_graph::image_handle rg_swapchain{};
    render_graph::image_handle rg_depth{};
    render_graph::buffer_handle rg_staging{};
    render_graph::buffer_handle rg_local{};
    render_graph::buffer_handle rg_uniform{};
    bool mesh_upload_pending = true;
    swapchain_image_state_tracker swapchain_image_states;
    vulkan_run_statistics run_statistics;
    // --- P2 scene system / geometry arena ---
    scene::scene_registry* scene_objects = nullptr;

    // 运行时对象的 device-local 大块显存（bump 分配 + 空闲链表回收）
    vk::Buffer arena_vertex_buffer = VK_NULL_HANDLE;
    vk::Buffer arena_index_buffer  = VK_NULL_HANDLE;
    VmaAllocation arena_vertex_allocation = VK_NULL_HANDLE;
    VmaAllocation arena_index_allocation  = VK_NULL_HANDLE;
    VmaAllocationInfo arena_vertex_allocation_info{};
    VmaAllocationInfo arena_index_allocation_info{};
    VkDeviceSize arena_vertex_cursor = 0;
    VkDeviceSize arena_index_cursor  = 0;
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> arena_vertex_free_list;
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> arena_index_free_list;

    // 待并入下一帧图变体的上传批次；提交后按 completed_frame 延迟销毁 staging
    std::vector<runtime_upload> queued_uploads;
    std::deque<std::pair<std::uint64_t, runtime_upload>> in_flight_uploads;
    bool runtime_upload_pending = false;
    std::uint64_t upload_serial = 0;
    std::vector<deferred_arena_free> deferred_frees;

    render_graph::buffer_handle rg_arena_vertex{};
    render_graph::buffer_handle rg_arena_index{};
    std::vector<render_graph::buffer_handle> rg_runtime_stagings;

    uint64_t submitted_frame = 1;
    uint64_t completed_frame = 0;
};
