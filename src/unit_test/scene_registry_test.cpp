// scene_registry 单元测试
// 覆盖：注册/卸载（含 read_only 保护）、槽位复用、变换矩阵、世界 AABB、
//       显隐、选中语义、射线拾取（最近命中/未命中/忽略不可见）、revision 计数。

#include <cstdlib>
#include <iostream>
#include <string_view>

#include <glm/glm.hpp>

#include "scene/scene_registry.h"

namespace
{
    int failures = 0;

    void check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    bool near(float a, float b, float eps = 1e-4F)
    {
        return std::abs(a - b) < eps;
    }

    scene::aabb unit_box()
    {
        return scene::aabb{glm::vec3(-1.0F), glm::vec3(1.0F)};
    }

    void test_register_and_find()
    {
        scene::scene_registry registry;
        const scene::object_id first  = registry.register_object("alpha", unit_box(), {});
        const scene::object_id second = registry.register_object("beta", unit_box(), {});
        check(first == 0, "first id is 0");
        check(second == 1, "second id is 1");
        check(registry.objects().size() == 2, "two objects alive");
        check(registry.find(first) != nullptr && registry.find(first)->name == "alpha", "find by id");
        check(registry.find(999) == nullptr, "find unknown id returns null");
        check(registry.revision() == 2, "revision bumps on register");
    }

    void test_unload_and_slot_reuse()
    {
        scene::scene_registry registry;
        const scene::object_id first  = registry.register_object("alpha", unit_box(), {});
        const scene::object_id second = registry.register_object("beta", unit_box(), {});
        check(registry.unload(first), "unload alive object");
        check(!registry.unload(first), "double unload fails");
        check(registry.find(first) == nullptr, "unloaded object gone");
        check(registry.objects().size() == 1, "one object alive after unload");

        const scene::object_id third = registry.register_object("gamma", unit_box(), {});
        check(third == 2, "id not reused after unload");
        check(registry.objects().size() == 2, "slot reused, count stable");
        check(registry.find(second) != nullptr, "surviving object intact");
    }

    void test_read_only_protection()
    {
        scene::scene_registry registry;
        const scene::object_id startup = registry.register_object("startup", unit_box(), {}, true);
        check(!registry.unload(startup), "read_only object cannot be unloaded");
        check(registry.find(startup) != nullptr, "read_only object still alive");
        check(registry.set_visibility(startup, false), "read_only object visibility mutable");
        check(registry.set_transform(startup, scene::object_transform{}), "read_only object transform mutable");
    }

    void test_model_matrix()
    {
        scene::object_transform transform;
        transform.position = glm::vec3(1.0F, 2.0F, 3.0F);
        transform.scale    = glm::vec3(2.0F);
        const glm::mat4 model = scene::model_matrix(transform);
        const glm::vec4 origin = model * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
        check(near(origin.x, 1.0F) && near(origin.y, 2.0F) && near(origin.z, 3.0F), "model translation");
        const glm::vec4 unit_x = model * glm::vec4(1.0F, 0.0F, 0.0F, 1.0F);
        check(near(unit_x.x, 3.0F), "model scale applied before translation");
    }

    void test_world_bounds()
    {
        scene::object_transform transform;
        transform.position = glm::vec3(10.0F, 0.0F, 0.0F);
        const scene::aabb world = scene::transform_bounds(unit_box(), scene::model_matrix(transform));
        check(near(world.min.x, 9.0F) && near(world.max.x, 11.0F), "world bounds translated");
        check(near(world.min.y, -1.0F) && near(world.max.y, 1.0F), "world bounds untouched axis");
    }

    void test_visibility_and_selection()
    {
        scene::scene_registry registry;
        const scene::object_id id = registry.register_object("alpha", unit_box(), {});
        check(registry.set_visibility(id, false), "set visibility");
        check(registry.find(id)->visible == false, "visibility stored");
        check(!registry.set_visibility(42, true), "visibility unknown id fails");

        check(registry.set_selected(id), "select existing");
        check(registry.selected() == id, "selection stored");
        check(!registry.set_selected(42), "select unknown fails");
        check(registry.set_selected(scene::invalid_object_id), "clear selection");
        check(registry.selected() == scene::invalid_object_id, "selection cleared");

        // 卸载选中对象时自动清除选择
        check(registry.set_selected(id), "reselect before unload");
        check(registry.unload(id), "unload selected object");
        check(registry.selected() == scene::invalid_object_id, "selection cleared on unload");
    }

    void test_pick()
    {
        scene::scene_registry registry;
        // 两个盒子：near 在 z=-5，far 在 z=-10，均可见
        scene::scene_object setup; // 仅为可读性，实际用 register 的返回 id
        const scene::object_id near_id = registry.register_object("near", unit_box(), {});
        scene::object_transform near_transform;
        near_transform.position = glm::vec3(0.0F, 0.0F, -5.0F);
        check(registry.set_transform(near_id, near_transform), "place near box");

        const scene::object_id far_id = registry.register_object("far", unit_box(), {});
        scene::object_transform far_transform;
        far_transform.position = glm::vec3(0.0F, 0.0F, -10.0F);
        check(registry.set_transform(far_id, far_transform), "place far box");

        scene::ray forward{glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, -1.0F)};
        const scene::pick_result hit = registry.pick(forward);
        check(hit.id == near_id, "pick nearest object");
        check(near(hit.distance, 4.0F, 1e-3F), "pick distance to box face");

        // 反方向无命中
        scene::ray backward{glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 1.0F)};
        check(registry.pick(backward).id == scene::invalid_object_id, "pick miss behind");

        // 偏离轴无命中
        scene::ray aside{glm::vec3(5.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, -1.0F)};
        check(registry.pick(aside).id == scene::invalid_object_id, "pick miss off axis");

        // 隐藏 near 后命中 far
        check(registry.set_visibility(near_id, false), "hide near box");
        check(registry.pick(forward).id == far_id, "pick skips invisible");

        // 缩放影响拾取：缩小 far 到不可命中
        scene::object_transform tiny = far_transform;
        tiny.scale = glm::vec3(0.1F);
        check(registry.set_transform(far_id, tiny), "shrink far box");
        scene::ray grazing{glm::vec3(0.5F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, -1.0F)};
        check(registry.pick(grazing).id == scene::invalid_object_id, "scaled box no longer hit");
    }

    void test_revision_tracking()
    {
        scene::scene_registry registry;
        check(registry.revision() == 0, "initial revision zero");
        const scene::object_id id = registry.register_object("alpha", unit_box(), {});
        const std::uint64_t after_register = registry.revision();
        check(after_register == 1, "register bumps revision");
        check(registry.set_visibility(id, false), "vis change");
        check(registry.revision() == after_register + 1, "visibility bumps revision");
        check(registry.set_transform(id, scene::object_transform{}), "transform change");
        check(registry.revision() == after_register + 2, "transform bumps revision");
    }
} // namespace

int main()
{
    test_register_and_find();
    test_unload_and_slot_reuse();
    test_read_only_protection();
    test_model_matrix();
    test_world_bounds();
    test_visibility_and_selection();
    test_pick();
    test_revision_tracking();

    if (failures != 0)
    {
        std::cerr << failures << " scene_registry test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "scene_registry tests passed\n";
    return EXIT_SUCCESS;
}
