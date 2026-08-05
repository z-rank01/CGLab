#pragma once
#include <chrono>
#include <cstdint>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "_interface/window.h"
#include "vulkan_sample.h"
#include "_interface/camera_component.h"
#include "_interface/camera_system.h"

class app_sample
{
public:
    app_sample(engine_config config);
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

    // data-oriented camera
    size_t camera_entity_index = 0;
    interface::camera_container camera_container;
    interface::camera_update_context camera_update_context;

    // delta time tracking
    std::chrono::high_resolution_clock::time_point last_frame_time;
    float delta_time;
    

    engine_config general_config;
};
