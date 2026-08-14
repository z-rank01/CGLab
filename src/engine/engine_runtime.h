#pragma once
#include <chrono>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "_interface/window.h"
#include "_interface/camera_component.h"
#include "_interface/camera_system.h"
#include "control_plane/control_plane_server.h"
#include "engine/render_backend.h"
#include "engine/asset_service.h"
#include "engine/runtime_config.h"
#include "engine/sample.h"
#include "measure/frame_metrics.h"
#include "scene/scene_registry.h"

namespace engine
{
class engine_runtime
{
public:
    engine_runtime(runtime_config config,
                   engine::render_driver renderer,
                   std::unique_ptr<interface::window> platform_window = {},
                   std::unique_ptr<asset_service> assets = {});
    ~engine_runtime();
    // core public function
    void initialize();
    [[nodiscard]] bool tick(std::optional<std::uint64_t> frame_limit = std::nullopt);
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t validation_error_count() const noexcept;
    [[nodiscard]] engine::render_statistics statistics() const noexcept { return run_statistics; }

    void set_initial_geometry(engine::geometry_asset asset);
    void set_required_startup_asset(std::filesystem::path path);
    void configure_sample(sample definition);

private:
    // core member
    std::unique_ptr<interface::window> window;
    engine::render_driver renderer;
    engine::render_statistics run_statistics;
    std::uint32_t final_validation_errors = 0;
    std::vector<interface::input_event> input_events;

    // data-oriented camera
    size_t camera_entity_index = 0;
    interface::camera_container camera_container;
    interface::camera_update_context camera_update_context;

    // delta time tracking
    std::chrono::high_resolution_clock::time_point last_frame_time;
    float delta_time;

    // control plane (Web UI 控制平面)
    std::unique_ptr<control_plane::control_plane_server> control_plane;
    bool frame_paused = false;
    std::uint32_t pending_frame_steps = 0;
    float telemetry_accumulator = 0.0F;
    float smoothed_frame_time = 1.0F / 60.0F;

    // --- P2 scene system ---
    scene::scene_registry scene_registry;
    // 场景实例到 RG persistent geometry handle 的行映射。
    std::vector<std::optional<engine::geometry_handle>> runtime_geometry_slots;
    std::optional<engine::geometry_asset> initial_geometry;
    std::optional<engine::asset_database> initial_asset;
    std::optional<std::filesystem::path> required_startup_asset;
    std::vector<engine::camera_row> render_cameras;
    std::vector<engine::instance_row> render_instances;
    std::vector<glm::mat4> render_transforms;
    std::uint64_t frame_serial = 0;
    sample sample_definition;
    std::uint64_t last_published_scene_revision = 0;

    // 异步加载：worker 线程只解析 glTF（纯 CPU），结果包在帧边界由主线程 staging + 注册
    struct pending_load
    {
        std::string client_id;
        nlohmann::json rpc_id;
        bool respond = false;
    };
    std::unique_ptr<asset_service> asset_loader;
    std::unordered_map<asset_request_id, pending_load> pending_loads;
    std::unordered_map<engine::geometry_handle, std::uint32_t> geometry_ref_counts;
    std::vector<completed_asset_request> completed_asset_rows;
    std::vector<geometry_retire_row> pending_geometry_retires;

    // --- A0 测量设施 ---
    // metrics_ring 约 885KB，必须堆上持有（禁止栈上实例化）。
    std::unique_ptr<measure::metrics_ring> metrics_ring;
    engine::frame_counters frame_counters; // 每帧清零，extract 填实例/可见/剔除，recipe 回填 draw/upload

    void handle_control_plane_commands();
    void handle_scene_command(const control_plane::engine_command& command);
    void publish_frame_telemetry();
    void publish_scene_telemetry_if_changed();
    void publish_load_telemetry(const engine::load_report& report) const;
    [[nodiscard]] nlohmann::json current_camera_state() const;
    [[nodiscard]] nlohmann::json current_scene_state() const;

    // 异步加载管线
    void enqueue_load(std::string path, std::string client_id, nlohmann::json rpc_id);
    void collect_completed_loads();
    void apply_completed_loads();
    [[nodiscard]] std::vector<scene::object_id> merge_asset_database(engine::asset_database asset, bool read_only,
                                                                     engine::load_report* report = nullptr);

    struct frame_phase_context
    {
        bool stop_success = false;
        bool stop_failure = false;
        bool rendered = false;
        bool render_this_frame = true;
    };
    using frame_phase = void (engine_runtime::*)(frame_phase_context&);
    void poll_events(frame_phase_context&);
    void consume_control_commands(frame_phase_context&);
    void merge_asset_results(frame_phase_context&);
    void update_scene_transforms(frame_phase_context&);
    void update_cameras(frame_phase_context&);
    void run_sample_systems(frame_phase_context&);
    void extract_render_packet(frame_phase_context&);
    void apply_resource_changes(frame_phase_context&);
    void submit_render_packet(frame_phase_context&);
    void publish_telemetry(frame_phase_context&);

    // fly 模式左键拾取：窗口坐标 → 相机射线 → registry pick
    void try_pick_object(float x, float y);

    runtime_config config;
};
} // namespace engine
