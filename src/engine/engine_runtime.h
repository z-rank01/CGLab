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
#include "engine/culling_manager.h"
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
    // H1：启动路径错误经 result 返回（不再 throw；runner 已有 catch 边界兜底）
    [[nodiscard]] engine::result<bool> initialize();
    [[nodiscard]] bool tick(std::optional<std::uint64_t> frame_limit = std::nullopt);
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t validation_error_count() const noexcept;
    [[nodiscard]] engine::render_statistics statistics() const noexcept { return run_statistics; }
    // 末帧计数器（R3/M5）：smoke 契约按帧校验 per-pass draw 计数（主 pass = 全量、
    // 阴影 pass ≤ 主 pass、debug pass 仅调试模式画 quad）。
    [[nodiscard]] const engine::frame_counters& last_frame_counters() const noexcept
    {
        return telemetry.frame_counters;
    }

    void set_initial_geometry(engine::asset_database asset);
    void set_required_startup_asset(std::filesystem::path path);
    void configure_sample(sample definition);
    // 给活动相机挂载/开关视锥剔除组件（能力可运行时增删，直通相机不受影响）。
    void set_camera_culling(bool enabled);

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
    // debug.set_view 覆盖状态（M8/B2）：持久成员，每帧裸指针发布进帧通道（H1 口径），
    // sample 侧翻译为 apps::debug_view_request。active=false 时 sample 回退 CLI。
    engine::debug_view_override debug_override{};

    // --- scene system ---
    scene::scene_registry scene_registry;
    sample sample_definition;

    // 成员按职责分簇（帧边界运行状态分组；组织性，无行为变化）。
    // --- 异步加载簇：worker 解析 glTF（纯 CPU），结果包在帧边界由主线程 staging + 注册 ---
    struct pending_load
    {
        std::string client_id;
        nlohmann::json rpc_id;
        bool respond = false;
    };
    // T1b/M7 分块流式上传：大资产按每帧字节预算分片 apply（M2 arena 池游标跨帧
    // 续传），完成前资产由主线程持有；进度经 A0 协议（telemetry.load_progress）
    // 逐片上报，完成后统一注册场景（对象渐进可见的语义由注册时机决定）。
    struct streamed_upload
    {
        engine::asset_database asset;
        std::string client_id;
        nlohmann::json rpc_id;
        bool respond = false;
        std::uint32_t material_base = 0;
        bool materials_uploaded = false;
        std::uint32_t next_mesh = 0;              // 下一片起点（mesh 句柄边界）
        std::vector<engine::geometry_handle> mesh_handles;
        std::uint64_t uploaded_bytes = 0;         // 进度：已上传 vertex+index 字节
        std::uint64_t total_bytes = 0;
        engine::load_report report;               // 完成时回填（worker 段 + 分片合计）
        std::chrono::steady_clock::time_point upload_begin{};
    };
    struct frame_loads
    {
        std::unique_ptr<asset_service> asset_loader;
        std::unordered_map<asset_request_id, pending_load> pending_loads;
        std::unordered_map<engine::geometry_handle, std::uint32_t> geometry_ref_counts;
        std::vector<completed_asset_request> completed_asset_rows;
        std::vector<geometry_retire_row> pending_geometry_retires;
        std::vector<streamed_upload> streamed_uploads;
        std::optional<engine::asset_database> initial_geometry;
        std::optional<engine::asset_database> initial_asset;
        std::optional<std::filesystem::path> required_startup_asset;
    };
    frame_loads loads;

    // --- 提取簇：packet 行表缓冲与剔除输入 ---
    struct frame_extract
    {
        // 场景实例到 RG persistent geometry handle 的行映射。
        std::vector<std::optional<engine::geometry_handle>> runtime_geometry_slots;
        std::vector<engine::camera_row> render_cameras;
        std::vector<engine::instance_row> render_instances;
        std::vector<glm::mat4> render_transforms;
        std::uint64_t frame_serial = 0;
        // 按 geometry_handle 索引的 mesh 级 world bounds（merge 时维护，extract 剔除消费）
        std::vector<glm::vec3> mesh_bounds_min;
        std::vector<glm::vec3> mesh_bounds_max;
        // 本帧实际提交给 recipe 的实例行（剔除后指向 culling scratch，否则指向 render_instances）
        std::span<const engine::instance_row> frame_instance_rows;
        // 帧通道行表（extract 发布，submit 经 packet 传递，生存期 = 单帧）
        engine::frame_channels channels;
    };
    frame_extract extract;

    // --- 遥测簇：测量设施与发布状态 ---
    struct frame_telemetry
    {
        // metrics_ring 约 885KB，必须堆上持有（禁止栈上实例化）。
        std::unique_ptr<measure::metrics_ring> metrics_ring;
        // 每帧清零，extract 填实例/可见/剔除，recipe 回填 draw/upload
        engine::frame_counters frame_counters;
        float telemetry_accumulator = 0.0F;
        float smoothed_frame_time = 1.0F / 60.0F;
        std::uint64_t last_published_scene_revision = 0;
    };
    frame_telemetry telemetry;

    // --- 视锥剔除 ---
    engine::culling_manager culling;

    // --- 帧边界副作用归并（poll_events 只记请求行，执行在帧边界出口）---
    std::uint32_t resize_requests = 0;
    struct pending_pick { float x = 0.0F; float y = 0.0F; };
    std::optional<pending_pick> pending_pick_request;

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
    // T1b/M7 分块流式上传：运行时资产按片上传（每帧一片），完成前不注册场景。
    void begin_streamed_upload(completed_asset_request& completed, pending_load response);
    void drain_streamed_uploads();
    void finalize_streamed_upload(streamed_upload& upload);
    void publish_load_progress(const streamed_upload& upload) const;
    // 上传（一次性或分片）与场景注册拆开：启动路径两者紧邻（行为不变），
    // 运行时路径上传分片、注册在全部片完成后一次完成。
    [[nodiscard]] engine::result<std::uint32_t> upload_asset_materials(const engine::asset_database& asset,
                                                                       engine::load_report* report);
    [[nodiscard]] engine::result<engine::resource_change_result> upload_asset_geometry(
        const engine::asset_database& asset,
        std::uint32_t material_base,
        std::uint32_t first_mesh,
        std::uint32_t mesh_count,
        engine::load_report* report);
    [[nodiscard]] std::vector<scene::object_id> register_asset_scene(
        const engine::asset_database& asset,
        std::span<const engine::geometry_handle> mesh_handles,
        bool read_only,
        engine::load_report* report);
    [[nodiscard]] std::vector<scene::object_id> merge_asset_database(engine::asset_database asset, bool read_only,
                                                                     engine::load_report* report = nullptr);

    // 帧阶段控制流：枚举代替布尔——stop 语义互斥（窗口关闭 / 错误），
    // rendered 独立标记本帧是否实际提交；render_this_frame 降为 submit 局部变量。
    enum class frame_stop_reason : std::uint8_t
    {
        none = 0,
        user_requested,  // 窗口关闭（poll_events 检测）
        failure,         // engine/backend 错误
    };
    struct frame_phase_context
    {
        frame_stop_reason stop = frame_stop_reason::none;
        bool rendered = false;
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
