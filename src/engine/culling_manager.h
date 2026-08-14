#pragma once

// engine::culling_manager —— 相机可选视锥剔除组件（A1，Component + Manager）。
//
// 角色分工（本项目不是严格 ECS，"system" 一词只属于系统函数本身）：
// - 组件列（纯数据，manager 的列即唯一真相）：
//     present  = 能力存在（无行 = 无剔除能力，直通；存在性代替布尔）
//     enabled  = 运行时开关
// - 系统执行上下文（transient，帧内有效、帧间复用）：
//     frustums = cull_instances 的派生缓存；flags/visible_scratch = CSR 两遍法工作区
//     last_visible/last_culled = 系统输出观测（telemetry 消费）
// - 组件生命周期：attach/detach/set_culling_enabled（registry 职责，非每帧逻辑）
// - 系统函数（每帧、帧边界副作用出口）：cull_instances
//   纯函数（frustum/AABB 数学）在 _interface/culling.h。
//
// 约束：
// - cull_instances 是 CSR 两遍法（掩码列 → 压实散布进单写者 scratch），稳态零分配；
//   作用于 render_frame_packet 输入侧，recipe/RG/glTF 零改动。
// - 数据行按相机索引对齐。

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "_interface/culling.h"
#include "engine/render_backend.h"
#include "scene/scene_registry.h"

namespace engine
{
    struct culling_manager
    {
        std::vector<std::uint8_t> present;   // 存在列（0/1）：能力是否挂载
        std::vector<std::uint8_t> enabled;   // 开关列：能力挂载后的运行时开关
        std::vector<interface::culling::frustum> frustums; // 每帧缓存（相机 dirty 时重算）
        // 单写者 scratch：帧边界复用，稳态零分配（只增长不收缩）
        std::vector<engine::instance_row> visible_scratch;
        std::vector<std::uint8_t> flags;     // 可见性掩码列（第一遍产物）
        // 上帧统计（telemetry 消费）
        std::uint64_t last_visible = 0;
        std::uint64_t last_culled = 0;
    };

    // 挂载剔除能力（不存在时扩容列到 camera_index + 1）。返回是否已挂载。
    [[nodiscard]] inline bool attach_culling(culling_manager& manager, std::size_t camera_index, bool enabled = true)
    {
        if (camera_index >= manager.present.size())
        {
            manager.present.resize(camera_index + 1, 0);
            manager.enabled.resize(camera_index + 1, 0);
            manager.frustums.resize(camera_index + 1);
        }
        manager.present[camera_index] = 1;
        manager.enabled[camera_index] = enabled ? 1 : 0;
        return true;
    }

    // 摘除能力（行标记清除；列保留，供后续再挂载）。
    inline void detach_culling(culling_manager& manager, std::size_t camera_index)
    {
        if (camera_index >= manager.present.size())
        {
            return;
        }
        manager.present[camera_index] = 0;
        manager.enabled[camera_index] = 0;
    }

    // 运行时开关（能力存在时生效；不存在时不改变状态）。
    inline void set_culling_enabled(culling_manager& manager, std::size_t camera_index, bool enabled)
    {
        if (camera_index >= manager.present.size() || manager.present[camera_index] == 0)
        {
            return;
        }
        manager.enabled[camera_index] = enabled ? 1 : 0;
    }

    // 每帧剔除 pass（CSR 两遍法）：
    //   1) 逐 instance 计算世界 AABB（mesh 局部 bounds × 世界变换，8 角点），
    //      写入 flags 掩码列；
    //   2) 压实散布进 visible_scratch。
    // 输入：instances/transforms 为 packet 行，mesh_bounds_* 按 geometry_handle 索引
    // （越界句柄/transform 保守判可见）。返回本帧可见行（指向 scratch，下一次调用前有效）。
    [[nodiscard]] inline std::span<const engine::instance_row> cull_instances(
        culling_manager& manager,
        std::size_t camera_index,
        const glm::mat4& view_projection,
        std::span<const engine::instance_row> instances,
        std::span<const glm::mat4> transforms,
        std::span<const glm::vec3> mesh_bounds_min,
        std::span<const glm::vec3> mesh_bounds_max,
        std::uint64_t* out_culled = nullptr)
    {
        const auto frustum = interface::culling::make_frustum(view_projection);
        if (camera_index < manager.frustums.size())
        {
            manager.frustums[camera_index] = frustum;
        }

        // 第一遍：掩码列
        manager.flags.assign(instances.size(), 0);
        std::uint64_t visible = 0;
        for (std::size_t i = 0; i < instances.size(); ++i)
        {
            const engine::instance_row& instance = instances[i];
            const std::uint32_t mesh = instance.mesh;
            if (mesh >= mesh_bounds_min.size() || instance.transform >= transforms.size())
            {
                manager.flags[i] = 1; // 越界保守判可见
                ++visible;
                continue;
            }
            const scene::aabb world = scene::transform_bounds(
                scene::aabb{.min=mesh_bounds_min[mesh], .max=mesh_bounds_max[mesh]}, transforms[instance.transform]);
            if (interface::culling::test_aabb(frustum, world.min, world.max))
            {
                manager.flags[i] = 1;
                ++visible;
            }
        }

        // 第二遍：压实散布（单次遍历，零分配）
        manager.visible_scratch.clear();
        manager.visible_scratch.reserve(instances.size()); // 只增长；稳态零分配
        for (std::size_t i = 0; i < instances.size(); ++i)
        {
            if (manager.flags[i] != 0)
            {
                manager.visible_scratch.push_back(instances[i]);
            }
        }

        const std::uint64_t culled = instances.size() - visible;
        manager.last_visible = visible;
        manager.last_culled = culled;
        if (out_culled != nullptr)
        {
            *out_culled = culled;
        }
        return manager.visible_scratch;
    }
} // namespace engine
