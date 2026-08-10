#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace apps
{
    struct application_cli
    {
        bool accepts_asset = false;
    };

    struct application_options
    {
        std::optional<std::filesystem::path> asset_path;
        std::optional<std::uint64_t> frame_limit;
        bool validation = false;
        bool smoke_test = false;

        // Control plane (Web UI) options.
        // The control plane is enabled by default for interactive runs and
        // disabled for smoke runs unless a port is explicitly supplied.
        std::uint16_t ui_port = 17381;
        bool ui_port_specified = false;
        bool no_ui = false;
        bool ui_open_browser = false;
    };

    enum class application_options_status
    {
        success,
        help,
        error,
    };

    struct application_options_result
    {
        application_options_status status = application_options_status::success;
        application_options options;
        std::string message;

        [[nodiscard]] bool succeeded() const noexcept { return status == application_options_status::success; }
    };

    [[nodiscard]] application_options_result parse_application_options(
        std::span<const std::string_view> arguments,
        application_cli cli = {});
    [[nodiscard]] std::string application_usage(std::string_view executable_name, application_cli cli = {});
} // namespace apps
