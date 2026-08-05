#include "app_sample.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#include "_interface/sdl_window.h" // For default implementation
#include "utility/logger.h"

namespace
{
    // 遥测推送间隔（秒）：10 Hz，避免每帧全量推 JSON
    constexpr float telemetry_interval_seconds = 0.1F;
}

app_sample::app_sample(engine_config config, control_plane::control_plane_config ui_config)
    : general_config(std::move(config)), ui_config(ui_config)
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

    // control plane：失败降级为无 UI 运行，不影响渲染
    if (ui_config.enabled)
    {
        control_plane = std::make_unique<control_plane::control_plane_server>();
        if (!control_plane->start(ui_config))
        {
            Logger::LogWarning("Control plane unavailable; continuing without Web UI backend.");
            control_plane.reset();
        }
    }

    // setup vulkan sample
    vulkan_instance->set_window(window.get());
    vulkan_instance->set_camera_container(&camera_container);
    vulkan_instance->set_camera_index(camera_entity_index);
    vulkan_instance->initialize();
    input_events.reserve(32);
    last_frame_time = std::chrono::high_resolution_clock::now();
}

void app_sample::handle_control_plane_commands()
{
    if (!control_plane)
    {
        return;
    }
    for (const control_plane::engine_command& command : control_plane->drain_commands())
    {
        using control_plane::command_kind;
        switch (command.kind)
        {
        case command_kind::echo:
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_result(command.id, {{"message", command.message}}));
            break;
        }
        case command_kind::frame_pause:
        {
            frame_paused = true;
            control_plane->post_response(command.client_id,
                                         control_plane::make_result(command.id, {{"paused", true}}));
            break;
        }
        case command_kind::frame_resume:
        {
            frame_paused = false;
            control_plane->post_response(command.client_id,
                                         control_plane::make_result(command.id, {{"paused", false}}));
            break;
        }
        case command_kind::frame_step:
        {
            pending_frame_steps += command.step_count;
            control_plane->post_response(command.client_id,
                                         control_plane::make_result(command.id, {{"stepped", command.step_count}}));
            break;
        }
        }
    }
}

void app_sample::publish_frame_telemetry()
{
    if (!control_plane)
    {
        return;
    }
    telemetry_accumulator += delta_time;
    if (telemetry_accumulator < telemetry_interval_seconds)
    {
        return;
    }
    telemetry_accumulator = 0.0F;

    // 帧时间指数滑动平均，抑制单帧抖动
    smoothed_frame_time = smoothed_frame_time * 0.9F + delta_time * 0.1F;
    const float smoothed_fps = smoothed_frame_time > 0.0F ? 1.0F / smoothed_frame_time : 0.0F;

    const vulkan_run_statistics stats = vulkan_instance->statistics();
    control_plane->publish(control_plane::make_notification(
        "telemetry.frame",
        {
            {"fps", smoothed_fps},
            {"frame_time_ms", smoothed_frame_time * 1000.0F},
            {"presented_frames", stats.presented_frames},
            {"draw_pass_executions", stats.draw_pass_executions},
            {"upload_pass_executions", stats.upload_pass_executions},
            {"validation_errors", validation_error_count()},
            {"paused", frame_paused},
        }));
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

        // 帧边界消费控制平面命令（pause/resume/step/echo）
        handle_control_plane_commands();

        interface::tick(camera_container, camera_update_context, input_events, delta_time);

        // 暂停语义：事件泵与 UI 通道保持存活，仅跳过渲染；
        // pending_frame_steps 允许逐帧步进。
        bool render_this_frame = !frame_paused;
        if (pending_frame_steps > 0)
        {
            --pending_frame_steps;
            render_this_frame = true;
        }
        if (!render_this_frame)
        {
            publish_frame_telemetry();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }

        const vulkan_frame_status status = vulkan_instance->tick();
        if (status == vulkan_frame_status::failed)
        {
            return false;
        }
        if (status == vulkan_frame_status::skipped)
        {
            publish_frame_telemetry();
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
        publish_frame_telemetry();
    }
    return true;
}

void app_sample::shutdown() noexcept
{
    if (vulkan_instance)
    {
        run_statistics = vulkan_instance->statistics();
    }
    control_plane.reset();
    vulkan_instance.reset();
    window.reset();
}

std::uint32_t app_sample::validation_error_count() const noexcept
{
    return validation_errors ? validation_errors->load(std::memory_order_relaxed) : 0;
}

void app_sample::set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data,
                                       std::vector<uint32_t> indices, std::vector<gltf::Vertex> vertices)
{
    vulkan_instance->set_vertex_index_data(std::move(per_draw_call_data), std::move(indices), std::move(vertices));
}

void app_sample::set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list)
{
    vulkan_instance->set_mesh_list(mesh_list);
}
