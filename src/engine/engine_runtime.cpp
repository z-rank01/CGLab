#include "engine/engine_runtime.h"

#include <algorithm>
#include <array>
#include <thread>
#include <utility>

#include "asset/asset_service.h"
#include "engine/geometry_upload_plan.h"
#include "utility/logger.h"

namespace
{
    // 遥测推送间隔（秒）：10 Hz，避免每帧全量推 JSON
    constexpr float telemetry_interval_seconds = 0.1F;
    // T1b/M7 分块流式上传：每帧字节预算（8 MB ≈ 亚毫秒 memcpy，帧时间有界；
    // 单片超预算的 mesh 独立成片，帧上界 = 单 mesh 大小，设计取舍）
    constexpr std::uint64_t upload_chunk_budget_bytes = 8ULL * 1024ULL * 1024ULL;

    std::string geometry_arena_summary(const engine::load_report& report)
    {
        if (report.geometry_arena_count == 0)
        {
            return {};
        }
        return ", arena count=" + std::to_string(report.geometry_arena_count) +
               " reserved=" + std::to_string(report.geometry_arena_reserved_bytes) +
               " used=" + std::to_string(report.geometry_arena_used_bytes) +
               " bytes (created=" + std::to_string(report.geometry_arenas_created) +
               ", alloc=" + std::to_string(report.geometry_arena_allocation_us) +
               "us, plan=" + std::to_string(report.geometry_plan_us) +
               "us, transfer=" + std::to_string(report.geometry_transfer_us) + "us)";
    }
} // namespace

