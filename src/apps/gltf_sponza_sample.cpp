#include <filesystem>

#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "renderer/vulkan/create_vulkan_backend.h"

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
        "GltfSponzaSample",
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
                .window            = {.title = "GltfSponzaSample", .width = 1280, .height = 720},
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
            engine::vulkan::render_program program{
                .pass_name   = "GltfSponzaPass",
                .clear_color = {0.03F, 0.04F, 0.08F, 1.0F},
            };
            const auto frames = options.smoke_test ? std::optional<std::uint64_t>(options.frame_limit.value_or(3)) : options.frame_limit;
            return apps::application_setup_result{.request = apps::application_run_request{
                                                      .runtime     = std::move(config),
                                                      .sample      = {.name = "GltfSponzaSample", .required_startup_asset = asset_path.string()},
                                                      .renderer    = engine::vulkan::create_backend(std::move(program)),
                                                      .frame_limit = frames,
                                                      .require_validation_clean = options.validation,
                                                      .enforce_smoke_contract   = options.smoke_test,
                                                  }};
        },
        {.accepts_asset = true});
}
