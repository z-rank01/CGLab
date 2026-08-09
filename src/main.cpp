#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>

#include "application_options.h"
#include "apps/application_runner.h"
#include "smoke_scene.h"
#include "renderer/vulkan/create_vulkan_renderer.h"
#include "utility/config_reader.h"
#include "utility/logger.h"

namespace
{
    bool load_general_config(const application_options& options, general_config& config)
    {
        config.app_name = "VulkanSample";
        if (!options.config_path)
        {
            return true;
        }
        if (!std::filesystem::is_regular_file(*options.config_path))
        {
            Logger::LogError("Config file does not exist: " + options.config_path->string());
            return false;
        }

        config_reader reader(options.config_path->string());
        return reader.try_parse_general_config(config);
    }

    engine::geometry_asset convert_smoke_scene(smoke_scene_data scene)
    {
        engine::geometry_asset result;
        result.name = "SmokeTriangle";
        result.bounds_min = glm::vec3(std::numeric_limits<float>::max());
        result.bounds_max = glm::vec3(std::numeric_limits<float>::lowest());
        for (const gltf::PerDrawCallData& source : scene.draw_calls)
        {
            engine::geometry_primitive primitive;
            primitive.indices = source.indices;
            primitive.material_index = source.material_index;
            primitive.vertices.reserve(source.vertices.size());
            for (const gltf::Vertex& vertex : source.vertices)
            {
                primitive.vertices.push_back(engine::vertex{
                    .position = vertex.position,
                    .color = vertex.color,
                    .normal = vertex.normal,
                    .tangent = vertex.tangent,
                    .uv0 = vertex.uv0,
                    .uv1 = vertex.uv1,
                });
                result.bounds_min = glm::min(result.bounds_min, vertex.position);
                result.bounds_max = glm::max(result.bounds_max, vertex.position);
            }
            result.primitives.push_back(std::move(primitive));
        }
        return result;
    }
}

int main(int argc, char** argv)
{
    return apps::run_application(argc, argv, "VulkanSample", [](const application_options& options)
    {
        general_config general;
        if (!options.smoke_test && !load_general_config(options, general))
        {
            return apps::application_setup_result{.error = "Failed to load application configuration"};
        }

        const std::filesystem::path source_directory = std::filesystem::path(CGLAB_SOURCE_DIR);
        if (options.smoke_test)
        {
            general.app_name = "VulkanSample Smoke Test";
        }
        general.working_directory = source_directory.string();
        std::optional<engine::geometry_asset> scene;
        if (options.smoke_test)
        {
            scene = convert_smoke_scene(make_smoke_scene());
        }
        else
        {
            const std::filesystem::path asset_path = resolve_asset_path(options, general.asset_directory);
            if (asset_path.empty() || !std::filesystem::is_regular_file(asset_path))
            {
                return apps::application_setup_result{
                    .error = "A valid glTF asset is required; pass --asset <path> or provide asset_directory in --config"};
            }
            general.asset_directory = asset_path.string();
        }

        // Control plane 启用规则：交互模式默认启用；--no-ui 关闭；
        // smoke-test 下默认关闭，显式 --ui-port 时启用（供协议级集成测试）。
        framework::control_config ui_config;
        ui_config.enabled = !options.no_ui && (!options.smoke_test || options.ui_port_specified);
        ui_config.port = options.ui_port;
        ui_config.open_browser = options.ui_open_browser;

        framework::runtime_config runtime{
            .window = interface::window_config{.title = general.app_name, .width = 1280, .height = 720},
            .working_directory = general.working_directory,
            .frames_in_flight = 3,
            .validation = options.validation,
            .control_plane = ui_config,
        };

        framework::sample sample{.name = general.app_name};
        if (scene)
        {
            sample.startup_geometry = std::move(*scene);
        }
        else
        {
            sample.required_startup_asset = general.asset_directory;
        }
        const std::optional<std::uint64_t> frame_limit =
            options.smoke_test ? std::optional<std::uint64_t>(options.frame_limit.value_or(3)) : options.frame_limit;
        return apps::application_setup_result{.request = apps::application_run_request{
                                                  .runtime = std::move(runtime),
                                                  .sample = std::move(sample),
                                                  .renderer = engine::vulkan::create_renderer(),
                                                  .frame_limit = frame_limit,
                                                  .require_validation_clean = options.validation,
                                                  .enforce_smoke_contract = options.smoke_test,
                                              }};
    });
}