namespace engine
{
engine_runtime::engine_runtime(runtime_config runtime_config,
                               engine::render_driver render_backend,
                               std::unique_ptr<interface::window> platform_window,
                               std::unique_ptr<asset_service> assets)
    : window(std::move(platform_window)), renderer(std::move(render_backend)), config(std::move(runtime_config))
{
    // H1：renderer 校验移入 initialize（构造不抛异常，错误经 result 返回）
    loads.asset_loader = std::move(assets);
}

engine_runtime::~engine_runtime()
{
    shutdown();
}

engine::result<bool> engine_runtime::initialize()
{
    // H1：启动路径错误经 result<bool> 返回（替换 8 处 throw；runner catch 边界兜底）
    if (!renderer)
    {
        return {.error = "engine_runtime requires a render backend"};
    }
    // The composition point (application runner) injects the platform window.
    if (!window)
    {
        return {.error = "engine_runtime requires a platform window"};
    }
    interface::window_config win_config;
    win_config = config.window;
    if (!window->open(win_config))
    {
        return {.error = "Failed to open window"};
    }

    // initialize camera
    camera_entity_index = camera_container.add_camera();

    // control plane：失败降级为无 UI 运行，不影响渲染
    if (config.control_plane.enabled)
    {
        control_plane = std::make_unique<control_plane::control_plane_server>();
        // I1：HTTP 静态托管与 WS 同端口。B2 起优先托管正式 UI 构建产物
        // （<工作目录>/ui/dist），未构建时回退单文件 dev console（<工作目录>/web）。
        const std::string dist_root = config.working_directory + "/ui/dist";
        const std::string web_root  = std::filesystem::is_directory(dist_root)
                                          ? dist_root
                                          : config.working_directory + "/web";
        if (!control_plane->start(control_plane::control_plane_config{
                .enabled = config.control_plane.enabled,
                .port = config.control_plane.port,
                .open_browser = config.control_plane.open_browser,
                .server_name = config.window.title,
                .web_root = web_root,
            }))
        {
            Logger::LogWarning("Control plane unavailable; continuing without Web UI backend.");
            control_plane.reset();
        }
    }

    if (!loads.asset_loader)
    {
        loads.asset_loader = std::make_unique<asset::asset_service>();
    }
    loads.asset_loader->start(config.working_directory);
    engine::load_report startup_report;
    if (loads.required_startup_asset)
    {
        const auto requested = loads.asset_loader->request(*loads.required_startup_asset);
        if (!requested)
        {
            return {.error = "Failed to queue startup asset: " + requested.error};
        }
        for (;;)
        {
            auto completed = loads.asset_loader->drain_completed();
            if (!completed.empty())
            {
                if (!completed.front().result)
                {
                    return {.error = "Failed to load startup asset: " + completed.front().result.error};
                }
                startup_report = std::move(completed.front().report);
                loads.initial_asset = std::move(completed.front().result.value);
                break;
            }
            std::this_thread::yield();
        }
        loads.required_startup_asset.reset();
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
        return {.error = "Failed to initialize render backend: " + initialized.error};
    }

    if (loads.initial_geometry)
    {
        engine::load_report report;
        const auto ids = merge_asset_database(std::move(*loads.initial_geometry), true, &report);
        if (ids.empty()) return {.error = "Startup geometry contains no mesh instances"};
        Logger::LogInfo("Loaded startup geometry in " + std::to_string(report.merge_us) + "us (upload " +
                        std::to_string(report.upload_us) + "us, " +
                        std::to_string(report.vertex_bytes + report.index_bytes) + " bytes" +
                        geometry_arena_summary(report) + ")");
        loads.initial_geometry.reset();
    }
    if (loads.initial_asset)
    {
        const auto ids = merge_asset_database(std::move(*loads.initial_asset), true, &startup_report);
        if (ids.empty()) return {.error = "Startup asset contains no mesh instances"};
        Logger::LogInfo("Loaded startup asset \"" + startup_report.path + "\" in " +
                        std::to_string(startup_report.load_us) + "us (merge " +
                        std::to_string(startup_report.merge_us) + "us, upload " +
                        std::to_string(startup_report.upload_us) + "us, " +
                        std::to_string(startup_report.vertex_bytes + startup_report.index_bytes) + " bytes" +
                        geometry_arena_summary(startup_report) + ")");
        loads.initial_asset.reset();
    }

    input_events.reserve(32);
    telemetry.metrics_ring = std::make_unique<measure::metrics_ring>();
    last_frame_time = std::chrono::high_resolution_clock::now();
    return {.value = true};
}

// --- 异步加载管线 ---

void engine_runtime::enqueue_load(const std::string& path, std::string client_id, nlohmann::json rpc_id)
{
    const auto requested = loads.asset_loader->request(path);
    if (!requested)
    {
        if (control_plane)
        {
            control_plane->post_response(client_id, control_plane::make_error(rpc_id, -32000, requested.error));
        }
        return;
    }
    loads.pending_loads.emplace(requested.value,
                          pending_load{.client_id = std::move(client_id), .rpc_id = std::move(rpc_id), .respond = true});
}

void engine_runtime::collect_completed_loads()
{
    for (completed_asset_request& completed : loads.asset_loader->drain_completed())
        loads.completed_asset_rows.push_back(std::move(completed));
}

void engine_runtime::apply_completed_loads()
{
    for (completed_asset_request& completed : loads.completed_asset_rows)
    {
        const auto pending = loads.pending_loads.find(completed.id);
        if (pending == loads.pending_loads.end())
        {
            continue;
        }
        pending_load response = std::move(pending->second);
        loads.pending_loads.erase(pending);
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

        // T1b/M7：运行时资产一律分块流式上传（小资产单片完成，语义与一次性一致；
        // 大资产跨帧续传，帧时间有界，进度经 A0 协议上报）。完成/失败在
        // drain_streamed_uploads 统一收尾（响应、遥测、场景注册）。
        begin_streamed_upload(completed, std::move(response));
    }
    loads.completed_asset_rows.clear();
}

void engine_runtime::begin_streamed_upload(completed_asset_request& completed, pending_load response)
{
    streamed_upload upload;
    upload.asset = std::move(completed.result.value);
    upload.client_id = std::move(response.client_id);
    upload.rpc_id = std::move(response.rpc_id);
    upload.respond = response.respond;
    upload.report = std::move(completed.report);
    upload.total_bytes = (upload.asset.vertex_blob.size() * sizeof(engine::vertex)) +
                         (upload.asset.index_blob.size() * sizeof(std::uint32_t));
    upload.upload_begin = std::chrono::steady_clock::now();
    loads.streamed_uploads.push_back(std::move(upload));
}

void engine_runtime::drain_streamed_uploads()
{
    if (loads.streamed_uploads.empty())
    {
        return;
    }
    // 每帧推进一个流的一片（顺序加载；worker 侧多资产并行解析不受影响）
    streamed_upload& upload = loads.streamed_uploads.front();
    if (!upload.materials_uploaded)
    {
        const auto material_base = upload_asset_materials(upload.asset, &upload.report);
        if (!material_base)
        {
            Logger::LogError("Streamed upload failed at material stage: " + material_base.error);
            if (control_plane && upload.respond)
            {
                control_plane->post_response(upload.client_id,
                                             control_plane::make_error(upload.rpc_id, -32000, material_base.error));
            }
            loads.streamed_uploads.erase(loads.streamed_uploads.begin());
            return;
        }
        upload.material_base = material_base.value;
        upload.materials_uploaded = true;
        upload.mesh_handles.resize(upload.asset.meshes.size(), engine::invalid_geometry_handle);
    }
    if (upload.next_mesh < upload.asset.meshes.size())
    {
        const auto chunk = engine::plan_upload_chunk(upload.asset, upload.next_mesh,
                                                     static_cast<std::uint32_t>(upload.asset.meshes.size() - upload.next_mesh),
                                                     upload_chunk_budget_bytes);
        const auto geometry_changes = upload_asset_geometry(upload.asset, upload.material_base, upload.next_mesh,
                                                            chunk.mesh_count, &upload.report);
        if (!geometry_changes)
        {
            Logger::LogError("Streamed upload failed at geometry chunk (mesh " +
                             std::to_string(upload.next_mesh) + "): " + geometry_changes.error);
            if (control_plane && upload.respond)
            {
                control_plane->post_response(upload.client_id,
                                             control_plane::make_error(upload.rpc_id, -32000, geometry_changes.error));
            }
            // 已上传分片的句柄 retire（未上传部分仍是 invalid_geometry_handle 哨兵），
            // 经 pending_geometry_retires 在 retire 边界统一排空——失败不泄漏 arena/句柄。
            for (const auto handle : upload.mesh_handles)
                if (handle != engine::invalid_geometry_handle) loads.pending_geometry_retires.push_back({handle});
            loads.streamed_uploads.erase(loads.streamed_uploads.begin());
            return;
        }
        for (std::uint32_t i = 0; i < chunk.mesh_count; ++i)
        {
            upload.mesh_handles[upload.next_mesh + i] = geometry_changes.value.geometry_handles[i];
        }
        upload.uploaded_bytes += chunk.payload_bytes;
        upload.next_mesh += chunk.mesh_count;
        if (control_plane)
        {
            publish_load_progress(upload);
        }
    }
    if (upload.next_mesh >= upload.asset.meshes.size())
    {
        finalize_streamed_upload(upload);
        loads.streamed_uploads.erase(loads.streamed_uploads.begin());
    }
}

void engine_runtime::finalize_streamed_upload(streamed_upload& upload)
{
    const std::string asset_name = upload.asset.name;
    const std::size_t primitive_count = upload.asset.primitives.size();
    const auto ids = register_asset_scene(upload.asset, upload.mesh_handles, false, &upload.report);
    if (ids.empty())
    {
        return; // 注册失败（如循环层级）：与一次性路径同语义，不回应、不上报
    }
    Logger::LogInfo("Loaded runtime asset \"" + asset_name + "\" with " + std::to_string(ids.size()) + " mesh instance(s)" +
                    " in " + std::to_string(upload.report.load_us) + "us (merge " +
                    std::to_string(upload.report.merge_us) + "us, upload " +
                    std::to_string(upload.report.upload_us) + "us, " +
                    std::to_string(upload.report.vertex_bytes + upload.report.index_bytes) + " bytes" +
                    geometry_arena_summary(upload.report) + ")");
    if (control_plane)
    {
        publish_load_telemetry(upload.report);
    }
    if (control_plane && upload.respond)
    {
        control_plane->post_response(upload.client_id,
                                     control_plane::make_result(upload.rpc_id,
                                                                {{"id", ids.front()},
                                                                 {"instances", ids},
                                                                 {"name", asset_name},
                                                                 {"primitives", primitive_count}}));
    }
}

void engine_runtime::publish_load_progress(const streamed_upload& upload) const
{
    const auto mesh_total = static_cast<std::uint32_t>(upload.asset.meshes.size());
    control_plane->publish(control_plane::make_notification("telemetry.load_progress",
                                                            {
                                                                {"path", upload.report.path},
                                                                {"uploaded_bytes", upload.uploaded_bytes},
                                                                {"total_bytes", upload.total_bytes},
                                                                {"fraction",
                                                                 upload.total_bytes != 0
                                                                     ? static_cast<double>(upload.uploaded_bytes) /
                                                                           static_cast<double>(upload.total_bytes)
                                                                     : 1.0},
                                                                {"meshes",
                                                                 {{"done", upload.next_mesh},
                                                                  {"total", mesh_total}}},
                                                            }));
}

engine::result<std::uint32_t> engine_runtime::upload_asset_materials(const engine::asset_database& asset,
                                                                     engine::load_report* report)
{
    engine::result<std::uint32_t> output;
    const auto upload_begin = std::chrono::steady_clock::now();
    const material_upload_row material_row{&asset};
    const auto material_changes = renderer->apply_resource_changes({.material_uploads = std::span(&material_row, 1)});
    if (report != nullptr)
    {
        report->upload_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - upload_begin).count());
    }
    if (!material_changes || material_changes.value.material_bases.size() != 1)
    {
        output.error = "Failed to upload glTF materials: " + material_changes.error;
        return output;
    }
    output.value = material_changes.value.material_bases.front();
    return output;
}

