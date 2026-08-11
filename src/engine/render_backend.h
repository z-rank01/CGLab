#pragma once

#include <cstdint>
#include <span>
#include <string>

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

    struct render_frame_packet
    {
        std::uint64_t frame_serial = 0;
        std::span<const camera_row> camera_rows;
        std::span<const instance_row> instance_rows;
        std::span<const glm::mat4> transform_rows;
        std::span<const std::uint32_t> material_handles;
        std::span<const geometry_handle> mesh_handles;
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

    struct geometry_upload_row { const geometry_asset* asset = nullptr; };
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
        [[nodiscard]] result<geometry_handle> upload_geometry(const geometry_asset& asset)
        {
            const geometry_upload_row row{&asset};
            const auto changed = api->apply_resource_changes(state, {.geometry_uploads = std::span(&row, 1)});
            return changed ? result<geometry_handle>{.value = changed.value.geometry_handles.front()}
                           : result<geometry_handle>{.error = changed.error};
        }
        [[nodiscard]] result<std::uint32_t> upload_materials(const asset_database& asset)
        {
            const material_upload_row row{&asset};
            const auto changed = api->apply_resource_changes(state, {.material_uploads = std::span(&row, 1)});
            return changed ? result<std::uint32_t>{.value = changed.value.material_bases.front()}
                           : result<std::uint32_t>{.error = changed.error};
        }
        void retire_geometry(geometry_handle handle)
        {
            const geometry_retire_row row{handle};
            (void)api->apply_resource_changes(state, {.geometry_retires = std::span(&row, 1)});
        }
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
