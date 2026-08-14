#include "engine/engine_runtime.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>
#include <utility>

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
                               engine::render_driver render_backend,
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
    // The composition point (application runner) injects the platform window.
    if (!window)
    {
        throw std::invalid_argument("engine_runtime requires a platform window");
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
    engine::load_report startup_report;
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
                startup_report = std::move(completed.front().report);
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
        engine::load_report report;
        const auto ids = merge_asset_database(std::move(*initial_geometry), true, &report);
        if (ids.empty()) throw std::runtime_error("Startup geometry contains no mesh instances");
        Logger::LogInfo("Loaded startup geometry in " + std::to_string(report.merge_us) + "us (upload " +
                        std::to_string(report.upload_us) + "us, " +
                        std::to_string(report.vertex_bytes + report.index_bytes) + " bytes)");
        initial_geometry.reset();
    }
    if (initial_asset)
    {
        const auto ids = merge_asset_database(std::move(*initial_asset), true, &startup_report);
        if (ids.empty()) throw std::runtime_error("Startup asset contains no mesh instances");
        Logger::LogInfo("Loaded startup asset \"" + startup_report.path + "\" in " +
                        std::to_string(startup_report.load_us) + "us (merge " +
                        std::to_string(startup_report.merge_us) + "us, upload " +
                        std::to_string(startup_report.upload_us) + "us, " +
                        std::to_string(startup_report.vertex_bytes + startup_report.index_bytes) + " bytes)");
        initial_asset.reset();
    }

    input_events.reserve(32);
    metrics_ring = std::make_unique<measure::metrics_ring>();
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

void engine_runtime::collect_completed_loads()
{
    for (completed_asset_request& completed : asset_loader->drain_completed())
        completed_asset_rows.push_back(std::move(completed));
}

void engine_runtime::apply_completed_loads()
{
    for (completed_asset_request& completed : completed_asset_rows)
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
        const auto ids = merge_asset_database(std::move(completed.result.value), false, &completed.report);
        if (ids.empty()) continue;
        Logger::LogInfo("Loaded runtime asset \"" + asset_name + "\" with " + std::to_string(ids.size()) + " mesh instance(s)" +
                        " in " + std::to_string(completed.report.load_us) + "us (merge " +
                        std::to_string(completed.report.merge_us) + "us, upload " +
                        std::to_string(completed.report.upload_us) + "us, " +
                        std::to_string(completed.report.vertex_bytes + completed.report.index_bytes) + " bytes)");
        if (control_plane)
        {
            publish_load_telemetry(completed.report);
        }
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
    completed_asset_rows.clear();
}

