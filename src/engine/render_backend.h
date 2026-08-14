#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/geometry.h"

namespace interface
{
    class window;
}

namespace engine
{
    enum class frame_status
    {
        rendered,
        skipped,
        failed,
    };

    struct render_statistics
    {
        std::uint64_t upload_pass_executions = 0;
        std::uint64_t draw_pass_executions = 0;
        std::uint64_t presented_frames = 0;
        std::uint64_t steady_frame_descriptor_updates = 0;
        std::uint64_t pipeline_creations = 0;
        std::uint64_t indirect_groups = 0;
    };

    struct camera_row
    {
        glm::mat4 view{1.0F};
        glm::mat4 projection{1.0F};
    };

    struct instance_row
    {
        geometry_handle mesh = invalid_geometry_handle;
        std::uint32_t transform = 0;
    };

    // 帧计数器（A0）：engine 填 instance/visible/culled，recipe 填 draw/upload 行数。
    // 主线程单写者：engine 在 extract 阶段清零并填自己的字段，recipe 在 build_frame 只写自己的字段。
    // 命名约定：标量计数用 *_count（*_rows 只用于真正的行表/span）。
    struct frame_counters
    {
        std::uint64_t instance_count = 0;
        std::uint64_t visible_count = 0;
        std::uint64_t culled_count = 0;
        std::uint64_t draw_commands = 0;
        std::uint64_t buffer_upload_count = 0;
        std::uint64_t image_upload_count = 0;
    };

    // 加载分段报告（A0）：worker 填 load_us（dcl::load_gltf 全程，单次黑盒调用），
    // 主线程在 merge 边界补 merge_us/upload_us。parse/convert/decode 三段细分依赖
    // DCL 侧可选计时装点，未装点时保持 0。
    struct load_report
    {
        std::string path;
        std::uint64_t load_us = 0;
        std::uint64_t merge_us = 0;
        std::uint64_t upload_us = 0;
        std::uint64_t vertex_bytes = 0;
        std::uint64_t index_bytes = 0;
        std::uint32_t image_count = 0;
        std::uint64_t parse_us = 0;
        std::uint64_t convert_us = 0;
        std::uint64_t decode_us = 0;
    };

    struct render_frame_packet
    {
        std::uint64_t frame_serial = 0;
        std::span<const camera_row> camera_rows;
        std::span<const instance_row> instance_rows;
        std::span<const glm::mat4> transform_rows;
        std::span<const std::uint32_t> material_handles;
        std::span<const geometry_handle> mesh_handles;
        // 帧计数回填（可空）：recipe 只写自己的字段（draw/upload 行数）。
        frame_counters* counters = nullptr;
    };

    struct backend_config
    {
        std::string application_name;
        std::string working_directory;
        std::uint8_t frames_in_flight = 3;
        bool validation = false;
    };

    template <typename T>
    struct result
    {
        T value{};
        std::string error;

        [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
    };

    // A2 批量行：直接引用共享 blob 行模型（dcl::asset_database），零拷贝上传。
    // 覆盖 [first_mesh, first_mesh + mesh_count) 的 mesh 行段；apply 返回
    // geometry_handles，按 mesh 顺序每 mesh 一个句柄（整资产单事务）。
    struct geometry_upload_row
    {
        const asset_database* asset = nullptr;
        std::uint32_t first_mesh = 0;
        std::uint32_t mesh_count = 0;     // ≥1
        std::uint32_t material_base = 0;  // 材质上传返回的 base（recipe 需要）
    };
    struct material_upload_row { const asset_database* asset = nullptr; };
    struct geometry_retire_row { geometry_handle handle = invalid_geometry_handle; };
    struct resource_change_batch
    {
        std::span<const geometry_upload_row> geometry_uploads;
        std::span<const material_upload_row> material_uploads;
        std::span<const geometry_retire_row> geometry_retires;
    };
    struct resource_change_result
    {
        std::vector<geometry_handle> geometry_handles;
        std::vector<std::uint32_t> material_bases;
    };

    struct render_driver_api
    {
        result<bool> (*initialize)(void*, interface::window&, const backend_config&);
        result<resource_change_result> (*apply_resource_changes)(void*, resource_change_batch);
        frame_status (*render)(void*, const render_frame_packet&);
        void (*request_resize)(void*) noexcept;
        void (*shutdown)(void*) noexcept;
        render_statistics (*statistics)(const void*) noexcept;
        std::uint32_t (*validation_error_count)(const void*) noexcept;
        void (*destroy)(void*) noexcept;
    };

    class render_driver
    {
    public:
        render_driver() = default;
        render_driver(void* driver_state, const render_driver_api* driver_api) : state(driver_state), api(driver_api) {}
        ~render_driver() { reset(); }
        render_driver(const render_driver&) = delete;
        render_driver& operator=(const render_driver&) = delete;
        render_driver(render_driver&& other) noexcept : state(other.state), api(other.api)
        {
            other.state = nullptr;
            other.api = nullptr;
        }
        render_driver& operator=(render_driver&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                state = other.state;
                api = other.api;
                other.state = nullptr;
                other.api = nullptr;
            }
            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return state != nullptr && api != nullptr; }
        [[nodiscard]] render_driver* operator->() noexcept { return this; }
        [[nodiscard]] const render_driver* operator->() const noexcept { return this; }
        [[nodiscard]] result<bool> initialize(interface::window& window, const backend_config& config)
        { return api->initialize(state, window, config); }
        [[nodiscard]] result<resource_change_result> apply_resource_changes(resource_change_batch batch)
        { return api->apply_resource_changes(state, batch); }
        [[nodiscard]] frame_status render(const render_frame_packet& packet) { return api->render(state, packet); }
        void request_resize() noexcept { api->request_resize(state); }
        void shutdown() noexcept { if (state) api->shutdown(state); }
        [[nodiscard]] render_statistics statistics() const noexcept { return api->statistics(state); }
        [[nodiscard]] std::uint32_t validation_error_count() const noexcept
        { return api->validation_error_count(state); }

    private:
        void reset() noexcept
        {
            if (state && api && api->destroy) api->destroy(state);
            state = nullptr;
            api = nullptr;
        }
        void* state = nullptr;
        const render_driver_api* api = nullptr;
    };
} // namespace engine
