#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "_interface/window.h"
#include "apps/application_options.h"
#include "engine/render_backend.h"
#include "engine/runtime_config.h"
#include "engine/sample.h"

namespace apps
{
    struct application_run_request
    {
        engine::runtime_config runtime;
        engine::sample sample;
        engine::render_driver renderer;
        // Null lets the runner create the default platform window.
        std::unique_ptr<interface::window> window;
        std::optional<std::uint64_t> frame_limit;
        bool require_validation_clean = false;
        bool enforce_smoke_contract = false;
        std::uint64_t expected_pipeline_creations = 7; // gltf: 4 材质组 + 1 shadow + 1 debug + 1 resolve（M6 恒建）；triangle 覆盖为 1
        // A3-2：每帧 raster pass 数（recipe 声明的结构常量；gltf 三 pass，debug 开启 4，
        // triangle 1）。该值必须由样本显式给出——它是"pass 结构被改而样本没同步"的唯一牙齿。
        // 注意：这不是"有 draw 的 pass 数"，后端按 pass 计数（vulkan_device.cpp draw_pass_executions）。
        std::uint64_t expected_raster_passes_per_frame = 0; // 0 = 不校验该列
        // A3-2：每帧间接绘制**分组数**不再由样本声明：资产分段（arena 段）会让它随资产
        // 规模变化（triangle=3 / 大资产=5），硬编码必然漂移。契约改为两路独立——
        // 期望由 driver 从 frame_plan 归约（render_statistics.last_frame_draw_groups），
        // 实测取后端累计 indirect_groups，二者必须相等。
        // 启动即给活动相机挂载视锥剔除组件（CullingSample 恒开）。
        bool culling_enabled = false;
    };

    struct application_setup_result
    {
        std::optional<application_run_request> request;
        std::string error;
    };

    using application_setup = std::function<application_setup_result(const application_options&)>;

    [[nodiscard]] int run_application(application_run_request request);
    [[nodiscard]] int run_application(int argc,
                                      char** argv,
                                      std::string_view executable_name,
                                      const application_setup& setup,
                                      application_cli cli = {});
}