std::vector<scene::object_id> engine_runtime::merge_asset_database(engine::asset_database asset, bool read_only,
                                                                   engine::load_report* report)
{
    const auto merge_begin = std::chrono::steady_clock::now();
    std::uint64_t upload_us = 0;
    const auto track_upload = [&upload_us](const auto& begin)
    {
        upload_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count());
    };

    const material_upload_row material_row{&asset};
    const auto upload_begin = std::chrono::steady_clock::now();
    const auto material_changes = renderer->apply_resource_changes({.material_uploads = std::span(&material_row, 1)});
    track_upload(upload_begin);
    if (!material_changes || material_changes.value.material_bases.size() != 1)
    {
        Logger::LogError("Failed to upload glTF materials: " + material_changes.error);
        return {};
    }
    const std::uint32_t material_base = material_changes.value.material_bases.front();
    std::vector<engine::geometry_handle> mesh_handles(asset.meshes.size(), engine::invalid_geometry_handle);
    if (!asset.meshes.empty())
    {
        // A2：整资产单事务——单条批量行覆盖全部 mesh，零拷贝（recipe 直接引用共享 blob span）。
        // 单事务原子性：全成或全败，无逐 mesh 回滚。
        const geometry_upload_row geometry_row{&asset, 0, static_cast<std::uint32_t>(asset.meshes.size()), material_base};
        const auto geometry_upload_begin = std::chrono::steady_clock::now();
        const auto geometry_changes = renderer->apply_resource_changes({.geometry_uploads = std::span(&geometry_row, 1)});
        track_upload(geometry_upload_begin);
        if (!geometry_changes || geometry_changes.value.geometry_handles.size() != asset.meshes.size())
        {
            Logger::LogError("Failed to upload glTF geometry: " + geometry_changes.error);
            return {};
        }
        for (std::uint32_t mesh_index = 0; mesh_index < asset.meshes.size(); mesh_index++)
        {
            const auto handle = geometry_changes.value.geometry_handles[mesh_index];
            mesh_handles[mesh_index] = handle;
            // A1：维护按 geometry_handle 索引的 mesh bounds 表（extract 剔除消费）
            if (handle != engine::invalid_geometry_handle)
            {
                if (mesh_bounds_min.size() <= handle)
                {
                    mesh_bounds_min.resize(handle + 1, glm::vec3(0.0F));
                    mesh_bounds_max.resize(handle + 1, glm::vec3(0.0F));
                }
                mesh_bounds_min[handle] = asset.meshes[mesh_index].bounds_min;
                mesh_bounds_max[handle] = asset.meshes[mesh_index].bounds_max;
            }
        }
    }

    std::vector<glm::mat4> world(asset.nodes.size(), glm::mat4(1.0F));
    std::vector<std::uint8_t> resolved(asset.nodes.size(), 0);
    bool hierarchy_cycle = false;
    const auto resolve = [&](auto&& self, std::uint32_t index) -> glm::mat4
    {
        if (resolved[index] == 2) return world[index];
        // Loaders reject cyclic hierarchies on the worker side; stay defensive here.
        if (resolved[index] == 1)
        {
            hierarchy_cycle = true;
            return glm::mat4(1.0F);
        }
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
        if (hierarchy_cycle)
        {
            Logger::LogError("Rejected asset with a cyclic node hierarchy: " + asset.name);
            std::vector<geometry_retire_row> rollback_rows;
            for (const auto handle : mesh_handles)
                if (handle != engine::invalid_geometry_handle) rollback_rows.push_back({handle});
            if (!rollback_rows.empty())
                (void)renderer->apply_resource_changes({.geometry_retires = rollback_rows});
            return {};
        }
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
        if (handle != engine::invalid_geometry_handle && !geometry_ref_counts.contains(handle))
            pending_geometry_retires.push_back({handle});
    if (report)
    {
        report->merge_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - merge_begin).count());
        report->upload_us = upload_us;
    }
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
                pending_geometry_retires.push_back({handle});
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
        case command_kind::camera_set_culling:
        {
            const bool enabled = command.params["enabled"].get<bool>();
            set_camera_culling(enabled);
            control_plane->post_response(command.client_id,
                                         control_plane::make_result(command.id, {{"culling", enabled}}));
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
        {"culling", camera_entity_index < culling.present.size() && culling.present[camera_entity_index] != 0 &&
                        culling.enabled[camera_entity_index] != 0},
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
            {"draw_count", object->draw_count},
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

    // A0 扩展：阶段耗时均值、帧时分位数、帧计数
    static constexpr std::array phase_names{
        "poll_events", "consume_control_commands", "merge_asset_results", "update_scene_transforms",
        "update_cameras", "run_sample_systems", "extract_render_packet", "apply_resource_changes",
        "submit_render_packet", "publish_telemetry",
    };
    nlohmann::json phase_us = nlohmann::json::object();
    nlohmann::json quantiles = nlohmann::json::object();
    if (metrics_ring)
    {
        for (std::uint32_t p = 0; p < measure::phase_count; ++p)
        {
            const auto q = measure::summarize(*metrics_ring, 1 + p);
            phase_us[phase_names[p]] = q.p50;
        }
        const auto frame_q = measure::summarize(*metrics_ring, 0);
        quantiles = {{"frame_p50_ms", frame_q.p50 / 1000.0},
                     {"frame_p95_ms", frame_q.p95 / 1000.0},
                     {"frame_p99_ms", frame_q.p99 / 1000.0}};
    }

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
                                                                {"phase_us", std::move(phase_us)},
                                                                {"quantiles", std::move(quantiles)},
                                                                {"counters",
                                                                 {{"instances", frame_counters.instance_count},
                                                                  {"visible", frame_counters.visible_count},
                                                                  {"culled", frame_counters.culled_count},
                                                                  {"draws", frame_counters.draw_commands},
                                                                  {"buffer_uploads", frame_counters.buffer_upload_count},
                                                                  {"image_uploads", frame_counters.image_upload_count}}},
                                                            }));

    publish_scene_telemetry_if_changed();
}