engine::result<engine::resource_change_result> engine_runtime::upload_asset_geometry(
    const engine::asset_database& asset,
    std::uint32_t material_base,
    std::uint32_t first_mesh,
    std::uint32_t mesh_count,
    engine::load_report* report)
{
    engine::result<engine::resource_change_result> output;
    if (mesh_count == 0)
    {
        return output;
    }
    const auto upload_begin = std::chrono::steady_clock::now();
    const geometry_upload_row geometry_row{.asset=&asset, .first_mesh=first_mesh, .mesh_count=mesh_count, .material_base=material_base};
    const auto geometry_changes = renderer->apply_resource_changes({.geometry_uploads = std::span(&geometry_row, 1)});
    if (report != nullptr)
    {
        report->upload_us += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - upload_begin).count());
    }
    if (!geometry_changes || geometry_changes.value.geometry_handles.size() != mesh_count)
    {
        output.error = "Failed to upload glTF geometry: " + geometry_changes.error;
        return output;
    }
    if (report != nullptr)
    {
        report->geometry_arena_count = geometry_changes.value.geometry_arena_count;
        report->geometry_arenas_created = geometry_changes.value.geometry_arenas_created;
        report->geometry_arena_reserved_bytes = geometry_changes.value.geometry_arena_reserved_bytes;
        report->geometry_arena_used_bytes = geometry_changes.value.geometry_arena_used_bytes;
        report->geometry_arena_allocation_us += geometry_changes.value.geometry_arena_allocation_us;
        report->geometry_plan_us += geometry_changes.value.geometry_plan_us;
        report->geometry_transfer_us += geometry_changes.value.geometry_transfer_us;
    }
    output.value = geometry_changes.value;
    return output;
}

