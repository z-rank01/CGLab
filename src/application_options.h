#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct application_options
{
    std::optional<std::filesystem::path> config_path;
    std::optional<std::filesystem::path> asset_path;
    std::optional<std::uint64_t> frame_limit;
    bool validation = false;
    bool smoke_test = false;
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

[[nodiscard]] application_options_result parse_application_options(std::span<const std::string_view> arguments);
[[nodiscard]] std::string application_usage(std::string_view executable_name);
[[nodiscard]] std::filesystem::path resolve_asset_path(
    const application_options& options,
    const std::filesystem::path& configured_asset_path);
