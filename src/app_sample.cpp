#include "app_sample.h"

#include <gltf/gltf_loader.h>
#include <gltf/gltf_parser.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

#include "_interface/sdl_window.h" // For default implementation
#include "utility/logger.h"

namespace
{
    // 遥测推送间隔（秒）：10 Hz，避免每帧全量推 JSON
    constexpr float telemetry_interval_seconds = 0.1F;
    // 异步加载队列上限：溢出直接报错，避免 worker 无限积压
    constexpr std::size_t max_pending_loads = 64;
} // namespace

app_sample::app_sample(engine_config config, control_plane::control_plane_config ui_config) : general_config(std::move(config)), ui_config(ui_config)
{
    vulkan_instance   = std::make_unique<vulkan_sample>(general_config);
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
    vulkan_instance->set_scene_registry(&scene_registry);
    vulkan_instance->initialize();

    // P2：启动异步加载 worker（仅解析 glTF，纯 CPU，不触碰 Vulkan）
    load_worker = std::thread(&app_sample::load_worker_loop, this);

    input_events.reserve(32);
    last_frame_time = std::chrono::high_resolution_clock::now();
}

// --- P2 异步加载管线 ---

void app_sample::enqueue_load(load_job job)
{
    {
        std::lock_guard lock(load_mutex);
        if (load_queue.size() >= max_pending_loads)
        {
            if (control_plane)
            {
                control_plane->post_response(job.client_id, control_plane::make_error(job.rpc_id, -32000, "Load queue is full"));
            }
            return;
        }
        load_queue.push_back(std::move(job));
    }
    load_cv.notify_one();
}

void app_sample::load_worker_loop() noexcept
{
    for (;;)
    {
        load_job job;
        {
            std::unique_lock lock(load_mutex);
            load_cv.wait(lock, [this] { return load_worker_stop || !load_queue.empty(); });
            if (load_worker_stop && load_queue.empty())
            {
                return;
            }
            job = std::move(load_queue.front());
            load_queue.pop_front();
        }

        load_result result;
        result.client_id = job.client_id;
        result.rpc_id    = job.rpc_id;
        try
        {
            // 相对路径以 working_directory 为基准解析
            std::filesystem::path path(job.path);
            if (path.is_relative())
            {
                path = std::filesystem::path(general_config.general_config.working_directory) / path;
            }
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("Asset file does not exist: " + path.string());
            }

            gltf::GltfLoader loader;
            auto asset = loader(path.string());
            gltf::GltfParser parser;
            result.primitives = parser(asset, gltf::RequestDrawCallList{});

            // 节点变换烘焙进顶点（worker 侧 CPU 完成），对象级变换走 push constant
            glm::vec3 bounds_min(std::numeric_limits<float>::max());
            glm::vec3 bounds_max(std::numeric_limits<float>::lowest());
            for (gltf::PerDrawCallData& primitive : result.primitives)
            {
                const glm::mat4& node_transform = primitive.transform;
                for (gltf::Vertex& vertex : primitive.vertices)
                {
                    vertex.position = glm::vec3(node_transform * glm::vec4(vertex.position, 1.0F));
                    bounds_min      = glm::min(bounds_min, vertex.position);
                    bounds_max      = glm::max(bounds_max, vertex.position);
                }
            }
            if (result.primitives.empty())
            {
                throw std::runtime_error("Asset contains no drawable primitives: " + path.string());
            }
            result.bounds    = scene::aabb{bounds_min, bounds_max};
            result.name      = path.filename().string();
            result.succeeded = true;
        }
        catch (const std::exception& error)
        {
            result.succeeded = false;
            result.error     = error.what();
        }

        {
            std::lock_guard lock(load_mutex);
            load_results.push_back(std::move(result));
        }
    }
}

void app_sample::drain_completed_loads()
{
    std::deque<load_result> completed;
    {
        std::lock_guard lock(load_mutex);
        completed.swap(load_results);
    }
    for (load_result& result : completed)
    {
        if (!control_plane)
        {
            continue;
        }
        if (!result.succeeded)
        {
            control_plane->post_response(result.client_id,
                                         control_plane::make_error(result.rpc_id, -32000, "scene.load_asset failed: " + result.error));
            continue;
        }

        staged_geometry geometry;
        if (!vulkan_instance->stage_runtime_geometry(result.primitives, geometry))
        {
            control_plane->post_response(result.client_id,
                                         control_plane::make_error(result.rpc_id, -32000, "scene.load_asset failed: geometry arena staging error"));
            continue;
        }

        const scene::object_id id = scene_registry.register_object(result.name, result.bounds, geometry.draws);
        if (runtime_geometry_slots.size() <= id)
        {
            runtime_geometry_slots.resize(id + 1);
        }
        runtime_geometry_slots[id] = std::move(geometry);
        Logger::LogInfo("Loaded runtime asset \"" + result.name + "\" as scene object " + std::to_string(id));
        control_plane->post_response(result.client_id,
                                     control_plane::make_result(result.rpc_id,
                                                                {{"id", id},
                                                                 {"name", result.name},
                                                                 {"primitives", result.primitives.size()},
                                                                 {"bounds",
                                                                  {{"min", {result.bounds.min.x, result.bounds.min.y, result.bounds.min.z}},
                                                                   {"max", {result.bounds.max.x, result.bounds.max.y, result.bounds.max.z}}}}}));
    }
}