std::vector<scene::object_id> engine_runtime::register_asset_scene(const engine::asset_database& asset,
                                                                   std::span<const engine::geometry_handle> mesh_handles,
                                                                   bool read_only,
                                                                   engine::load_report* report)
{
    const auto merge_begin = std::chrono::steady_clock::now();
    for (std::uint32_t mesh_index = 0; mesh_index < asset.meshes.size(); mesh_index++)
    {
        const auto handle = mesh_handles[mesh_index];
        // 维护按 geometry_handle 索引的 mesh bounds 表（extract 剔除消费）
        if (handle != engine::invalid_geometry_handle)
        {
            if (extract.mesh_bounds_min.size() <= handle)
            {
                extract.mesh_bounds_min.resize(handle + 1, glm::vec3(0.0F));
                extract.mesh_bounds_max.resize(handle + 1, glm::vec3(0.0F));
            }
            extract.mesh_bounds_min[handle] = asset.meshes[mesh_index].bounds_min;
            extract.mesh_bounds_max[handle] = asset.meshes[mesh_index].bounds_max;
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
            return {1.0F};
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
        const auto id = scene_registry.register_matrix_object(
            node.name.empty() ? mesh.name : node.name,
            {.min=mesh.bounds_min, .max=mesh.bounds_max}, 
            world[node_index],
            read_only, 
            handle);
        if (extract.runtime_geometry_slots.size() <= id) extract.runtime_geometry_slots.resize(id + 1);
        extract.runtime_geometry_slots[id] = handle;
        loads.geometry_ref_counts[handle]++;
        ids.push_back(id);
    }
    for (const auto handle : mesh_handles)
        if (handle != engine::invalid_geometry_handle && !loads.geometry_ref_counts.contains(handle))
            loads.pending_geometry_retires.push_back({handle});
    if (report != nullptr)
    {
        report->vertex_bytes = asset.vertex_blob.size() * sizeof(engine::vertex);
        report->index_bytes = asset.index_blob.size() * sizeof(std::uint32_t);
        report->image_count = static_cast<std::uint32_t>(asset.images.size());
        report->merge_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - merge_begin).count());
    }
    return ids;
}

