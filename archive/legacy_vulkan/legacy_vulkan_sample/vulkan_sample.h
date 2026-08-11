#pragma once

#include <VkBootstrap.h>

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
#include "_old/vulkan_framebuffer.h"
#include "_old/vulkan_pipeline.h"
#include "_old/vulkan_renderpass.h"
#include "_old/vulkan_shader.h"
#include "_old/vulkan_synchronization.h"
#include "_templates/common.hpp"
#include "_vra/vra.h"
#include "utility/config_reader.h"

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

class vulkan_sample
{
public:
    vulkan_sample() = delete;
    vulkan_sample(engine_config config);
    ~vulkan_sample();

    void tick();
    void draw();

    void initialize();
    void set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data,
                               std::vector<uint32_t> indices,
                               std::vector<gltf::Vertex> vertices);
    void set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list);

    void set_window(interface::window* sdl_window) { this->window = sdl_window; }
    void set_camera_container(interface::camera_container* container) { camera_container = container; }
    void set_camera_index(size_t index) { camera_entity_index = index; }

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
    void* uniform_buffer_mapped_data;
    vk::Buffer uniform_buffer;
    VmaAllocator vma_allocator;
    VmaAllocation uniform_buffer_allocation;
    VmaAllocationInfo uniform_buffer_allocation_info;
    std::unique_ptr<vra::VraDataBatcher> vra_data_batcher;
    std::map<vra::BatchId, vra::VraDataBatcher::VraBatchHandle> uniform_batch_handle;
    std::vector<vra::ResourceId> uniform_buffer_id;

    // surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    // descriptor
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSetLayout descriptor_set_layout;
    std::vector<vk::DescriptorSet> descriptor_sets;
    vk::VertexInputBindingDescription vertex_input_binding_description;
    std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes;

    // vulkan helper members (old oop version)
    // TODO: remove these helper classes with dod version instead in the future
    interface::window* window                     = nullptr;
    interface::camera_container* camera_container = nullptr;
    size_t camera_entity_index                    = 0;
    std::unique_ptr<VulkanShaderHelper> vk_shader_helper;
    std::unique_ptr<VulkanRenderpassHelper> vk_renderpass_helper;
    std::unique_ptr<VulkanPipelineHelper> vk_pipeline_helper;
    std::unique_ptr<VulkanCommandBufferHelper> vk_command_buffer_helper;
    std::unique_ptr<VulkanFrameBufferHelper> vk_frame_buffer_helper;
    std::unique_ptr<VulkanSynchronizationHelper> vk_synchronization_helper;

    // --- Vulkan Initialization Steps ---

    void initialize_vulkan_hpp();
    void initialize_vulkan();

    void generate_frame_structs();
    bool create_instance();
    bool create_surface();
    bool create_physical_device();
    bool create_logical_device();
    bool create_swapchain();
    bool create_depth_resources();
    bool create_frame_buffer();
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

    void draw_frame();
    void resize_swapchain();
    bool record_command(uint32_t image_index, const std::string& command_buffer_id);
    void update_uniform_buffer(uint32_t current_frame_index);

    // -------------------------

    // --- Common Templates ---

    vk::Instance comm_vk_instance;
    vk::PhysicalDevice comm_vk_physical_device;
    vk::Device comm_vk_logical_device;
    vk::Queue comm_vk_graphics_queue;
    vk::Queue comm_vk_transfer_queue;
    vk::SwapchainKHR comm_vk_swapchain;
    templates::common::CommVkInstanceContext comm_vk_instance_context;
    templates::common::CommVkPhysicalDeviceContext comm_vk_physical_device_context;
    templates::common::CommVkLogicalDeviceContext comm_vk_logical_device_context;
    templates::common::CommVkSwapchainContext comm_vk_swapchain_context;

    std::vector<gltf::PerDrawCallData> per_draw_call_data_list;
    std::vector<uint32_t> indices;
    std::vector<gltf::Vertex> vertices;

    vk::Buffer local_buffer;
    vk::Buffer staging_buffer;

    VmaAllocation local_buffer_allocation;
    VmaAllocation staging_buffer_allocation;
    VmaAllocationInfo local_buffer_allocation_info;
    VmaAllocationInfo staging_buffer_allocation_info;

    vra::ResourceId vertex_buffer_id;
    vra::ResourceId index_buffer_id;
    vra::ResourceId staging_vertex_buffer_id;
    vra::ResourceId staging_index_buffer_id;

    std::map<vra::BatchId, vra::VraDataBatcher::VraBatchHandle> local_host_batch_handle;

    // 深度资源相关成员
    vk::Image depth_image          = VK_NULL_HANDLE;
    vk::DeviceMemory depth_memory  = VK_NULL_HANDLE;
    vk::ImageView depth_image_view = VK_NULL_HANDLE;
    vk::Format depth_format        = vk::Format::eD32Sfloat;

    // 创建深度资源
    vk::Format find_supported_depth_format();
};