void engine_runtime::publish_load_telemetry(const engine::load_report& report) const
{
    control_plane->publish(control_plane::make_notification("telemetry.load",
                                                            {
                                                                {"path", report.path},
                                                                {"load_us", report.load_us},
                                                                {"merge_us", report.merge_us},
                                                                {"upload_us", report.upload_us},
                                                                {"vertex_bytes", report.vertex_bytes},
                                                                {"index_bytes", report.index_bytes},
                                                                {"images", report.image_count},
                                                                {"parse_us", report.parse_us},
                                                                {"convert_us", report.convert_us},
                                                                {"decode_us", report.decode_us},
                                                            }));
}

bool engine_runtime::tick(std::optional<std::uint64_t> frame_limit)
{
    static constexpr std::array phase_table{
        &engine_runtime::poll_events,
        &engine_runtime::consume_control_commands,
        &engine_runtime::merge_asset_results,
        &engine_runtime::update_scene_transforms,
        &engine_runtime::update_cameras,
        &engine_runtime::run_sample_systems,
        &engine_runtime::extract_render_packet,
        &engine_runtime::apply_resource_changes,
        &engine_runtime::submit_render_packet,
        &engine_runtime::publish_telemetry,
    };
    std::uint64_t rendered_frames = 0;
    while (!window->should_close())
    {
        frame_phase_context context;
        const auto frame_begin = std::chrono::steady_clock::now();
        std::array<std::uint64_t, measure::phase_count> phase_us{};
        for (std::uint32_t phase_index = 0; phase_index < measure::phase_count; ++phase_index)
        {
            const auto phase_begin = std::chrono::steady_clock::now();
            (this->*phase_table[phase_index])(context);
            phase_us[phase_index] = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phase_begin)
                    .count());
        }
        if (metrics_ring)
        {
            std::array<std::uint64_t, measure::counter_slot_count> counter_slots{};
            counter_slots[0] = frame_counters.instance_count;
            counter_slots[1] = frame_counters.visible_count;
            counter_slots[2] = frame_counters.culled_count;
            counter_slots[3] = frame_counters.draw_commands;
            counter_slots[4] = frame_counters.buffer_upload_count;
            counter_slots[5] = frame_counters.image_upload_count;
            measure::push(*metrics_ring,
                          static_cast<std::uint64_t>(
                              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                                    frame_begin)
                                  .count()),
                          phase_us, counter_slots);
        }
        if (context.stop_failure) return false;
        if (context.stop_success) return true;
        if (context.rendered && frame_limit && ++rendered_frames >= *frame_limit) return true;
    }
    return true;
}

void engine_runtime::poll_events(frame_phase_context& context)
{
    const auto current_time = std::chrono::high_resolution_clock::now();
    delta_time = std::chrono::duration<float>(current_time - last_frame_time).count();
    last_frame_time = current_time;
    window->poll_events(input_events);
    for (const interface::input_event& event : input_events)
    {
        if (event.type == interface::event_type::resize) renderer->request_resize();
        if (event.type == interface::event_type::mouse_button_down &&
            event.mouse_button.button == interface::mouse_button::left &&
            camera_container.transforms[camera_entity_index].mode == interface::camera_mode::fly)
            try_pick_object(event.mouse_button.x, event.mouse_button.y);
    }
    context.stop_success = window->should_close();
}

void engine_runtime::consume_control_commands(frame_phase_context& context)
{
    if (!context.stop_success) handle_control_plane_commands();
}

void engine_runtime::merge_asset_results(frame_phase_context& context)
{
    if (!context.stop_success) collect_completed_loads();
}

void engine_runtime::update_scene_transforms(frame_phase_context&)
{
    // Matrix rows are authoritative; hierarchical glTF transforms were resolved at the frame-boundary merge.
}

void engine_runtime::update_cameras(frame_phase_context& context)
{
    if (!context.stop_success) interface::tick(camera_container, camera_update_context, input_events, delta_time);
}