std::vector<scene::object_id> engine_runtime::merge_asset_database(const engine::asset_database& asset, bool read_only,
                                                                   engine::load_report* report)
{
    // 启动路径：材质 + 几何一次事务 + 场景注册紧邻（行为与分片前一致）。
    const auto material_base = upload_asset_materials(asset, report);
    if (!material_base)
    {
        Logger::LogError(material_base.error);
        return {};
    }
    std::vector<engine::geometry_handle> mesh_handles(asset.meshes.size(), engine::invalid_geometry_handle);
    if (!asset.meshes.empty())
    {
        // 整资产单事务——单条批量行覆盖全部 mesh，零拷贝（recipe 直接引用共享 blob span）。
        // 单事务原子性：全成或全败，无逐 mesh 回滚。
        const auto geometry_changes =
            upload_asset_geometry(asset, material_base.value, 0, static_cast<std::uint32_t>(asset.meshes.size()), report);
        if (!geometry_changes)
        {
            Logger::LogError(geometry_changes.error);
            return {};
        }
        mesh_handles = geometry_changes.value.geometry_handles;
    }
    return register_asset_scene(asset, mesh_handles, read_only, report);
}

// --- 场景命令 ---

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
        if (id < extract.runtime_geometry_slots.size() && extract.runtime_geometry_slots[id].has_value())
        {
            const auto handle = *extract.runtime_geometry_slots[id];
            auto references = loads.geometry_ref_counts.find(handle);
            if (references == loads.geometry_ref_counts.end() || --references->second == 0)
            {
                loads.pending_geometry_retires.push_back({handle});
                if (references != loads.geometry_ref_counts.end()) loads.geometry_ref_counts.erase(references);
            }
            extract.runtime_geometry_slots[id].reset();
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
    case command_kind::debug_set_view:
    {
        // M8/B2：协议层已把枚举名映射为下标（对齐 apps::debug_view_mode），
        // 引擎只搬运数值；sample 每帧从帧通道读取并翻译发布。
        debug_override.mode   = command.params["view"].get<std::uint32_t>();
        debug_override.active = true;
        control_plane->post_response(command.client_id,
                                     control_plane::make_result(command.id, {{"mode", debug_override.mode}}));
        break;
    }
    case command_kind::rg_get_dump:
    {
        // M8/B3：子仓 dump 为确定性 JSON 文本，解析后随响应回填（镜像 scene.list 拉取口径）
        auto dump = renderer->graph_debug_dump();
        if (!dump)
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32603, "RG debug dump unavailable: " + dump.error));
            break;
        }
        auto parsed = nlohmann::json::parse(dump.value, nullptr, false);
        if (parsed.is_discarded())
        {
            control_plane->post_response(command.client_id,
                                         control_plane::make_error(command.id, -32603, "RG debug dump is not valid JSON"));
            break;
        }
        control_plane->post_response(command.client_id,
                                     control_plane::make_result(command.id,
                                                                {{"revision", renderer->statistics().graph_compiles},
                                                                 {"dump", std::move(parsed)}}));
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
    if (revision == telemetry.last_published_scene_revision)
    {
        return;
    }
    telemetry.last_published_scene_revision = revision;
    control_plane->publish(control_plane::make_notification("telemetry.scene", current_scene_state()));
}

