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
        std::uint64_t expected_indirect_groups_per_frame = 3; // shadow + 主 pass + resolve（debug 开启时为 4）
        std::uint64_t expected_draw_passes_per_frame = 3;     // 每帧 raster pass 数（gltf 三 pass；debug 开启时为 4；triangle 覆盖为 1）
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
