// A1 视锥剔除单元测试
// 覆盖：make_frustum 平面正确性（视锥内/外/跨越）、AABB 保守性（相切判可见）、
// cull_instances CSR 压实与朴素扫描一致性、组件存在性语义（挂载/摘除/开关）。

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "_interface/culling.h"
#include "engine/culling_system.h"

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