// M8/B3：RG 快照推送（镜像 scene 版本闸）——revision = graph_compiles，
// debug 视图切换/resize 触发 recompile 后下一遥测帧自动下发新图。
void engine_runtime::publish_rg_telemetry_if_changed()
{
    if (!control_plane)
    {
        return;
    }
    const std::uint64_t revision = renderer->statistics().graph_compiles;
    if (revision == 0 || revision == telemetry.last_published_rg_revision)
    {
        return;
    }
    auto dump = renderer->graph_debug_dump();
    if (!dump)
    {
        return; // 版本号不消费，下个遥测帧重试
    }
    auto parsed = nlohmann::json::parse(dump.value, nullptr, false);
    if (parsed.is_discarded())
    {
        return;
    }
    telemetry.last_published_rg_revision = revision;
    control_plane->publish(control_plane::make_notification("telemetry.rg",
                                                            {{"revision", revision}, {"dump", std::move(parsed)}}));
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
    const float ndc_x             = ((2.0F * x) / static_cast<float>(width)) - 1.0F;
    const float ndc_y             = 1.0F - ((2.0F * y) / static_cast<float>(height));
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
        Logger::LogInfo("Picked scene object " + std::to_string(hit.id) + " (\"" + ((object != nullptr) ? object->name : "?") + "\") at distance " +
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
    telemetry.telemetry_accumulator += delta_time;
    if (telemetry.telemetry_accumulator < telemetry_interval_seconds)
    {
        return;
    }
    telemetry.telemetry_accumulator = 0.0F;

    // 帧时间指数滑动平均，抑制单帧抖动
    telemetry.smoothed_frame_time      = (telemetry.smoothed_frame_time * 0.9F) + (delta_time * 0.1F);
    const float smoothed_fps = telemetry.smoothed_frame_time > 0.0F ? 1.0F / telemetry.smoothed_frame_time : 0.0F;

    const engine::render_statistics stats = renderer->statistics();

    // 扩展：阶段耗时均值、帧时分位数、帧计数
    static constexpr std::array phase_names{
        "poll_events", "consume_control_commands", "merge_asset_results", "update_scene_transforms",
        "update_cameras", "run_sample_systems", "extract_render_packet", "apply_resource_changes",
        "submit_render_packet", "publish_telemetry",
    };
    nlohmann::json phase_us = nlohmann::json::object();
    nlohmann::json quantiles = nlohmann::json::object();
    if (telemetry.metrics_ring)
    {
        for (std::uint32_t p = 0; p < measure::phase_count; ++p)
        {
            const auto q = measure::summarize(*telemetry.metrics_ring, 1 + p);
            phase_us[phase_names[p]] = q.p50;
        }
        const auto frame_q = measure::summarize(*telemetry.metrics_ring, 0);
        quantiles = {{"frame_p50_ms", frame_q.p50 / 1000.0},
                     {"frame_p95_ms", frame_q.p95 / 1000.0},
                     {"frame_p99_ms", frame_q.p99 / 1000.0}};
    }

    // M8/B2：sample 回报的 debug 视图状态（帧通道；缺通道 = off）。
    // publish_telemetry 在 run_sample_systems 之后执行，同帧通道可读。
    const auto* debug_status = extract.channels.find_state<engine::debug_view_status>();
    const std::uint32_t debug_view_mode = debug_status != nullptr ? debug_status->mode : 0U;

    control_plane->publish(control_plane::make_notification("telemetry.frame",
                                                            {
                                                                {"fps", smoothed_fps},
                                                                {"frame_time_ms", telemetry.smoothed_frame_time * 1000.0F},
                                                                {"presented_frames", stats.presented_frames},
                                                                {"draw_pass_executions", stats.draw_pass_executions},
                                                                {"upload_pass_executions", stats.upload_pass_executions},
                                                                {"steady_frame_descriptor_updates", stats.steady_frame_descriptor_updates},
                                                                {"pipeline_creations", stats.pipeline_creations},
                                                                {"indirect_groups", stats.indirect_groups},
                                                                {"validation_errors", validation_error_count()},
                                                                {"paused", frame_paused},
                                                                // M8/B2：sample 回报的实际生效 debug 视图（缺通道 = off）
                                                                {"debug_view", debug_view_mode},
                                                                {"camera", current_camera_state()},
                                                                {"phase_us", std::move(phase_us)},
                                                                {"quantiles", std::move(quantiles)},
                                                                {"counters",
                                                                 {{"instances", telemetry.frame_counters.instance_count},
                                                                  {"visible", telemetry.frame_counters.visible_count},
                                                                  {"culled", telemetry.frame_counters.culled_count},
                                                                  {"draws", telemetry.frame_counters.draw_commands},
                                                                  {"buffer_uploads", telemetry.frame_counters.buffer_upload_count},
                                                                  {"image_uploads", telemetry.frame_counters.image_upload_count},
                                                                  // R3/M5 per-pass draw 计数（M8 pass 瀑布数据源）
                                                                  {"shadow_draws", telemetry.frame_counters.shadow_draw_count},
                                                                  {"main_draws", telemetry.frame_counters.main_draw_count},
                                                                  {"debug_draws", telemetry.frame_counters.debug_draw_count},
                                                                  // M6/R5 resolve pass draw 计数
                                                                  {"resolve_draws", telemetry.frame_counters.resolve_draw_count}}},
                                                            }));

    publish_scene_telemetry_if_changed();
    publish_rg_telemetry_if_changed();
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
                                                                 {"geometry_arena",
                                                                  {{"count", report.geometry_arena_count},
                                                                   {"created", report.geometry_arenas_created},
                                                                   {"reserved_bytes", report.geometry_arena_reserved_bytes},
                                                                   {"used_bytes", report.geometry_arena_used_bytes},
                                                                   {"allocation_us", report.geometry_arena_allocation_us},
                                                                   {"plan_us", report.geometry_plan_us},
                                                                   {"transfer_us", report.geometry_transfer_us}}},
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
        // 帧首清零（发布窗口 = 整帧；sample 系统在 run_sample_systems 发布，
        // 若在 extract 里 clear 会冲掉其通道）。extract 只发布，不 clear。
        extract.channels.clear();
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
        if (telemetry.metrics_ring)
        {
            std::array<std::uint64_t, measure::counter_slot_count> counter_slots{};
            counter_slots[0] = telemetry.frame_counters.instance_count;
            counter_slots[1] = telemetry.frame_counters.visible_count;
            counter_slots[2] = telemetry.frame_counters.culled_count;
            counter_slots[3] = telemetry.frame_counters.draw_commands;
            counter_slots[4] = telemetry.frame_counters.buffer_upload_count;
            counter_slots[5] = telemetry.frame_counters.image_upload_count;
            // R3/M5 per-pass draw 计数（槽 6–8，槽 6–15 预留区间内）
            counter_slots[6] = telemetry.frame_counters.shadow_draw_count;
            counter_slots[7] = telemetry.frame_counters.main_draw_count;
            counter_slots[8] = telemetry.frame_counters.debug_draw_count;
            // M6/R5 resolve pass draw 计数（槽 9）
            counter_slots[9] = telemetry.frame_counters.resolve_draw_count;
            measure::push(*telemetry.metrics_ring,
                          static_cast<std::uint64_t>(
                              std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                                    frame_begin)
                                  .count()),
                          phase_us, counter_slots);
        }
        if (context.stop == frame_stop_reason::failure) return false;
        if (context.stop == frame_stop_reason::user_requested) return true;
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
        // 副作用归并——poll 只记请求行，执行在帧边界出口
        // （resize 在 submit 前边界，拾取在 publish_telemetry 出口）。
        if (event.type == interface::event_type::resize) ++resize_requests;
        if (event.type == interface::event_type::mouse_button_down &&
            event.mouse_button.button == interface::mouse_button::left &&
            camera_container.transforms[camera_entity_index].mode == interface::camera_mode::fly)
            pending_pick_request = pending_pick{.x = event.mouse_button.x, .y = event.mouse_button.y};
    }
    context.stop = window->should_close() ? frame_stop_reason::user_requested : frame_stop_reason::none;
}

