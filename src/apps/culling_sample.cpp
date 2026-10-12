#include <filesystem>

#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "apps/gltf_render_recipe.h"

// CullingSample：GltfSponzaSample 的剔除演示变体。
// - 启动相机恒带视锥剔除组件（CullingSample 的定位就是演示剔除能力）；
// - 复用 gltf recipe + sponza 资产，渲染侧零改动；
// - dev console 可经 camera.set_culling 运行时开关，观察
//   telemetry.frame.counters.{visible,culled,draws} 与 fps 变化。
// TriangleSample / GltfSponzaSample 默认不带剔除（行为不变），可选 --culling 开启。

namespace
{
    std::filesystem::path startup_asset(const apps::application_options& options)
    {
        if (options.asset_path)
        {
            const std::filesystem::path path = *options.asset_path;
            return path.is_relative() ? (std::filesystem::path(CGLAB_SOURCE_DIR) / path).lexically_normal() : path.lexically_normal();
        }
        if (options.smoke_test)
        {
            return std::filesystem::path(CGLAB_SOURCE_DIR) / "assets" / "triangle.gltf";
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    return apps::run_application(
        argc,
        argv,
        "CullingSample",
        [](const apps::application_options& options)
        {
            const std::filesystem::path asset_path = startup_asset(options);
            if (asset_path.empty() || !std::filesystem::is_regular_file(asset_path))
            {
                return apps::application_setup_result{
                    .error = "A valid .gltf or .glb asset is required; pass --asset <path>",
                };
            }

            engine::runtime_config config{
                .window            = {.title = "CullingSample", .width = 1280, .height = 720},
                .working_directory = CGLAB_SOURCE_DIR,
                .frames_in_flight  = 3,
                .validation        = options.validation,
                .control_plane =
                    {
                        .enabled      = !options.no_ui && (!options.smoke_test || options.ui_port_specified),
                        .port         = options.ui_port,
                        .open_browser = options.ui_open_browser,
                    },
            };
            const auto frames = options.smoke_test ? std::optional<std::uint64_t>(options.frame_limit.value_or(3)) : options.frame_limit;
            // A3-2：本样本原先漏设契约字段，"三字段缺省"又无声地等于"不校验"——
            // 契约对它形同虚设。现在字段语义显式化并补齐：pipeline 恒 7（debug 管线
            // 无条件创建）、raster pass 3（debug off；本样本未接 --debug-view）、
            // 间接分组数由 driver 从 frame_plan 归约，不需要声明。
            return apps::application_setup_result{.request = apps::application_run_request{
                                                      .runtime     = std::move(config),
                                                      .sample      = {.name = "CullingSample", .required_startup_asset = asset_path.string()},
                                                      .renderer    = apps::create_gltf_render_driver(),
                                                      .frame_limit = frames,
                                                      .require_validation_clean = options.validation,
                                                      .enforce_smoke_contract   = options.smoke_test,
                                                      .expected_pipeline_creations = 7,
                                                      .expected_raster_passes_per_frame = 3,
                                                      .culling_enabled          = true,
                                                  }};
        },
        {.accepts_asset = true});
}
