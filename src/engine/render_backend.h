#pragma once

#include <cstdint>
#include <memory>
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
    };

    struct render_object
    {
        geometry_handle geometry = invalid_geometry_handle;
        glm::mat4 model{1.0F};
    };

    struct render_snapshot
    {
        std::uint64_t frame_serial = 0;
        glm::mat4 view{1.0F};
        glm::mat4 projection{1.0F};
        std::span<const render_object> objects;
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

    class render_backend
    {
    public:
        virtual ~render_backend() = default;

        [[nodiscard]] virtual result<bool> initialize(interface::window& window, const backend_config& config) = 0;
        [[nodiscard]] virtual result<geometry_handle> upload_geometry(const geometry_asset& asset) = 0;
        virtual void retire_geometry(geometry_handle handle) = 0;
        virtual void request_resize() noexcept = 0;
        [[nodiscard]] virtual frame_status render(const render_snapshot& snapshot) = 0;
        virtual void shutdown() noexcept = 0;
        [[nodiscard]] virtual std::uint32_t validation_error_count() const noexcept = 0;
        [[nodiscard]] virtual render_statistics statistics() const noexcept = 0;
    };
} // namespace engine