void engine_runtime::consume_control_commands(frame_phase_context& context)
{
    if (context.stop != frame_stop_reason::user_requested) handle_control_plane_commands();
}

void engine_runtime::merge_asset_results(frame_phase_context& context)
{
    if (context.stop != frame_stop_reason::user_requested) collect_completed_loads();
}

void engine_runtime::update_scene_transforms(frame_phase_context&)
{
    // 脏行批量重算矩阵缓存（静态场景零矩阵数学）。transform/注册置脏，
    // extract 直读缓存列，不再逐对象 model_matrix。
    scene_registry.refresh_matrices();
}

void engine_runtime::update_cameras(frame_phase_context& context)
{
    if (context.stop != frame_stop_reason::user_requested) interface::tick(camera_container, camera_update_context, input_events, delta_time);
}

void engine_runtime::run_sample_systems(frame_phase_context& context)
{
    if (context.stop == frame_stop_reason::user_requested || !sample_definition.update) return;
    // 引擎侧通道发布（sample update 前）：debug 视图覆盖，持久成员裸指针（H1）。
    // 引擎是该通道唯一写者；sample 是 debug_view_request 的唯一写者，互不冲突。
    extract.channels.publish_state<engine::debug_view_override>(&debug_override);
    runtime_services services{
        .scene = scene_registry,
        .cameras = camera_container,
        .active_camera = camera_entity_index,
        .channels = extract.channels,
        .request_asset = [this](std::filesystem::path path)
        {
            auto requested = loads.asset_loader->request(std::move(path));
            if (requested) loads.pending_loads.emplace(requested.value, pending_load{});
            return requested;
        },
        .post_message = [](std::string_view message) { Logger::LogInfo(std::string(message)); },
    };
    sample_definition.update(services, delta_time);
}

