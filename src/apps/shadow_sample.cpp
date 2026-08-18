#include <filesystem>
#include <limits>
#include <memory>

#include "apps/application_options.h"
#include "apps/application_runner.h"
#include "apps/debug_view.h"
#include "apps/gltf_render_recipe.h"
#include "apps/lights_table.h"
#include "apps/sun_light.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/render_backend.h"
#include "scene/scene_registry.h"

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

    // 接影地面：6×6 单位 XZ 平面（y=0，法线 +Y，白色默认材质），与展示物件
    // 一起接收/投射阴影。绕序与 glTF 一致（从 +Y 看逆时针，cull back 正常显示）。
    engine::asset_database make_ground_plane()
    {
        constexpr float half = 3.0F;
        engine::asset_database asset{.name = "GroundPlane"};
        const glm::vec3 normal{0.0F, 1.0F, 0.0F};
        const glm::vec4 color{1.0F, 1.0F, 1.0F, 1.0F};
        const glm::vec4 tangent{1.0F, 0.0F, 0.0F, 1.0F};
        asset.vertex_blob = {
            {{-half, 0.0F, -half}, color, normal, tangent, {0.0F, 0.0F}, {0.0F, 0.0F}},
            {{-half, 0.0F, half}, color, normal, tangent, {0.0F, 1.0F}, {0.0F, 1.0F}},
            {{half, 0.0F, half}, color, normal, tangent, {1.0F, 1.0F}, {1.0F, 1.0F}},
            {{half, 0.0F, -half}, color, normal, tangent, {1.0F, 0.0F}, {1.0F, 0.0F}},
        };
        asset.index_blob = {0, 1, 2, 0, 2, 3};
        asset.meshes.push_back({.name = "GroundPlane", .first_primitive = 0, .primitive_count = 1,
                                .bounds_min = {-half, 0.0F, -half}, .bounds_max = {half, 0.0F, half}});
        asset.primitives.push_back({.mesh = 0, .material = 0,
                                    .vertex_offset = 0, .vertex_count = 4,
                                    .index_offset = 0, .index_count = 6});
        asset.nodes.push_back({.name = "GroundPlane", .parent = engine::invalid_asset_index, .mesh = 0});
        asset.materials.emplace_back();
        return asset;
    }
}