void app_sample::stop_load_worker() noexcept
{
    {
        std::lock_guard lock(load_mutex);
        load_worker_stop = true;
    }
    load_cv.notify_one();
    if (load_worker.joinable())
    {
        load_worker.join();
    }
}

// --- P2 场景命令 ---

void app_sample::handle_scene_command(const control_plane::engine_command& command)
{
    using control_plane::command_kind;
    switch (command.kind)
    {
    case command_kind::scene_load_asset:
    {
        enqueue_load(load_job{.path = command.params["path"].get<std::string>(), .client_id = command.client_id, .rpc_id = command.id});
        break;
    }
    case command_kind::scene_unload:
    {
        const scene::object_id id         = command.params["id"].get<scene::object_id>();
        const scene::scene_object* object = scene_registry.find(id);
        if (object == nullptr)
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32602, "Unknown scene object id " + std::to_string(id)));
            break;
        }
        if (object->read_only)
        {
            control_plane->post_response(
                command.client_id,
                control_plane::make_error(command.id, -32602, "Scene object " + std::to_string(id) + " is read-only (startup asset)"));
            break;
        }
        if (!scene_registry.unload(id))
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32602, "Failed to unload scene object " + std::to_string(id)));
            break;
        }
        if (id < runtime_geometry_slots.size() && runtime_geometry_slots[id].has_value())
        {
            vulkan_instance->retire_runtime_geometry(std::move(*runtime_geometry_slots[id]));
            runtime_geometry_slots[id].reset();
        }
        control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"unloaded", true}, {"id", id}}));
        break;
    }
    case command_kind::scene_set_visibility:
    {
        const scene::object_id id = command.params["id"].get<scene::object_id>();
        const bool visible        = command.params["visible"].get<bool>();
        if (!scene_registry.set_visibility(id, visible))
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32602, "Unknown scene object id " + std::to_string(id)));
            break;
        }
        control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"id", id}, {"visible", visible}}));
        break;
    }
    case command_kind::scene_set_transform:
    {
        const scene::object_id id         = command.params["id"].get<scene::object_id>();
        const scene::scene_object* object = scene_registry.find(id);
        if (object == nullptr)
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32602, "Unknown scene object id " + std::to_string(id)));
            break;
        }
        scene::object_transform transform = object->transform;
        const auto read_vec3              = [&command](const char* field, glm::vec3& out)
        {
            if (!command.params.contains(field))
            {
                return;
            }
            const auto& value = command.params[field];
            out               = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
        };
        read_vec3("position", transform.position);
        read_vec3("rotation", transform.rotation_deg);
        read_vec3("scale", transform.scale);
        (void)scene_registry.set_transform(id, transform);
        control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"id", id}}));
        break;
    }
    case command_kind::scene_select:
    {
        if (command.params["id"].is_null())
        {
            (void)scene_registry.set_selected(scene::invalid_object_id);
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"selected", nullptr}}));
            break;
        }
        const scene::object_id id = command.params["id"].get<scene::object_id>();
        if (!scene_registry.set_selected(id))
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32602, "Unknown scene object id " + std::to_string(id)));
            break;
        }
        control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"selected", id}}));
        break;
    }
    case command_kind::scene_list:
    {
        control_plane->post_response(command.client_id, control_plane::make_result(command.id, current_scene_state()));
        break;
    }
    default:
        break;
    }
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
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"message", command.params["message"]}}));
            break;
        }
        case command_kind::frame_pause:
        {
            frame_paused = true;
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"paused", true}}));
            break;
        }
        case command_kind::frame_resume:
        {
            frame_paused = false;
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"paused", false}}));
            break;
        }
        case command_kind::frame_step:
        {
            const std::uint32_t steps = command.params["count"].get<std::uint32_t>();
            pending_frame_steps += steps;
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"stepped", steps}}));
            break;
        }
        case command_kind::camera_set_mode:
        {
            const std::string mode = command.params["mode"].get<std::string>();
            interface::set_camera_mode(
                camera_container, camera_entity_index, mode == "orbit" ? interface::camera_mode::orbit : interface::camera_mode::fly);
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"mode", mode}}));
            break;
        }
        case command_kind::camera_set_params:
        {
            interface::camera_config& config       = camera_container.configs[camera_entity_index];
            interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
            const nlohmann::json& params           = command.params;
            if (params.contains("fov"))
            {
                transform.current_zoom = params["fov"].get<float>();
                transform.dirty        = true;
            }
            if (params.contains("movement_speed"))
            {
                config.movement_speed = params["movement_speed"].get<float>();
            }
            if (params.contains("mouse_sensitivity"))
            {
                config.mouse_sensitivity = params["mouse_sensitivity"].get<float>();
            }
            if (params.contains("zoom_speed"))
            {
                config.zoom_speed = params["zoom_speed"].get<float>();
            }
            if (params.contains("orbit_distance"))
            {
                transform.orbit_distance = params["orbit_distance"].get<float>();
                transform.dirty          = true;
            }
            if (params.contains("near_plane"))
            {
                config.near_plane = params["near_plane"].get<float>();
            }
            if (params.contains("far_plane"))
            {
                config.far_plane = params["far_plane"].get<float>();
            }
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"applied", params}}));
            break;
        }
        case command_kind::camera_get_state:
        {
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, current_camera_state()));
            break;
        }
        case command_kind::camera_bookmark_save:
        {
            const std::uint32_t slot = command.params["slot"].get<std::uint32_t>();
            const bool saved         = interface::save_bookmark(camera_container, camera_entity_index, slot);
            control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"saved", saved}, {"slot", slot}}));
            break;
        }
        case command_kind::camera_bookmark_goto:
        {
            const std::uint32_t slot = command.params["slot"].get<std::uint32_t>();
            const bool started       = interface::goto_bookmark(camera_container, camera_entity_index, slot);
            if (started)
            {
                control_plane->post_response(command.client_id, control_plane::make_result(command.id, {{"goto", true}, {"slot", slot}}));
            }
            else
            {
                control_plane->post_response(command.client_id,
                                             control_plane::make_error(command.id, -32602, "Bookmark slot " + std::to_string(slot) + " is empty"));
            }
            break;
        }
        default:
        {
            // scene.* 命令集中在独立处理器
            handle_scene_command(command);
            break;
        }
        }
    }
}

