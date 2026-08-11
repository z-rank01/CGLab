#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "apps/triangle_render_recipe.h"

namespace
{
    engine::geometry_asset make_triangle()
    {
        engine::geometry_asset asset{
            .name = "Triangle",
            .bounds_min = {-1.0F, -1.0F, 0.0F},
            .bounds_max = {1.0F, 1.0F, 0.0F},
        };
        engine::geometry_primitive primitive;
        primitive.indices = {0, 1, 2};
        primitive.vertices = {
            {.position = {-1.0F, -1.0F, 0.0F}, .color = {1.0F, 0.0F, 0.0F, 1.0F}},
            {.position = {1.0F, -1.0F, 0.0F}, .color = {0.0F, 1.0F, 0.0F, 1.0F}},
            {.position = {0.0F, 1.0F, 0.0F}, .color = {0.0F, 0.0F, 1.0F, 1.0F}},
        };
        asset.primitives.push_back(std::move(primitive));
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
                                              }};
    });
}
