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

#include "_interface/window.h"
#include "_old/vulkan_commandbuffer.h"
#include "_old/vulkan_pipeline.h"
#include "_old/vulkan_shader.h"
#include "_old/vulkan_synchronization.h"
#include "_templates/common.hpp"
#include "_vra/vra.h"
#include "render_graph/system.h"
#include "render_graph/vk_backend.h"
#include "swapchain_image_state.h"
#include "engine/render_backend.h"
#include "renderer/vulkan/render_program.h"

struct vulkan_backend_config
{
    int width = 0;
    int height = 0;
    std::string application_name;
    std::string working_directory;
    uint8_t frame_count = 3;
    bool use_validation_layers = false;
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

using vulkan_frame_status = engine::frame_status;
using vulkan_run_statistics = engine::render_statistics;

// P2：一次运行时几何上传的登记/回收句柄（draws 供 registry 绘制，spans 供 arena 回收）
struct staged_geometry
{
    std::vector<engine::draw_range> draws;
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

class vulkan_backend final : public engine::render_backend
{
public:
    explicit vulkan_backend(engine::vulkan::render_program program);
    ~vulkan_backend() override;
    void request_resize() noexcept override { resize_request = true; }
    [[nodiscard]] vulkan_run_statistics statistics() const noexcept override { return run_statistics; }

    [[nodiscard]] engine::result<bool> initialize(interface::window& render_window,
                                                  const engine::backend_config& backend_config) override;
    [[nodiscard]] engine::result<engine::geometry_handle> upload_geometry(const engine::geometry_asset& asset) override;
    void retire_geometry(engine::geometry_handle handle) override;
    [[nodiscard]] engine::frame_status render(const engine::render_snapshot& snapshot) override;
    void shutdown() noexcept override;
    [[nodiscard]] std::uint32_t validation_error_count() const noexcept override
    {
        return validation_errors->load(std::memory_order_relaxed);
    }
private:
#define FRAME_INDEX_TO_UNIFORM_BUFFER_ID(frame_index) ((frame_index) + 4)
    // engine members
    uint8_t frame_index = 0;
    bool resize_request = false;
    vulkan_backend_config config;
    std::vector<output_frame> output_frames;

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
    std::unique_ptr<VulkanShaderHelper> vk_shader_helper;
    std::unique_ptr<VulkanPipelineHelper> vk_pipeline_helper;
    std::unique_ptr<VulkanCommandBufferHelper> vk_command_buffer_helper;
    std::unique_ptr<VulkanSynchronizationHelper> vk_synchronization_helper;

    // --- Vulkan Initialization Steps ---

    void initialize();
    [[nodiscard]] vulkan_frame_status tick();
    [[nodiscard]] vulkan_frame_status draw();
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
    [[nodiscard]] bool stage_runtime_geometry(const std::vector<engine::geometry_primitive>& primitives,
                                              staged_geometry& out);
    void retire_runtime_geometry(staged_geometry geometry);


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

    std::vector<uint32_t> indices;
    std::vector<engine::vertex> vertices;

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
    const engine::render_snapshot* current_snapshot = nullptr;
    std::map<engine::geometry_handle, staged_geometry> geometry_allocations;
    engine::geometry_handle next_geometry_handle = 0;
    bool shutdown_requested = false;
    engine::vulkan::render_program render_program;

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