// ShadowSample：小物件阴影展示（GltfSponzaSample 的桌面级变体）。
// - 内置 6×6 接影地面 + --asset 装载展示物件（helmet 等小 glTF/GLB）；
// - 斜射太阳光（右上前方入射，物件受光面朝默认相机，影子向左后方铺在平面上）；
// - 光正交视锥收紧到 ±4 单位（Sponza 版是 ±40，小物件下 2048² 阴影图
//   分辨率浪费 100 倍），一盏冷色补光 lift 背光面。
int main(int argc, char** argv)
{
    return apps::run_application(
        argc,
        argv,
        "ShadowSample",
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
                .window            = {.title = "ShadowSample", .width = 1280, .height = 720},
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
            // 调试视图（R4/M3）：--debug-view 开启时经帧通道发布请求（owned 发布），
            // recipe 追加 debug pass——引擎零改动；契约计数随 debug pass 增加。
            // 注意：debug 管线在 recipe initialize 无条件创建（pass 才是条件性的），
            // 故 pipeline 计数恒为 7（M6 增 resolve）；indirect groups / raster pass 计数随 debug pass
            // 增加。MSVC 的 designated initializer 内不能直接放三元表达式（解析 bug），
            // 期望计数先算成局部常量。
            const auto debug_mode = options.debug_view;
            const bool debug_on = debug_mode != apps::debug_view_mode::off;
            const std::uint64_t expected_pipeline_creations = 7;
            const std::uint64_t expected_indirect_groups = debug_on ? 4 : 3;
            const std::uint64_t expected_draw_passes = debug_on ? 4 : 3;
            // 平面贴地状态：首帧把平面移到物件包围盒最低点之下（对任意 glb 自适应）。
            // H1 口径（2026-08-16 复盘）：帧间静态数据 = 持久持有 + 裸指针发布
            // （零分配；发布点注释"持久对象，非局部变量"）；每帧新建数据 = owned
            // 发布（机制防呆）。灯光/太阳数据帧间不变 → 持久持有；debug 请求语义
            // 即每帧一个 → 保留 owned（F4 范例）。plane_fit 是 sample 持久状态
            // （不发布），作普通成员而非 shared_ptr。
            struct sample_persistent_state
            {
                std::unique_ptr<apps::lights_table> lights = std::make_unique<apps::lights_table>();
                std::unique_ptr<apps::sun_light> sun = std::make_unique<apps::sun_light>();
                engine::debug_view_status debug_status{}; // 每帧回报实际生效的 debug 视图（M8/B2）
                bool plane_fit = false;
            };
            sample_persistent_state persistent;
            // 冷色补光：照亮物件背光面（无阴影，避免与主光阴影打架）
            persistent.lights->positions = {{2.5F, 2.5F, 3.5F}};
            persistent.lights->colors = {{0.45F, 0.55F, 0.7F}};
            persistent.lights->intensities = {1.4F};
            // 斜射平行光：右上前方入射（受光面朝默认相机），影子向左后铺开。
            // 正交视锥 ±4 覆盖地面与影子落点；Y 翻转与主相机一致。
            persistent.sun->direction = glm::normalize(glm::vec3(-0.5F, -0.8F, -0.35F));
            persistent.sun->intensity = 3.5F;
            persistent.sun->color = {1.0F, 0.96F, 0.9F};
            const float extent = 4.0F;       // 光正交视锥半宽（覆盖 6×6 地面 + 影子）
            const float distance = 15.0F;     // 光眼位距场景中心
            const glm::vec3 center{0.0F, 0.3F, 0.0F};
            const glm::vec3 eye = center - persistent.sun->direction * distance;
            const glm::mat4 light_view = glm::lookAt(eye, center, glm::vec3(0.0F, 1.0F, 0.0F));
            glm::mat4 light_proj = glm::ortho(-extent, extent, -extent, extent, 0.1F, distance * 2.0F);
            // Vulkan 的 framebuffer Y 翻转属于投影变换。若在 view_proj
            // 合成后只改 [1][1]，旋转过的 light_view 会被剪切，导致沿
            // sun.direction 的点在阴影图 XY 上发生漂移，影子方向便与光照不一致。
            light_proj[1][1] *= -1.0F;
            persistent.sun->view_proj = light_proj * light_view;
            persistent.sun->ortho_box = {-extent, extent, -extent, extent};
            persistent.sun->ortho_near = 0.1F;
            persistent.sun->ortho_far = distance * 2.0F;
            // shared_ptr 捕获：std::function 要求 lambda 可拷贝（H1 持久状态
            // 本身 unique_ptr 持有，捕获层只拷指针）。
            auto persistent_holder = std::make_shared<sample_persistent_state>(std::move(persistent));
            engine::sample sample{
                .name = "ShadowSample",
                .startup_geometry = make_ground_plane(),
                .required_startup_asset = asset_path.string(),
                .update = [debug_mode, persistent = std::move(persistent_holder)](engine::runtime_services& services, float)
                {
                    // debug 视图：web 覆盖（debug.set_view → override 通道，M8/B2）优先于 CLI
                    const auto* override_state = services.channels.find_state<engine::debug_view_override>();
                    const std::uint32_t view_mode = (override_state != nullptr && override_state->active)
                                                        ? override_state->mode
                                                        : static_cast<std::uint32_t>(debug_mode);
                    if (view_mode != static_cast<std::uint32_t>(apps::debug_view_mode::off))
                    {
                        // owned 发布（每帧一个请求，机制防呆）
                        auto debug = std::make_unique<apps::debug_view_request>();
                        debug->mode = view_mode;
                        services.channels.publish_state_owned<apps::debug_view_request>(std::move(debug));
                    }
                    // 状态回报（持久成员裸指针，H1）：随 telemetry.frame 下发，UI 初值对齐 CLI
                    persistent->debug_status.mode = view_mode;
                    services.channels.publish_state<engine::debug_view_status>(&persistent->debug_status);
                    // 持久对象，非局部变量：裸指针发布，帧间零分配（H1）
                    services.channels.publish_state<apps::lights_table>(persistent->lights.get());
                    services.channels.publish_state<apps::sun_light>(persistent->sun.get());

                    // 平面贴地（一次性）：物件原点常在包围盒中心（如 helmet），平面
                    // 需移到物件最低点之下，否则下半截插进地里。扫描除平面外的
                    // 存活对象世界 AABB 取最低点。
                    if (persistent->plane_fit)
                    {
                        return;
                    }
                    float asset_min_y = std::numeric_limits<float>::max();
                    scene::object_id plane_id = scene::invalid_object_id;
                    for (const scene::scene_object* object : services.scene.objects())
                    {
                        if (object->name == "GroundPlane")
                        {
                            plane_id = object->id;
                            continue;
                        }
                        const auto world_bounds = scene::transform_bounds(object->local_bounds,
                                                                         scene::model_matrix(*object));
                        asset_min_y = std::min(asset_min_y, world_bounds.min.y);
                    }
                    if (plane_id != scene::invalid_object_id && asset_min_y < std::numeric_limits<float>::max())
                    {
                        scene::object_transform transform{};
                        transform.position = {0.0F, asset_min_y - 0.02F, 0.0F};
                        (void)services.scene.set_transform(plane_id, transform);
                        persistent->plane_fit = true;
                    }
                },
            };
            return apps::application_setup_result{.request = apps::application_run_request{
                                                      .runtime     = std::move(config),
                                                      .sample      = std::move(sample),
                                                      .renderer    = apps::create_gltf_render_driver(),
                                                      .frame_limit = frames,
                                                      .require_validation_clean = options.validation,
                                                      .enforce_smoke_contract   = options.smoke_test,
                                                      .expected_pipeline_creations = expected_pipeline_creations,
                                                      .expected_indirect_groups_per_frame = expected_indirect_groups,
                                                      .expected_draw_passes_per_frame = expected_draw_passes,
                                                  }};
        },
        {.accepts_asset = true});
}
