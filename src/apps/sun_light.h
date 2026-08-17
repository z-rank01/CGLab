#pragma once

// apps::sun_light —— 平行光组件表（单例 state 通道）。
//
// 插件侧组件表：sample 系统每帧发布为帧通道，recipe 侧经
// channels->find_state<apps::sun_light>() 消费——引擎零改动。
// 与 lights_table 同款模式；direction 为光入射方向（指向场景），
// view_proj 为正交光空间矩阵（主 pass 采样阴影图用同一矩阵）。
// ortho_box 记录光正交视锥范围（调试 / 未来 CSM 分阶用）。

#include <glm/glm.hpp>

namespace apps
{
    struct sun_light
    {
        glm::vec3 direction{-0.4F, -1.0F, -0.3F};
        float intensity = 3.0F;
        glm::vec3 color{1.0F, 0.95F, 0.9F};
        float pad = 0.0F;
        glm::mat4 view_proj{1.0F};
        glm::vec4 ortho_box{0.0F}; // x=left, y=right, z=bottom, w=top
        // 光正交视锥近/远平面（调试视图线性化深度用，R4/M3）
        float ortho_near = 0.1F;
        float ortho_far = 240.0F;
    };
} // namespace apps
