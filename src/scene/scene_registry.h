#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/geometry.h"

// scene::scene_registry
// - P2 场景系统核心：场景对象注册表（DoD 风格槽位存储，纯数据，无 Vulkan 依赖，可单测）。
// - 职责：对象生命周期（注册/卸载）、变换、显隐、选中、射线拾取（AABB）。
// - 不负责 GPU 资源：draw_range 由渲染侧（Vulkan backend 的 geometry arena）分配后回填，
//   注册表只保存"画哪些区间"的描述。
// - 启动资产登记为 read_only 条目（draws 为空，走 legacy buffer），可列出/选中，不可卸载。
// - revision：任何场景变更单调递增，供控制平面做场景快照的增量推送判定。

namespace scene
{
    using object_id = std::uint32_t;
    inline constexpr object_id invalid_object_id = std::numeric_limits<object_id>::max();

    struct aabb
    {
        glm::vec3 min{0.0F};
        glm::vec3 max{0.0F};
    };

    // 对象级变换（欧拉角，单位：度；应用顺序 R = Ry(yaw) * Rx(pitch) * Rz(roll)，model = T * R * S）
    struct object_transform
    {
        glm::vec3 position{0.0F};
        glm::vec3 rotation_deg{0.0F};
        glm::vec3 scale{1.0F};
    };

    // 运行时对象在 geometry arena 中的绘制区间（vkCmdDrawIndexed 参数语义）
    struct draw_range
    {
        std::uint32_t first_index   = 0; // 索引区间起点（单位：index）
        std::uint32_t index_count   = 0;
        std::int32_t  vertex_offset = 0; // 顶点区间起点（单位：vertex）
    };

    struct scene_object
    {
        object_id id = invalid_object_id;
        std::string name;
        object_transform transform{};
        bool visible   = true;
        bool read_only = false;   // 启动资产：可列出/选中，不可卸载
        aabb local_bounds{};
        std::vector<draw_range> draws; // 启动资产为空（走 legacy buffer）
        engine::geometry_handle render_geometry = engine::invalid_geometry_handle;
    };

    struct ray
    {
        glm::vec3 origin{0.0F};
        glm::vec3 direction{0.0F, 0.0F, -1.0F};
    };

    struct pick_result
    {
        object_id id       = invalid_object_id;
        float     distance = 0.0F;
    };

    // model = T * R * S；R 为欧拉角（度）按 Ry * Rx * Rz 组合
    [[nodiscard]] inline glm::mat4 model_matrix(const object_transform& transform)
    {
        glm::mat4 model = glm::translate(glm::mat4(1.0F), transform.position);
        model           = glm::rotate(model, glm::radians(transform.rotation_deg.y), glm::vec3(0.0F, 1.0F, 0.0F));
        model           = glm::rotate(model, glm::radians(transform.rotation_deg.x), glm::vec3(1.0F, 0.0F, 0.0F));
        model           = glm::rotate(model, glm::radians(transform.rotation_deg.z), glm::vec3(0.0F, 0.0F, 1.0F));
        model           = glm::scale(model, transform.scale);
        return model;
    }

    // 用 8 角点变换求世界空间 AABB
    [[nodiscard]] inline aabb transform_bounds(const aabb& local, const glm::mat4& model)
    {
        aabb result{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local_corner{
                (corner & 1) != 0 ? local.max.x : local.min.x,
                (corner & 2) != 0 ? local.max.y : local.min.y,
                (corner & 4) != 0 ? local.max.z : local.min.z,
            };
            const glm::vec3 world_corner = glm::vec3(model * glm::vec4(local_corner, 1.0F));
            result.min                   = glm::min(result.min, world_corner);
            result.max                   = glm::max(result.max, world_corner);
        }
        return result;
    }

    class scene_registry
    {
    public:
        // 注册对象，返回单调递增的 id。槽位内部复用，id 不复用。
        object_id register_object(std::string name, const aabb& local_bounds, std::vector<draw_range> draws,
                                  bool read_only = false,
                                  engine::geometry_handle geometry = engine::invalid_geometry_handle)
        {
            scene_object object;
            object.id          = next_id++;
            object.name        = std::move(name);
            object.local_bounds = local_bounds;
            object.draws       = std::move(draws);
            object.read_only   = read_only;
            object.render_geometry = geometry;

            std::size_t slot;
            if (!free_slots.empty())
            {
                slot = free_slots.back();
                free_slots.pop_back();
                slots[slot] = std::move(object);
                alive[slot] = true;
            }
            else
            {
                slot = slots.size();
                slots.push_back(std::move(object));
                alive.push_back(true);
            }
            bump();
            return slots[slot].id;
        }

