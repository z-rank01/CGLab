#include "engine/engine_runtime.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#include "_interface/sdl_window.h" // For default implementation
#include "asset/asset_service.h"
#include "utility/logger.h"

namespace
{
    // 遥测推送间隔（秒）：10 Hz，避免每帧全量推 JSON
    constexpr float telemetry_interval_seconds = 0.1F;
} // namespace

namespace engine
{
engine_runtime::engine_runtime(runtime_config runtime_config,
                               std::unique_ptr<engine::render_backend> render_backend,
                               std::unique_ptr<interface::window> platform_window,
                               std::unique_ptr<asset_service> assets)
    : window(std::move(platform_window)), renderer(std::move(render_backend)), asset_loader(std::move(assets)),
      config(std::move(runtime_config))
{
    if (!renderer)
    {
        throw std::invalid_argument("engine_runtime requires a render backend");
    }
}

engine_runtime::~engine_runtime()
{
    shutdown();
}

void engine_runtime::initialize()
{
    // initialize sdl window
    if (!window)
    {
        window = std::make_unique<interface::sdl_window>();
    }
    interface::window_config win_config;
    win_config = config.window;
    if (!window->open(win_config))
    {
        throw std::runtime_error("Failed to open window.");
    }

    // initialize camera
    camera_entity_index = camera_container.add_camera();

    // control plane：失败降级为无 UI 运行，不影响渲染
    if (config.control_plane.enabled)
    {
        control_plane = std::make_unique<control_plane::control_plane_server>();
        if (!control_plane->start(control_plane::control_plane_config{
                .enabled = config.control_plane.enabled,
                .port = config.control_plane.port,
                .open_browser = config.control_plane.open_browser,
                .server_name = config.window.title,
            }))
        {
            Logger::LogWarning("Control plane unavailable; continuing without Web UI backend.");
            control_plane.reset();
        }
    }

    if (!asset_loader)
    {
        asset_loader = std::make_unique<asset::asset_service>();
    }
    asset_loader->start(config.working_directory);
    if (required_startup_asset)
    {
        const auto requested = asset_loader->request(*required_startup_asset);
        if (!requested)
        {
            throw std::runtime_error("Failed to queue startup asset: " + requested.error);
        }
        for (;;)
        {
            auto completed = asset_loader->drain_completed();
            if (!completed.empty())
            {
                if (!completed.front().result)
                {
                    throw std::runtime_error("Failed to load startup asset: " + completed.front().result.error);
                }
                initial_asset = std::move(completed.front().result.value);
                break;
            }
            std::this_thread::yield();
        }
        required_startup_asset.reset();
    }

    const engine::backend_config backend_config{
        .application_name = config.window.title,
        .working_directory = config.working_directory,
        .frames_in_flight = config.frames_in_flight,
        .validation = config.validation,
    };
    const auto initialized = renderer->initialize(*window, backend_config);
    if (!initialized)
    {
        throw std::runtime_error("Failed to initialize render backend: " + initialized.error);
    }

    if (initial_geometry)
    {
        const auto uploaded = renderer->upload_geometry(*initial_geometry);
        if (!uploaded)
        {
            throw std::runtime_error("Failed to upload startup geometry: " + uploaded.error);
        }
        const scene::aabb bounds{initial_geometry->bounds_min, initial_geometry->bounds_max};
        const scene::object_id id = scene_registry.register_object(
            initial_geometry->name.empty() ? "startup" : initial_geometry->name,
            bounds,
            {},
            true,
            uploaded.value);
        if (runtime_geometry_slots.size() <= id)
        {
            runtime_geometry_slots.resize(id + 1);
        }
        runtime_geometry_slots[id] = uploaded.value;
        initial_geometry.reset();
    }
    if (initial_asset)
    {
        const auto ids = merge_asset_database(std::move(*initial_asset), true);
        if (ids.empty()) throw std::runtime_error("Startup asset contains no mesh instances");
        initial_asset.reset();
    }

    input_events.reserve(32);
    last_frame_time = std::chrono::high_resolution_clock::now();
}

// --- P2 异步加载管线 ---

void engine_runtime::enqueue_load(std::string path, std::string client_id, nlohmann::json rpc_id)
{
    const auto requested = asset_loader->request(std::move(path));
    if (!requested)
    {
        if (control_plane)
        {
            control_plane->post_response(client_id, control_plane::make_error(rpc_id, -32000, requested.error));
        }
        return;
    }
    pending_loads.emplace(requested.value,
                          pending_load{.client_id = std::move(client_id), .rpc_id = std::move(rpc_id), .respond = true});
}

void engine_runtime::drain_completed_loads()
{
    for (completed_asset_request& completed : asset_loader->drain_completed())
    {
        const auto pending = pending_loads.find(completed.id);
        if (pending == pending_loads.end())
        {
            continue;
        }
        pending_load response = std::move(pending->second);
        pending_loads.erase(pending);
        if (!completed.result)
        {
            if (control_plane && response.respond)
            {
                control_plane->post_response(response.client_id,
                                             control_plane::make_error(response.rpc_id, -32000,
                                                                       "scene.load_asset failed: " + completed.result.error));
            }
            continue;
        }

        const std::string asset_name = completed.result.value.name;
        const std::size_t primitive_count = completed.result.value.primitives.size();
        const auto ids = merge_asset_database(std::move(completed.result.value), false);
        if (ids.empty()) continue;
        Logger::LogInfo("Loaded runtime asset \"" + asset_name + "\" with " + std::to_string(ids.size()) + " mesh instance(s)");
        if (control_plane && response.respond)
        {
            control_plane->post_response(response.client_id,
                                         control_plane::make_result(response.rpc_id,
                                                                    {{"id", ids.front()},
                                                                     {"instances", ids},
                                                                     {"name", asset_name},
                                                                     {"primitives", primitive_count}}));
        }
    }
}

std::vector<scene::object_id> engine_runtime::merge_asset_database(engine::asset_database asset, bool read_only)
{
    const auto uploaded_materials = renderer->upload_materials(asset);
    if (!uploaded_materials)
    {
        Logger::LogError("Failed to upload glTF materials: " + uploaded_materials.error);
        return {};
    }
    const std::uint32_t material_base = uploaded_materials.value;
    std::vector<engine::geometry_handle> mesh_handles(asset.meshes.size(), engine::invalid_geometry_handle);
    for (std::uint32_t mesh_index = 0; mesh_index < asset.meshes.size(); mesh_index++)
    {
        const auto& mesh = asset.meshes[mesh_index];
        engine::geometry_asset geometry{.name = mesh.name, .bounds_min = mesh.bounds_min, .bounds_max = mesh.bounds_max};
        geometry.primitives.reserve(mesh.primitive_count);
        for (std::uint32_t row = 0; row < mesh.primitive_count; row++)
        {
            const auto& primitive = asset.primitives[mesh.first_primitive + row];
            engine::geometry_primitive output{.material_index = material_base + primitive.material};
            output.vertices.assign(asset.vertex_blob.begin() + primitive.vertex_offset,
                                   asset.vertex_blob.begin() + primitive.vertex_offset + primitive.vertex_count);
            output.indices.assign(asset.index_blob.begin() + primitive.index_offset,
                                  asset.index_blob.begin() + primitive.index_offset + primitive.index_count);
            geometry.primitives.push_back(std::move(output));
        }
        const auto uploaded = renderer->upload_geometry(geometry);
        if (!uploaded)
        {
            Logger::LogError("Failed to upload glTF mesh: " + uploaded.error);
            for (const auto handle : mesh_handles)
                if (handle != engine::invalid_geometry_handle) renderer->retire_geometry(handle);
            return {};
        }
        mesh_handles[mesh_index] = uploaded.value;
    }

    std::vector<glm::mat4> world(asset.nodes.size(), glm::mat4(1.0F));
    std::vector<std::uint8_t> resolved(asset.nodes.size(), 0);
    const auto resolve = [&](auto&& self, std::uint32_t index) -> glm::mat4
    {
        if (resolved[index] == 2) return world[index];
        if (resolved[index] == 1) throw std::runtime_error("glTF node hierarchy contains a cycle");
        resolved[index] = 1;
        const auto parent = asset.nodes[index].parent;
        world[index] = parent == engine::invalid_asset_index
                           ? asset.nodes[index].local_transform
                           : self(self, parent) * asset.nodes[index].local_transform;
        resolved[index] = 2;
        return world[index];
    };
    std::vector<scene::object_id> ids;
    for (std::uint32_t node_index = 0; node_index < asset.nodes.size(); node_index++)
    {
        const auto& node = asset.nodes[node_index];
        (void)resolve(resolve, node_index);
        if (node.mesh == engine::invalid_asset_index || node.mesh >= asset.meshes.size()) continue;
        const auto handle = mesh_handles[node.mesh];
        const auto& mesh = asset.meshes[node.mesh];
        const auto id = scene_registry.register_matrix_object(node.name.empty() ? mesh.name : node.name,
                                                               {mesh.bounds_min, mesh.bounds_max}, world[node_index],
                                                               read_only, handle);
        if (runtime_geometry_slots.size() <= id) runtime_geometry_slots.resize(id + 1);
        runtime_geometry_slots[id] = handle;
        geometry_ref_counts[handle]++;
        ids.push_back(id);
    }
    for (const auto handle : mesh_handles)
        if (handle != engine::invalid_geometry_handle && !geometry_ref_counts.contains(handle)) renderer->retire_geometry(handle);
    return ids;
}

// --- P2 场景命令 ---

void engine_runtime::handle_scene_command(const control_plane::engine_command& command)
{
    using control_plane::command_kind;
    switch (command.kind)
    {
    case command_kind::scene_load_asset:
    {
        enqueue_load(command.params["path"].get<std::string>(), command.client_id, command.id);
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
            const auto handle = *runtime_geometry_slots[id];
            auto references = geometry_ref_counts.find(handle);
            if (references == geometry_ref_counts.end() || --references->second == 0)
            {
                renderer->retire_geometry(handle);
                if (references != geometry_ref_counts.end()) geometry_ref_counts.erase(references);
            }
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

void engine_runtime::handle_control_plane_commands()
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
            interface::camera_config& camera_config = camera_container.configs[camera_entity_index];
            interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
            const nlohmann::json& params           = command.params;
            if (params.contains("fov"))
            {
                transform.current_zoom = params["fov"].get<float>();
                transform.dirty        = true;
            }
            if (params.contains("movement_speed"))
            {
                camera_config.movement_speed = params["movement_speed"].get<float>();
            }
            if (params.contains("mouse_sensitivity"))
            {
                camera_config.mouse_sensitivity = params["mouse_sensitivity"].get<float>();
            }
            if (params.contains("zoom_speed"))
            {
                camera_config.zoom_speed = params["zoom_speed"].get<float>();
            }
            if (params.contains("orbit_distance"))
            {
                transform.orbit_distance = params["orbit_distance"].get<float>();
                transform.dirty          = true;
            }
            if (params.contains("near_plane"))
            {
                camera_config.near_plane = params["near_plane"].get<float>();
            }
            if (params.contains("far_plane"))
            {
                camera_config.far_plane = params["far_plane"].get<float>();
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

nlohmann::json engine_runtime::current_camera_state() const
{
    const interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
    const interface::camera_config& camera_config = camera_container.configs[camera_entity_index];
    const interface::camera_bookmarks& bookmarks = camera_container.bookmarks[camera_entity_index];
    return {
        {"mode", transform.mode == interface::camera_mode::orbit ? "orbit" : "fly"},
        {"position", {transform.position.x, transform.position.y, transform.position.z}},
        {"yaw", transform.yaw},
        {"pitch", transform.pitch},
        {"fov", transform.current_zoom},
        {"orbit_distance", transform.orbit_distance},
        {"focus_point", {transform.focus_point.x, transform.focus_point.y, transform.focus_point.z}},
        {"movement_speed", camera_config.movement_speed},
        {"mouse_sensitivity", camera_config.mouse_sensitivity},
        {"zoom_speed", camera_config.zoom_speed},
        {"near_plane", camera_config.near_plane},
        {"far_plane", camera_config.far_plane},
        {"bookmarks_valid", bookmarks.valid},
        {"blending", camera_container.blends[camera_entity_index].active},
    };
}

nlohmann::json engine_runtime::current_scene_state() const
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

void engine_runtime::publish_scene_telemetry_if_changed()
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
void engine_runtime::try_pick_object(float x, float y)
{
    int width  = 0;
    int height = 0;
    window->get_extent(width, height);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const interface::camera_transform& transform = camera_container.transforms[camera_entity_index];
    const interface::camera_config& camera_config = camera_container.configs[camera_entity_index];

    // 注意使用未做 Vulkan Y 翻转的投影做反投影（拾取在标准 NDC 约定下进行）
    const float ndc_x             = (2.0F * x) / static_cast<float>(width) - 1.0F;
    const float ndc_y             = 1.0F - (2.0F * y) / static_cast<float>(height);
    const glm::mat4 view          = interface::get_view_matrix(transform);
    const glm::mat4 proj          = interface::get_projection_matrix(transform, camera_config);
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

void engine_runtime::publish_frame_telemetry()
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

    const engine::render_statistics stats = renderer->statistics();
    control_plane->publish(control_plane::make_notification("telemetry.frame",
                                                            {
                                                                {"fps", smoothed_fps},
                                                                {"frame_time_ms", smoothed_frame_time * 1000.0F},
                                                                {"presented_frames", stats.presented_frames},
                                                                {"draw_pass_executions", stats.draw_pass_executions},
                                                                {"upload_pass_executions", stats.upload_pass_executions},
                                                                {"steady_frame_descriptor_updates", stats.steady_frame_descriptor_updates},
                                                                {"pipeline_creations", stats.pipeline_creations},
                                                                {"indirect_groups", stats.indirect_groups},
                                                                {"validation_errors", validation_error_count()},
                                                                {"paused", frame_paused},
                                                                {"camera", current_camera_state()},
                                                            }));

    publish_scene_telemetry_if_changed();
}

bool engine_runtime::tick(std::optional<std::uint64_t> frame_limit)
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
                renderer->request_resize();
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
        if (sample_definition.update)
        {
            runtime_services services{
                .scene = scene_registry,
                .cameras = camera_container,
                .active_camera = camera_entity_index,
                .request_asset = [this](std::filesystem::path path)
                {
                    auto requested = asset_loader->request(std::move(path));
                    if (requested)
                    {
                        pending_loads.emplace(requested.value, pending_load{});
                    }
                    return requested;
                },
                .post_message = [](std::string_view message) { Logger::LogInfo(std::string(message)); },
            };
            sample_definition.update(services, delta_time);
        }

        render_objects.clear();
        for (const scene::scene_object* object : scene_registry.objects())
        {
            if (object->visible && object->render_geometry != engine::invalid_geometry_handle)
            {
                render_objects.push_back(engine::render_object{
                    .geometry = object->render_geometry,
                    .model = scene::model_matrix(*object),
                });
            }
        }

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

        const engine::render_snapshot snapshot{
            .frame_serial = frame_serial++,
            .view = interface::get_view_matrix(camera_container.transforms[camera_entity_index]),
            .projection = interface::get_projection_matrix(camera_container.transforms[camera_entity_index],
                                                           camera_container.configs[camera_entity_index]),
            .objects = render_objects,
        };
        const engine::frame_status status = renderer->render(snapshot);
        if (status == engine::frame_status::failed)
        {
            return false;
        }
        if (status == engine::frame_status::skipped)
        {
            publish_frame_telemetry();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        if (status == engine::frame_status::rendered)
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

void engine_runtime::shutdown() noexcept
{
    if (control_plane)
    {
        control_plane->stop();
    }
    if (asset_loader)
    {
        asset_loader->shutdown();
    }
    control_plane.reset();
    if (renderer)
    {
        run_statistics = renderer->statistics();
        final_validation_errors = renderer->validation_error_count();
        renderer->shutdown();
    }
    renderer.reset();
    window.reset();
}

std::uint32_t engine_runtime::validation_error_count() const noexcept
{
    return renderer ? renderer->validation_error_count() : final_validation_errors;
}

void engine_runtime::set_initial_geometry(engine::geometry_asset asset)
{
    initial_geometry = std::move(asset);
}

void engine_runtime::set_required_startup_asset(std::filesystem::path path)
{
    required_startup_asset = std::move(path);
}

void engine_runtime::configure_sample(sample definition)
{
    sample_definition = std::move(definition);
    if (sample_definition.startup_geometry)
    {
        set_initial_geometry(std::move(*sample_definition.startup_geometry));
        sample_definition.startup_geometry.reset();
    }
    if (sample_definition.required_startup_asset)
    {
        set_required_startup_asset(std::move(*sample_definition.required_startup_asset));
        sample_definition.required_startup_asset.reset();
    }
}
} // namespace engine
