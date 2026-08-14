#pragma once

// apps::lights_table —— F3 端到端插件验证（EngineLayerDoDPlan F3）。
//
// 插件侧组件表：sample 系统持有并每帧发布为帧通道，recipe 侧经
// channels->find_state<apps::lights_table>() 消费——引擎零改动（组件表即插即用）。
// 命名按 Architecture.md 术语表：component table = 某类组件的全量 SoA 表（*_table）。

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace apps
{
    struct lights_table
    {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> colors;
        std::vector<float> intensities;

        [[nodiscard]] std::size_t count() const noexcept { return positions.size(); }
    };
} // namespace apps
