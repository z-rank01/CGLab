#include "app_sample.h"

#include <stdexcept>

#include "_interface/sdl_window.h" // For default implementation

app_sample::app_sample(engine_config config) : general_config(std::move(config))
{
    vulkan_instance = std::make_unique<vulkan_sample>(general_config);
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
}

void app_sample::tick()
{
    interface::input_event event{};
    while (!window->should_close())
    {
        // calculate delta time
        auto current_time = std::chrono::high_resolution_clock::now();
        delta_time        = std::chrono::duration<float>(current_time - last_frame_time).count();
        last_frame_time   = current_time;

        window->tick(event);
        interface::tick(camera_container, camera_update_context, event, delta_time);
        vulkan_instance->tick();
    }
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
