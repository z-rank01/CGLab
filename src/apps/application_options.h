#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace apps
{
    // 调试视图模式（R4/M3 + M6/R5）：recipe 按模式追加 debug pass，把中间 RT
    // （阴影图/半分辨率 RT）渲染到屏幕角落 inset。off = 不加 pass（默认，零开销）。
    enum class debug_view_mode : std::uint8_t
    {
        off = 0,
        shadow = 1,  // 阴影图原始深度（近=白）
        depth = 2,   // 线性化距离热力图（近=蓝，远=红）
        hdr = 3,     // 半分辨率 RT 原始（M6/R5，tonemap 前）
        resolved = 4, // 半分辨率 RT + ACES tonemap（M6/R5，与 resolve pass 同款）
    };

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

        // 启动即给活动相机挂载视锥剔除组件（CullingSample 恒开，其余默认关）。
        bool culling = false;

        // 调试视图（--debug-view shadow|depth|off）：样本经帧通道发布请求，
        // recipe 决定是否追加 debug pass；引擎零改动。
        debug_view_mode debug_view = debug_view_mode::off;
    };

    enum class application_options_status : std::uint8_t
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