        // 卸载对象。不存在或 read_only 返回 false。卸载选中对象时清除选择。
        bool unload(object_id id)
        {
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
            {
                if (alive[slot] && slots[slot].id == id)
                {
                    if (slots[slot].read_only)
                    {
                        return false;
                    }
                    alive[slot] = false;
                    free_slots.push_back(slot);
                    if (selected_id == id)
                    {
                        selected_id = invalid_object_id;
                    }
                    bump();
                    return true;
                }
            }
            return false;
        }

        bool set_visibility(object_id id, bool visible)
        {
            scene_object* object = find_mutable(id);
            if (object == nullptr)
            {
                return false;
            }
            object->visible = visible;
            bump();
            return true;
        }

        bool set_transform(object_id id, const object_transform& transform)
        {
            scene_object* object = find_mutable(id);
            if (object == nullptr)
            {
                return false;
            }
            object->transform = transform;
            bump();
            return true;
        }

        // 选中对象；invalid_object_id 表示清除选择。目标不存在返回 false。
        bool set_selected(object_id id)
        {
            if (id == invalid_object_id)
            {
                if (selected_id != invalid_object_id)
                {
                    selected_id = invalid_object_id;
                    bump();
                }
                return true;
            }
            if (find(id) == nullptr)
            {
                return false;
            }
            if (selected_id != id)
            {
                selected_id = id;
                bump();
            }
            return true;
        }

        [[nodiscard]] object_id selected() const noexcept { return selected_id; }

        [[nodiscard]] const scene_object* find(object_id id) const
        {
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
            {
                if (alive[slot] && slots[slot].id == id)
                {
                    return &slots[slot];
                }
            }
            return nullptr;
        }

        // 全部存活对象（按注册顺序）
        [[nodiscard]] std::vector<const scene_object*> objects() const
        {
            std::vector<const scene_object*> result;
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
            {
                if (alive[slot])
                {
                    result.push_back(&slots[slot]);
                }
            }
            return result;
        }

        // 射线拾取：世界空间射线 → 各对象局部空间做 AABB slab 测试，取最近命中。
        // 忽略不可见对象；无命中返回 invalid_object_id。
        [[nodiscard]] pick_result pick(const ray& world_ray) const
        {
            pick_result best;
            float best_distance = std::numeric_limits<float>::max();
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
            {
                if (!alive[slot] || !slots[slot].visible)
                {
                    continue;
                }
                const scene_object& object  = slots[slot];
                const glm::mat4 inverse     = glm::inverse(model_matrix(object.transform));
                const glm::vec3 local_origin = glm::vec3(inverse * glm::vec4(world_ray.origin, 1.0F));
                const glm::vec3 local_dir    = glm::vec3(inverse * glm::vec4(world_ray.direction, 0.0F));

                float hit_t = 0.0F;
                if (!ray_aabb_intersect(local_origin, local_dir, object.local_bounds, hit_t))
                {
                    continue;
                }
                // 命中距离换算回世界空间近似：用世界射线参数化距离
                const glm::vec3 world_hit    = glm::vec3(model_matrix(object.transform) *
                                                         glm::vec4(local_origin + local_dir * hit_t, 1.0F));
                const float world_distance   = glm::dot(world_hit - world_ray.origin, world_ray.direction);
                if (world_distance >= 0.0F && world_distance < best_distance)
                {
                    best_distance = world_distance;
                    best.id       = object.id;
                    best.distance = world_distance;
                }
            }
            return best;
        }

        // 场景变更计数（注册/卸载/变换/显隐/选中），用于遥测增量判定
        [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    private:
        // 局部空间射线 vs AABB 的 slab 测试；命中时 out_t 为进入距离（射线在盒内时为 0）
        static bool ray_aabb_intersect(const glm::vec3& origin, const glm::vec3& direction, const aabb& box,
                                       float& out_t)
        {
            float t_min = 0.0F;
            float t_max = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(direction[axis]) < 1e-8F)
                {
                    if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis])
                    {
                        return false;
                    }
                    continue;
                }
                const float inv_d = 1.0F / direction[axis];
                float t0          = (box.min[axis] - origin[axis]) * inv_d;
                float t1          = (box.max[axis] - origin[axis]) * inv_d;
                if (t0 > t1)
                {
                    std::swap(t0, t1);
                }
                t_min = std::max(t_min, t0);
                t_max = std::min(t_max, t1);
                if (t_min > t_max)
                {
                    return false;
                }
            }
            out_t = t_min;
            return true;
        }

        scene_object* find_mutable(object_id id)
        {
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
            {
                if (alive[slot] && slots[slot].id == id)
                {
                    return &slots[slot];
                }
            }
            return nullptr;
        }

        void bump() { ++revision_; }

        std::vector<scene_object> slots;
        std::vector<bool> alive;
        std::vector<std::size_t> free_slots;
        object_id next_id     = 0;
        object_id selected_id = invalid_object_id;
        std::uint64_t revision_ = 0;
    };
} // namespace scene
