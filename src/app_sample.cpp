#include "app_sample.h"

#include <stdexcept>
#include <thread>
#include <utility>

#include "_interface/sdl_window.h" // For default implementation

app_sample::app_sample(engine_config config) : general_config(std::move(config))
{
    vulkan_instance = std::make_unique<vulkan_sample>(general_config);
    validation_errors = vulkan_instance->validation_counter();
}

app_sample::~app_sample()
{
    shutdown();
}

void app_sample::initialize()
{
    // initialize sdl window
    window = std::make_unique<interface::sdl_window>();
    interface::window_config win_config;
    win_config.title  = general_config.window_config.title;
    win_config.width  = general_config.window_config.width;
    win_config.height = general_config.window_config.height;
    if (!window->open(win_config))
    {
        throw std::runtime_error("Failed to open window.");
    }

    // initialize camera
    camera_entity_index = camera_container.add_camera();

    // setup vulkan sample
    vulkan_instance->set_window(window.get());
    vulkan_instance->set_camera_container(&camera_container);
    vulkan_instance->set_camera_index(camera_entity_index);
    vulkan_instance->initialize();
    input_events.reserve(32);
    last_frame_time = std::chrono::high_resolution_clock::now();
}

bool app_sample::tick(std::optional<std::uint64_t> frame_limit)
{
    std::uint64_t rendered_frames = 0;
    while (!window->should_close())
    {
        // calculate delta time
        auto current_time = std::chrono::high_resolution_clock::now();
        delta_time        = std::chrono::duration<float>(current_time - last_frame_time).count();
        last_frame_time   = current_time;

        window->poll_events(input_events);
        for (const interface::input_event& event : input_events)
        {
            if (event.type == interface::event_type::resize)
            {
                vulkan_instance->request_resize();
            }
        }
        if (window->should_close())
        {
            return true;
        }
        interface::tick(camera_container, camera_update_context, input_events, delta_time);
        const vulkan_frame_status status = vulkan_instance->tick();
        if (status == vulkan_frame_status::failed)
        {
            return false;
        }
        if (status == vulkan_frame_status::skipped)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        if (status == vulkan_frame_status::rendered)
        {
            ++rendered_frames;
            if (frame_limit && rendered_frames >= *frame_limit)
            {
                return true;
            }
        }
    }
    return true;
}

void app_sample::shutdown() noexcept
{
    if (vulkan_instance)
    {
        run_statistics = vulkan_instance->statistics();
    }
    vulkan_instance.reset();
    window.reset();
}

std::uint32_t app_sample::validation_error_count() const noexcept
{
    return validation_errors ? validation_errors->load(std::memory_order_relaxed) : 0;
}

void app_sample::set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data,
                                       std::vector<uint32_t> indices,
                                       std::vector<gltf::Vertex> vertices)
{
    vulkan_instance->set_vertex_index_data(std::move(per_draw_call_data), std::move(indices), std::move(vertices));
}

void app_sample::set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list)
{
    vulkan_instance->set_mesh_list(mesh_list);
}