nlohmann::json app_sample::current_camera_state() const
{
    const interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
    const interface::camera_config& config       = camera_container.configs[camera_entity_index];
    const interface::camera_bookmarks& bookmarks = camera_container.bookmarks[camera_entity_index];
    return {
        {"mode", transform.mode == interface::camera_mode::orbit ? "orbit" : "fly"},
        {"position", {transform.position.x, transform.position.y, transform.position.z}},
        {"yaw", transform.yaw},
        {"pitch", transform.pitch},
        {"fov", transform.current_zoom},
        {"orbit_distance", transform.orbit_distance},
        {"focus_point", {transform.focus_point.x, transform.focus_point.y, transform.focus_point.z}},
        {"movement_speed", config.movement_speed},
        {"mouse_sensitivity", config.mouse_sensitivity},
        {"zoom_speed", config.zoom_speed},
        {"near_plane", config.near_plane},
        {"far_plane", config.far_plane},
        {"bookmarks_valid", bookmarks.valid},
        {"blending", camera_container.blends[camera_entity_index].active},
    };
}

nlohmann::json app_sample::current_scene_state() const
{
    nlohmann::json objects = nlohmann::json::array();
    for (const scene::scene_object* object : scene_registry.objects())
    {
        objects.push_back({
            {"id", object->id},
            {"name", object->name},
            {"visible", object->visible},
            {"read_only", object->read_only},
            {"draw_count", object->draws.size()},
            {"bounds",
             {{"min", {object->local_bounds.min.x, object->local_bounds.min.y, object->local_bounds.min.z}},
              {"max", {object->local_bounds.max.x, object->local_bounds.max.y, object->local_bounds.max.z}}}},
            {"transform",
             {{"position", {object->transform.position.x, object->transform.position.y, object->transform.position.z}},
              {"rotation", {object->transform.rotation_deg.x, object->transform.rotation_deg.y, object->transform.rotation_deg.z}},
              {"scale", {object->transform.scale.x, object->transform.scale.y, object->transform.scale.z}}}},
        });
    }
    const scene::object_id selected = scene_registry.selected();
    return {
        {"revision", scene_registry.revision()},
        {"selected", selected == scene::invalid_object_id ? nlohmann::json(nullptr) : nlohmann::json(selected)},
        {"objects", std::move(objects)},
    };
}

