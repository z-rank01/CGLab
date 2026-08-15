#include <filesystem>
#include <memory>

#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "apps/gltf_render_recipe.h"
#include "apps/lights_table.h"
#include "apps/sun_light.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
            const auto frames = options.smoke_test ? std::optional<std::uint64_t>(options.frame_limit.value_or(3)) : options.frame_limit;
            // 插件侧发布光源表（每帧两盏灯：暖色主光 + 冷色补光）与平行光：
            // sample 经 services.channels 发布组件表通道，gltf recipe 在 build_frame 经 find_state 消费。
            // 发布用 owned 变体（F4）：每帧新造对象并把所有权移交通道，帧尾统一释放，
            // 不需要 shared_ptr 手工保活——不存在"update 返回后悬空指针"的路径。
            engine::sample sample{
                .name = "GltfSponzaSample",
                .required_startup_asset = asset_path.string(),
                .update = [](engine::runtime_services& services, float)
                {
                    auto lights = std::make_unique<apps::lights_table>();
                    lights->positions = {{-1.0F, 3.0F, 2.0F}, {2.0F, 2.0F, -1.0F}};
                    lights->colors = {{1.0F, 0.9F, 0.8F}, {0.4F, 0.6F, 1.0F}};
                    lights->intensities = {3.0F, 2.0F};
                    services.channels.publish_state_owned<apps::lights_table>(std::move(lights));

                    // 平行光（sun_light）：斜向入射 + 正交光空间矩阵（Y 翻转与主相机
                    // 一致，shadow pass 与主 pass 采样共用同一矩阵）。正交范围按
                    // Sponza 量级场景固定；后续 CSM/自适应可按场景包围盒收紧。
                    auto sun = std::make_unique<apps::sun_light>();
                    sun->direction = glm::normalize(glm::vec3(-0.4F, -1.0F, -0.3F));
                    sun->intensity = 3.0F;
                    sun->color = {1.0F, 0.95F, 0.9F};
                    const float extent = 40.0F;      // 光正交视锥半宽
                    const float depth_range = 120.0F; // 光眼位距场景中心
                    const glm::vec3 center{0.0F, 0.0F, 0.0F};
                    const glm::vec3 eye = center - sun->direction * depth_range;
                    const glm::mat4 light_view = glm::lookAt(eye, center, glm::vec3(0.0F, 1.0F, 0.0F));
                    glm::mat4 light_proj = glm::ortho(-extent, extent, -extent, extent, 0.1F, depth_range * 2.0F);
                    // 在投影阶段翻转 Vulkan framebuffer Y；合成后只修改
                    // view_proj[1][1] 会剪切旋转过的光空间，使阴影偏离入射方向。
                    light_proj[1][1] *= -1.0F;
                    sun->view_proj = light_proj * light_view;
                    sun->ortho_box = {-extent, extent, -extent, extent};
                    services.channels.publish_state_owned<apps::sun_light>(std::move(sun));
                },
            };
            return apps::application_setup_result{.request = apps::application_run_request{
                                                      .runtime     = std::move(config),
                                                      .sample      = std::move(sample),
                                                      .renderer    = apps::create_gltf_render_driver(),
                                                      .frame_limit = frames,
                                                      .require_validation_clean = options.validation,
                                                      .enforce_smoke_contract   = options.smoke_test,
                                                      .culling_enabled          = options.culling,
                                                  }};
        },
        {.accepts_asset = true});
}