void engine_runtime::extract_render_packet(frame_phase_context& context)
{
    if (context.stop == frame_stop_reason::user_requested) return;
    telemetry.frame_counters = {};
    extract.render_instances.clear();
    extract.render_transforms.clear();
    // 沿紧凑 slot 索引直读热列（visible/matrices/geometries），
    // 不触碰 scene_object 记录；矩阵在 update_scene_transforms 已刷新。
    const std::vector<std::size_t>& slots = scene_registry.slot_indices();
    extract.render_instances.reserve(slots.size());
    extract.render_transforms.reserve(slots.size());
    for (const std::size_t slot : slots)
    {
        if (scene_registry.visible_at(slot) == 0 ||
            scene_registry.geometry_at(slot) == engine::invalid_geometry_handle)
            continue;
        const std::uint32_t transform = static_cast<std::uint32_t>(extract.render_transforms.size());
        extract.render_transforms.push_back(scene_registry.matrix_at(slot));
        extract.render_instances.push_back({.mesh = scene_registry.geometry_at(slot), .transform = transform});
    }
    telemetry.frame_counters.instance_count = extract.render_instances.size();
    extract.render_cameras = {engine::camera_row{
        .view = interface::get_view_matrix(camera_container.transforms[camera_entity_index]),
        .projection = interface::get_projection_matrix(camera_container.transforms[camera_entity_index],
                                                       camera_container.configs[camera_entity_index]),
    }};

    // 相机挂了剔除组件且开启时，在 packet 输入侧做实例级视锥剔除。
    // 显隐过滤（scene_registry visible）在上方完成，剔除在其后；recipe/RG 零改动。
    extract.frame_instance_rows = extract.render_instances;
    if (camera_entity_index < culling.present.size() && culling.present[camera_entity_index] != 0 &&
        culling.enabled[camera_entity_index] != 0)
    {
        const glm::mat4 view_projection = extract.render_cameras.front().projection * extract.render_cameras.front().view;
        std::uint64_t culled = 0;
        extract.frame_instance_rows = engine::cull_instances(culling, camera_entity_index, view_projection,
                                                     extract.render_instances, extract.render_transforms,
                                                     extract.mesh_bounds_min, extract.mesh_bounds_max, &culled);
        telemetry.frame_counters.visible_count = extract.frame_instance_rows.size();
        telemetry.frame_counters.culled_count = culled;
    }
    else
    {
        telemetry.frame_counters.visible_count = extract.render_instances.size();
    }

    // 发布帧通道（camera / instance / transform；instance 为剔除后的行）。
    // 帧首已 clear（tick 循环），此处只发布——发布窗口 = 阶段表顺序，每通道单写者。
    extract.channels.publish_rows<engine::camera_row>(extract.render_cameras);
    extract.channels.publish_rows<engine::instance_row>(extract.frame_instance_rows);
    extract.channels.publish_rows<glm::mat4>(extract.render_transforms);
}

void engine_runtime::apply_resource_changes(frame_phase_context& context)
{
    if (context.stop == frame_stop_reason::user_requested) return;
    apply_completed_loads();
    // T1b/M7：分块流式上传逐帧排空（每帧一片）；失败路径会把已上传句柄并入
    // pending_geometry_retires，随后的 retire 边界同帧回收。
    drain_streamed_uploads();
    if (loads.pending_geometry_retires.empty()) return;
    const auto changed = renderer->apply_resource_changes({.geometry_retires = loads.pending_geometry_retires});
    if (!changed)
    {
        Logger::LogError("Failed to retire geometry resources: " + changed.error);
        context.stop = frame_stop_reason::failure;
        return;
    }
    loads.pending_geometry_retires.clear();
}

void engine_runtime::submit_render_packet(frame_phase_context& context)
{
    if (context.stop == frame_stop_reason::user_requested) return;
    // resize 请求在提交前边界执行（poll_events 只计数）
    if (resize_requests > 0)
    {
        renderer->request_resize();
        resize_requests = 0;
    }
    // render_this_frame 降为局部变量（是否暂停仅本阶段相关）
    bool render_this_frame = !frame_paused;
    if (pending_frame_steps > 0)
    {
        --pending_frame_steps;
        render_this_frame = true;
    }
    if (!render_this_frame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return;
    }
    const engine::render_frame_packet packet{
        .frame_serial = extract.frame_serial++,
        .channels = &extract.channels,
        .counters = &telemetry.frame_counters,
    };
    const engine::frame_status status = renderer->render(packet);
    context.stop = status == engine::frame_status::failed ? frame_stop_reason::failure : context.stop;
    context.rendered = status == engine::frame_status::rendered;
    if (status == engine::frame_status::skipped)
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
}

void engine_runtime::publish_telemetry(frame_phase_context& context)
{
    if (context.stop != frame_stop_reason::none) return;
    // 拾取请求在遥测出口执行（poll_events 只记坐标；命中变化随本帧遥测发布）
    if (pending_pick_request)
    {
        try_pick_object(pending_pick_request->x, pending_pick_request->y);
        pending_pick_request.reset();
    }
    publish_frame_telemetry();
}

void engine_runtime::shutdown() noexcept
{
    if (control_plane)
    {
        control_plane->stop();
    }
    if (loads.asset_loader)
    {
        loads.asset_loader->shutdown();
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
    loads.initial_geometry = std::move(asset);
}

void engine_runtime::set_required_startup_asset(std::filesystem::path path)
{
    loads.required_startup_asset = std::move(path);
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
