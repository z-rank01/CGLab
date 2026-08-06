#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <gltf/gltf_data.h>

#include "_interface/window.h"
#include "vulkan_sample.h"
#include "_interface/camera_component.h"
#include "_interface/camera_system.h"
#include "control_plane/control_plane_server.h"
#include "scene/scene_registry.h"

class app_sample
{
public:
    app_sample(engine_config config, control_plane::control_plane_config ui_config = {});
    ~app_sample();
    // core public function
    void initialize();
    [[nodiscard]] bool tick(std::optional<std::uint64_t> frame_limit = std::nullopt);
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t validation_error_count() const noexcept;
    [[nodiscard]] vulkan_run_statistics statistics() const noexcept { return run_statistics; }

    void set_vertex_index_data(std::vector<gltf::PerDrawCallData> per_draw_call_data, std::vector<uint32_t> indices, std::vector<gltf::Vertex> vertices);
    void set_mesh_list(const std::vector<gltf::PerMeshData>& mesh_list);

private:
    // core member
    std::unique_ptr<interface::window> window;
    std::unique_ptr<vulkan_sample> vulkan_instance;
    std::shared_ptr<std::atomic_uint32_t> validation_errors;
    vulkan_run_statistics run_statistics;
    std::vector<interface::input_event> input_events;

    // data-oriented camera
    size_t camera_entity_index = 0;
    interface::camera_container camera_container;
    interface::camera_update_context camera_update_context;

    // delta time tracking
    std::chrono::high_resolution_clock::time_point last_frame_time;
    float delta_time;

    // control plane (Web UI 控制平面)
    control_plane::control_plane_config ui_config;
    std::unique_ptr<control_plane::control_plane_server> control_plane;
    bool frame_paused = false;
    std::uint32_t pending_frame_steps = 0;
    float telemetry_accumulator = 0.0F;
    float smoothed_frame_time = 1.0F / 60.0F;

    // --- P2 scene system ---
    scene::scene_registry scene_registry;
    // 启动资产（legacy buffer）登记的只读条目；unload 时用于查 arena 回收区间
    std::vector<std::optional<staged_geometry>> runtime_geometry_slots;
    std::uint64_t last_published_scene_revision = 0;

    // 异步加载：worker 线程只解析 glTF（纯 CPU），结果包在帧边界由主线程 staging + 注册
    struct load_job
    {
        std::string path;
        std::string client_id;
        nlohmann::json rpc_id;
    };
    struct load_result
    {
        std::string client_id;
        nlohmann::json rpc_id;
        bool succeeded = false;
        std::string error;
        std::string name;
        scene::aabb bounds{};
        std::vector<gltf::PerDrawCallData> primitives; // 节点变换已烘焙进顶点（worker 侧完成）
    };
    std::thread load_worker;
    std::mutex load_mutex;
    std::condition_variable load_cv;
    std::deque<load_job> load_queue;
    std::deque<load_result> load_results;
    bool load_worker_stop = false;

    void handle_control_plane_commands();
    void handle_scene_command(const control_plane::engine_command& command);
    void publish_frame_telemetry();
    void publish_scene_telemetry_if_changed();
    [[nodiscard]] nlohmann::json current_camera_state() const;
    [[nodiscard]] nlohmann::json current_scene_state() const;

    // 异步加载管线
    void enqueue_load(load_job job);
    void load_worker_loop() noexcept;
    void drain_completed_loads();
    void stop_load_worker() noexcept;

    // fly 模式左键拾取：窗口坐标 → 相机射线 → registry pick
    void try_pick_object(float x, float y);

    engine_config general_config;
};