void engine_runtime::run_sample_systems(frame_phase_context& context)
{
    if (context.stop_success || !sample_definition.update) return;
    runtime_services services{
        .scene = scene_registry,
        .cameras = camera_container,
        .active_camera = camera_entity_index,
        .request_asset = [this](std::filesystem::path path)
        {
            auto requested = asset_loader->request(std::move(path));
            if (requested) pending_loads.emplace(requested.value, pending_load{});
            return requested;
        },
        .post_message = [](std::string_view message) { Logger::LogInfo(std::string(message)); },
    };
    sample_definition.update(services, delta_time);
}

void engine_runtime::extract_render_packet(frame_phase_context& context)
{
    if (context.stop_success) return;
    frame_counters = {};
    render_instances.clear();
    render_transforms.clear();
    for (const scene::scene_object* object : scene_registry.objects())
    {
        if (!object->visible || object->render_geometry == engine::invalid_geometry_handle) continue;
        const std::uint32_t transform = static_cast<std::uint32_t>(render_transforms.size());
        render_transforms.push_back(scene::model_matrix(*object));
        render_instances.push_back({.mesh = object->render_geometry, .transform = transform});
    }
    frame_counters.instance_count = render_instances.size();
    render_cameras = {engine::camera_row{
        .view = interface::get_view_matrix(camera_container.transforms[camera_entity_index]),
        .projection = interface::get_projection_matrix(camera_container.transforms[camera_entity_index],
                                                       camera_container.configs[camera_entity_index]),
    }};

    // A1：相机挂了剔除组件且开启时，在 packet 输入侧做实例级视锥剔除。
    // 显隐过滤（scene_registry visible）在上方完成，剔除在其后；recipe/RG 零改动。
    frame_instance_rows = render_instances;
    if (camera_entity_index < culling.present.size() && culling.present[camera_entity_index] != 0 &&
        culling.enabled[camera_entity_index] != 0)
    {
        const glm::mat4 view_projection = render_cameras.front().projection * render_cameras.front().view;
        std::uint64_t culled = 0;
        frame_instance_rows = engine::cull_instances(culling, camera_entity_index, view_projection,
                                                     render_instances, render_transforms,
                                                     mesh_bounds_min, mesh_bounds_max, &culled);
        frame_counters.visible_count = frame_instance_rows.size();
        frame_counters.culled_count = culled;
    }
    else
    {
        frame_counters.visible_count = render_instances.size();
    }
}

void engine_runtime::apply_resource_changes(frame_phase_context& context)
{
    if (context.stop_success) return;
    apply_completed_loads();
    if (pending_geometry_retires.empty()) return;
    const auto changed = renderer->apply_resource_changes({.geometry_retires = pending_geometry_retires});
    if (!changed)
    {
        Logger::LogError("Failed to retire geometry resources: " + changed.error);
        context.stop_failure = true;
        return;
    }
    pending_geometry_retires.clear();
}

void engine_runtime::submit_render_packet(frame_phase_context& context)
{
    if (context.stop_success) return;
    context.render_this_frame = !frame_paused;
    if (pending_frame_steps > 0)
    {
        --pending_frame_steps;
        context.render_this_frame = true;
    }
    if (!context.render_this_frame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return;
    }
    const engine::render_frame_packet packet{
        .frame_serial = frame_serial++,
        .camera_rows = render_cameras,
        .instance_rows = frame_instance_rows,
        .transform_rows = render_transforms,
        .counters = &frame_counters,
    };
    const engine::frame_status status = renderer->render(packet);
    context.stop_failure = status == engine::frame_status::failed;
    context.rendered = status == engine::frame_status::rendered;
    if (status == engine::frame_status::skipped)
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
}

void engine_runtime::publish_telemetry(frame_phase_context& context)
{
    if (!context.stop_success && !context.stop_failure) publish_frame_telemetry();
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
    renderer = engine::render_driver{};
    window.reset();
}

std::uint32_t engine_runtime::validation_error_count() const noexcept
{
    return renderer ? renderer->validation_error_count() : final_validation_errors;
}

void engine_runtime::set_initial_geometry(engine::asset_database asset)
{
    initial_geometry = std::move(asset);
}

void engine_runtime::set_required_startup_asset(std::filesystem::path path)
{
    required_startup_asset = std::move(path);
}

void engine_runtime::set_camera_culling(bool enabled)
{
    if (enabled)
    {
        (void)engine::attach_culling(culling, camera_entity_index, true);
    }
    else
    {
        engine::set_culling_enabled(culling, camera_entity_index, false);
    }
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
