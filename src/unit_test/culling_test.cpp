// 视锥剔除单元测试
// 覆盖：make_frustum 平面正确性（视锥内/外/跨越）、AABB 保守性（相切判可见）、
// cull_instances CSR 压实与朴素扫描一致性、组件存在性语义（挂载/摘除/开关）、
// transform_aabb 世界包围盒（R3/M5）、光视图正交视锥边界（R3/M5）。

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "_interface/culling.h"
#include "engine/culling_manager.h"

namespace
{
    int failures = 0;

    void check(bool condition, const char* name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    // 相机位于原点，看向 -Z，FOV 45°，near 0.1 / far 100。
    glm::mat4 default_view_projection()
    {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, -1.0F), glm::vec3(0.0F, 1.0F, 0.0F));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0F), 16.0F / 9.0F, 0.1F, 100.0F);
        return proj * view;
    }

    void test_frustum_planes()
    {
        const auto frustum = interface::culling::make_frustum(default_view_projection());

        // 视线正前方 5 单位：所有平面 dot <= 0（在视锥内）
        const glm::vec3 inside{0.0F, 0.0F, -5.0F};
        for (const auto& plane : frustum.planes)
        {
            check(glm::dot(glm::vec3(plane), inside) + plane.w <= 0.0F, "frustum contains point ahead");
        }

        // 相机后方：近平面之外（z >= -w 外向平面为正）
        const glm::vec3 behind{0.0F, 0.0F, 5.0F};
        bool outside = false;
        for (const auto& plane : frustum.planes)
        {
            if (glm::dot(glm::vec3(plane), behind) + plane.w > 0.0F)
            {
                outside = true;
            }
        }
        check(outside, "frustum excludes point behind camera");

        // 远平面之外：far 100，z = -200 应在外
        check(!interface::culling::test_aabb(frustum, {-1.0F, -1.0F, -200.0F}, {1.0F, 1.0F, -199.0F}),
              "frustum excludes beyond far plane");

        // 距离 5 处水平半宽 = 5 * (16/9) / tan(22.5°)^-1 ≈ 3.68，垂直半宽 = 5 * tan(22.5°) ≈ 2.07；
        // x=3.6 可见，x=3.8 剔除
        check(interface::culling::test_aabb(frustum, {3.6F, -0.5F, -5.0F}, {3.61F, 0.5F, -4.99F}),
              "aabb near frustum edge visible");
        check(!interface::culling::test_aabb(frustum, {3.8F, -0.5F, -5.0F}, {3.81F, 0.5F, -4.99F}),
              "aabb beyond frustum edge culled");
    }

    void test_aabb_conservative()
    {
        const auto frustum = interface::culling::make_frustum(default_view_projection());

        // 跨越近平面的大盒（相机在盒内）：可见
        check(interface::culling::test_aabb(frustum, {-10.0F, -10.0F, -1.0F}, {10.0F, 10.0F, 10.0F}),
              "aabb straddling near plane visible");

        // 与上平面相切（dot == 0 边界）：必须判可见（保守，不丢面）
        const glm::vec3 min{-0.5F, 2.07F, -5.0F};
        const glm::vec3 max{0.5F, 2.071F, -4.99F};
        check(interface::culling::test_aabb(frustum, min, max), "aabb tangent to frustum plane visible");

        // 完全在左侧外：剔除
        check(!interface::culling::test_aabb(frustum, {50.0F, -0.5F, -5.0F}, {51.0F, 0.5F, -4.99F}),
              "aabb fully outside culled");
    }

    void test_cull_instances_compaction()
    {
        engine::culling_manager manager;
        check(engine::attach_culling(manager, 0, true), "attach culling on camera 0");
        check(manager.present[0] == 1 && manager.enabled[0] == 1, "attach sets present/enabled");

        // 3 个实例：mesh 0 在视锥内，mesh 1 在视锥外（左侧），mesh 2 在视锥内
        std::vector<glm::vec3> bounds_min{
            {-0.5F, -0.5F, -5.0F},
            {50.0F, -0.5F, -5.0F},
            {-0.5F, -0.5F, -5.0F},
        };
        std::vector<glm::vec3> bounds_max{
            {0.5F, 0.5F, -4.9F},
            {51.0F, 0.5F, -4.9F},
            {0.5F, 0.5F, -4.9F},
        };
        std::vector<glm::mat4> transforms(3, glm::mat4(1.0F));
        std::vector<engine::instance_row> instances{
            {.mesh = 0, .transform = 0},
            {.mesh = 1, .transform = 1},
            {.mesh = 2, .transform = 2},
        };

        std::uint64_t culled = 0;
        const auto visible = engine::cull_instances(manager, 0, default_view_projection(),
                                                    instances, transforms, bounds_min, bounds_max, &culled);

        // 与朴素扫描一致：期望 {0, 2}
        std::vector<std::uint32_t> naive;
        const auto frustum = interface::culling::make_frustum(default_view_projection());
        for (std::size_t i = 0; i < instances.size(); ++i)
        {
            const auto world = scene::transform_bounds(scene::aabb{bounds_min[i], bounds_max[i]}, transforms[i]);
            if (interface::culling::test_aabb(frustum, world.min, world.max))
            {
                naive.push_back(static_cast<std::uint32_t>(i));
            }
        }
        check(culled == instances.size() - naive.size(), "culled count matches naive scan");
        check(visible.size() == naive.size(), "visible count matches naive scan");
        bool matches = visible.size() == naive.size();
        for (std::size_t i = 0; i < naive.size(); ++i)
        {
            matches = matches && visible[i].mesh == naive[i];
        }
        check(matches, "compacted rows match naive scan order");
        check(manager.last_visible == naive.size() && manager.last_culled == culled, "manager stats updated");
    }

    void test_cull_instances_conservative_out_of_range()
    {
        engine::culling_manager manager;
        (void)engine::attach_culling(manager, 0, true);

        // 句柄越界 / transform 越界：保守判可见
        std::vector<engine::instance_row> instances{{.mesh = 99, .transform = 99}};
        std::vector<glm::mat4> transforms(1, glm::mat4(1.0F));
        std::vector<glm::vec3> bounds_min{{-0.5F, -0.5F, -5.0F}};
        std::vector<glm::vec3> bounds_max{{0.5F, 0.5F, -4.9F}};
        std::uint64_t culled = 0;
        const auto visible = engine::cull_instances(manager, 0, default_view_projection(),
                                                    instances, transforms, bounds_min, bounds_max, &culled);
        check(visible.size() == 1 && culled == 0, "out-of-range instance kept visible");
    }

    void test_transform_aabb()
    {
        // 单位阵：包围盒不变
        const auto identity = interface::culling::transform_aabb(
            glm::mat4(1.0F), {-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F});
        check(identity.min == glm::vec3(-1.0F, -2.0F, -3.0F) && identity.max == glm::vec3(1.0F, 2.0F, 3.0F),
              "identity transform keeps bounds");

        // 平移：中心随矩阵移动
        const auto translated = interface::culling::transform_aabb(
            glm::translate(glm::mat4(1.0F), glm::vec3(5.0F, 0.0F, -2.0F)), {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F});
        check(translated.min == glm::vec3(4.0F, -1.0F, -3.0F) && translated.max == glm::vec3(6.0F, 1.0F, -1.0F),
              "translation moves bounds");

        // 缩放：半径按 |M| 缩放
        const auto scaled = interface::culling::transform_aabb(
            glm::scale(glm::mat4(1.0F), glm::vec3(2.0F, 0.5F, 1.0F)), {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F});
        check(scaled.min == glm::vec3(-2.0F, -0.5F, -1.0F) && scaled.max == glm::vec3(2.0F, 0.5F, 1.0F),
              "scale scales bounds");

        // 绕 Z 旋转 90°：x/y 半径互换（闭式解 = 8 角点变换）
        const auto rotated = interface::culling::transform_aabb(
            glm::rotate(glm::mat4(1.0F), glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F)),
            {-1.0F, -2.0F, -1.0F}, {1.0F, 2.0F, 1.0F});
        check(glm::length(rotated.min - glm::vec3(-2.0F, -1.0F, -1.0F)) < 1e-4F &&
                  glm::length(rotated.max - glm::vec3(2.0F, 1.0F, 1.0F)) < 1e-4F,
              "rotation 90 swaps extents");
    }

    // R3/M5 光视图：ShadowSample 同款斜射平行光 view_proj（正交 ±4，Vulkan Y 翻转，
    // GLM_FORCE_DEPTH_ZERO_TO_ONE）。
    glm::mat4 light_view_projection()
    {
        const glm::vec3 sun_direction = glm::normalize(glm::vec3(-0.5F, -0.8F, -0.35F));
        const glm::vec3 center{0.0F, 0.3F, 0.0F};
        const glm::vec3 eye = center - sun_direction * 15.0F;
        const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0F, 1.0F, 0.0F));
        glm::mat4 proj = glm::ortho(-4.0F, 4.0F, -4.0F, 4.0F, 0.1F, 30.0F);
        proj[1][1] *= -1.0F; // Vulkan framebuffer Y 翻转（recipe 同款；视锥体不变）
        return proj * view;
    }

    void test_light_frustum_ortho()
    {
        const glm::mat4 view_projection = light_view_projection();
        const auto frustum = interface::culling::make_frustum(view_projection);
        // 视矩阵本身（不含投影）用于把光空间点映射回世界
        const glm::vec3 sun_direction = glm::normalize(glm::vec3(-0.5F, -0.8F, -0.35F));
        const glm::mat4 view_inverse = glm::inverse(
            glm::lookAt(glm::vec3(0.0F, 0.3F, 0.0F) - sun_direction * 15.0F, glm::vec3(0.0F, 0.3F, 0.0F),
                        glm::vec3(0.0F, 1.0F, 0.0F)));

        // 光盒中心物体：可见
        check(interface::culling::test_aabb(frustum, {-0.5F, -1.0F, -0.5F}, {0.5F, 1.0F, 0.5F}),
              "light box center visible");

        // 光侧向 ±4 之外（光空间 x = 6）：剔除
        const glm::vec3 side_world = glm::vec3(view_inverse * glm::vec4(6.0F, 0.0F, -10.0F, 1.0F));
        check(!interface::culling::test_aabb(frustum, side_world - 0.1F, side_world + 0.1F),
              "light box side culled");

        // 光 far（30）之外（光空间 z = -31）：剔除
        const glm::vec3 far_world = glm::vec3(view_inverse * glm::vec4(0.0F, 0.0F, -31.0F, 1.0F));
        check(!interface::culling::test_aabb(frustum, far_world - 0.1F, far_world + 0.1F),
              "light box beyond far culled");

        // 光近平面后（光空间 z = +5）：保守保留（近侧松约束，宁多画不丢影）
        const glm::vec3 near_behind_world = glm::vec3(view_inverse * glm::vec4(0.0F, 0.0F, 5.0F, 1.0F));
        check(interface::culling::test_aabb(frustum, near_behind_world - 0.1F, near_behind_world + 0.1F),
              "light box near-behind kept (conservative)");
    }

    void test_component_lifecycle()
    {
        engine::culling_manager manager;
        // 无组件：直通语义由调用方保证（present == 0）
        check(manager.present.empty(), "no component rows by default");

        (void)engine::attach_culling(manager, 0, false);
        check(manager.present[0] == 1 && manager.enabled[0] == 0, "attach with disabled switch");

        engine::set_culling_enabled(manager, 0, true);
        check(manager.enabled[0] == 1, "set_culling_enabled turns on");

        engine::set_culling_enabled(manager, 5, true);
        check(manager.present.size() == 1, "set_culling_enabled on absent camera is no-op");

        engine::detach_culling(manager, 0);
        check(manager.present[0] == 0 && manager.enabled[0] == 0, "detach clears component row");

        (void)engine::attach_culling(manager, 2, true);
        check(manager.present.size() == 3 && manager.present[2] == 1, "attach grows columns to camera index");
    }
} // namespace

int main()
{
    test_frustum_planes();
    test_aabb_conservative();
    test_transform_aabb();
    test_light_frustum_ortho();
    test_cull_instances_compaction();
    test_cull_instances_conservative_out_of_range();
    test_component_lifecycle();

    if (failures != 0)
    {
        std::cerr << failures << " culling test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All culling tests passed\n";
    return EXIT_SUCCESS;
}
