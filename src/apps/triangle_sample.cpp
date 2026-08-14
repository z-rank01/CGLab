#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "apps/triangle_render_recipe.h"

namespace
{
    // 启动几何改为单 mesh 的 dcl 行模型（共享 blob），与 glTF 加载路径同一契约。
    engine::asset_database make_triangle()
    {
        engine::asset_database asset{.name = "Triangle"};
        asset.vertex_blob = {
            {{-1.0F, -1.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 1.0F}},
            {{1.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 1.0F}},
            {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 1.0F}},
        };
        asset.index_blob = {0, 1, 2};
        asset.meshes.push_back({.name = "Triangle", .first_primitive = 0, .primitive_count = 1,
                                .bounds_min = {-1.0F, -1.0F, 0.0F}, .bounds_max = {1.0F, 1.0F, 0.0F}});
        asset.primitives.push_back({.mesh = 0, .material = 0,
                                    .vertex_offset = 0, .vertex_count = 3,
                                    .index_offset = 0, .index_count = 3});
        asset.nodes.push_back({.name = "Triangle", .parent = engine::invalid_asset_index, .mesh = 0});
        asset.materials.emplace_back();
        return asset;
    }
}

int main(int argc, char** argv)
{
    return apps::run_application(argc, argv, "TriangleSample", [](const apps::application_options& options)
    {
        engine::runtime_config config{
            .window = {.title = "TriangleSample", .width = 1280, .height = 720},
            .working_directory = CGLAB_SOURCE_DIR,
            .frames_in_flight = 3,
            .validation = options.validation,
            .control_plane = {
                .enabled = !options.no_ui && (!options.smoke_test || options.ui_port_specified),
                .port = options.ui_port,
                .open_browser = options.ui_open_browser,
            },
        };
        const auto frames = options.smoke_test
                                ? std::optional<std::uint64_t>(options.frame_limit.value_or(3))
                                : options.frame_limit;
        return apps::application_setup_result{.request = apps::application_run_request{
                                                  .runtime = std::move(config),
                                                  .sample = {.name = "TriangleSample", .startup_geometry = make_triangle()},
                                                  .renderer = apps::create_triangle_render_driver(),
                                                  .frame_limit = frames,
                                                  .require_validation_clean = options.validation,
                                                  .enforce_smoke_contract = options.smoke_test,
                                                  .expected_pipeline_creations = 1,
                                                  .culling_enabled = options.culling,
                                              }};
    });
}
