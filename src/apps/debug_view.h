#pragma once

// apps::debug_view_request —— 调试视图请求（单例 state 通道，R4/M3）。
//
// 插件侧配置通道：sample 按 --debug-view 选项每帧发布（owned 发布，F4），
// gltf recipe 在 build_frame 经 channels->find_state<apps::debug_view_request>()
// 消费——引擎零改动。缺失通道 = 不加 debug pass（默认路径零开销）。
// mode 取值与 apps::debug_view_mode 一致（off/shadow/depth）。

#include <cstdint>

namespace apps
{
    struct debug_view_request
    {
        std::uint32_t mode = 0; // 0=off 1=shadow(原始深度) 2=depth(线性化热力图) 3=hdr 4=resolved（M6）
    };
} // namespace apps