void app_sample::publish_scene_telemetry_if_changed()
{
    if (!control_plane)
    {
        return;
    }
    const std::uint64_t revision = scene_registry.revision();
    if (revision == last_published_scene_revision)
    {
        return;
    }
    last_published_scene_revision = revision;
    control_plane->publish(control_plane::make_notification("telemetry.scene", current_scene_state()));
}

// fly 模式左键拾取：窗口坐标 → 相机射线 → registry AABB pick
void app_sample::try_pick_object(float x, float y)
{
    int width  = 0;
    int height = 0;
    window->get_extent(width, height);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
    const interface::camera_config& config       = camera_container.configs[camera_entity_index];

    // 注意使用未做 Vulkan Y 翻转的投影做反投影（拾取在标准 NDC 约定下进行）
    const float ndc_x             = (2.0F * x) / static_cast<float>(width) - 1.0F;
    const float ndc_y             = 1.0F - (2.0F * y) / static_cast<float>(height);
    const glm::mat4 view          = interface::get_view_matrix(transform);
    const glm::mat4 proj          = interface::get_projection_matrix(transform, config);
    const glm::mat4 inv_view_proj = glm::inverse(proj * view);
    const glm::vec4 near_point    = inv_view_proj * glm::vec4(ndc_x, ndc_y, -1.0F, 1.0F);
    const glm::vec4 far_point     = inv_view_proj * glm::vec4(ndc_x, ndc_y, 1.0F, 1.0F);
    const glm::vec3 near_world    = glm::vec3(near_point) / near_point.w;
    const glm::vec3 far_world     = glm::vec3(far_point) / far_point.w;

    scene::ray pick_ray;
    pick_ray.origin    = transform.position;
    pick_ray.direction = glm::normalize(far_world - near_world);

    const scene::pick_result hit = scene_registry.pick(pick_ray);
    (void)scene_registry.set_selected(hit.id);
    if (hit.id != scene::invalid_object_id)
    {
        const scene::scene_object* object = scene_registry.find(hit.id);
        Logger::LogInfo("Picked scene object " + std::to_string(hit.id) + " (\"" + (object ? object->name : "?") + "\") at distance " +
                        std::to_string(hit.distance));
    }
    publish_scene_telemetry_if_changed();
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
    smoothed_frame_time      = smoothed_frame_time * 0.9F + delta_time * 0.1F;
    const float smoothed_fps = smoothed_frame_time > 0.0F ? 1.0F / smoothed_frame_time : 0.0F;

    const vulkan_run_statistics stats = vulkan_instance->statistics();
    control_plane->publish(control_plane::make_notification("telemetry.frame",
                                                            {
                                                                {"fps", smoothed_fps},
                                                                {"frame_time_ms", smoothed_frame_time * 1000.0F},
                                                                {"presented_frames", stats.presented_frames},
                                                                {"draw_pass_executions", stats.draw_pass_executions},
                                                                {"upload_pass_executions", stats.upload_pass_executions},
                                                                {"validation_errors", validation_error_count()},
                                                                {"paused", frame_paused},
                                                                {"camera", current_camera_state()},
                                                            }));

    publish_scene_telemetry_if_changed();
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
            // P2：fly 模式左键 = 拾取（orbit 模式下左键是环绕旋转，不触发拾取）
            if (event.type == interface::event_type::mouse_button_down && event.mouse_button.button == interface::mouse_button::left &&
                camera_container.transforms[camera_entity_index].mode == interface::camera_mode::fly)
            {
                try_pick_object(event.mouse_button.x, event.mouse_button.y);
            }
        }
        if (window->should_close())
        {
            return true;
        }

        // 帧边界消费控制平面命令（pause/resume/step/echo/camera.*/scene.*）
        handle_control_plane_commands();
        // 帧边界应用异步加载结果（staging + 注册 + 回响应）
        drain_completed_loads();

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
    stop_load_worker();
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
                                       std::vector<uint32_t> indices,
                                       std::vector<gltf::Vertex> vertices)
{
    // P2：启动资产登记为只读场景条目（GPU 数据仍走 legacy buffer，draws 为空）
    if (!vertices.empty() && scene_registry.objects().empty())
    {
        glm::vec3 bounds_min(std::numeric_limits<float>::max());
        glm::vec3 bounds_max(std::numeric_limits<float>::lowest());
        for (const gltf::Vertex& vertex : vertices)
        {
            bounds_min = glm::min(bounds_min, vertex.position);
            bounds_max = glm::max(bounds_max, vertex.position);
        }
        (void)scene_registry.register_object("startup", scene::aabb{bounds_min, bounds_max}, {}, true);
    }
    vulkan_instance->set_vertex_index_data(std::move(per_draw_call_data), std::move(indices), std::move(vertices));
}

void app_sample::set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list)
{
    vulkan_instance->set_mesh_list(mesh_list);
}
