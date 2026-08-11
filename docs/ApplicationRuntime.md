# Application Runtime

> as-built，2026-08。

`TriangleSample` 和 `GltfSponzaSample` 都是轻量组合点。应用选择启动资产和 render program，然后把窗口、asset service、Engine runtime 与 `cglab_render_graph_vulkan` driver 交给共享 application runner。

## 构建目标

| target | 职责 |
|---|---|
| `cglab_engine_core` | API 无关资产、场景、camera、driver 和 frame packet 类型 |
| `cglab_asset_gltf` | glTF Core 2.0 CPU Asset Database adapter |
| `cglab_asset_runtime` | worker 调度和帧边界完成结果 |
| `cglab_platform_sdl` | window/input 实现 |
| `cglab_sdl_vulkan_surface` | Vulkan runtime 所需的窄 surface provider |
| `cglab_engine_runtime` | state tables、固定 phase systems、控制平面 |
| `cglab_render_graph_vulkan` | Engine 行表到 RG Vulkan runtime 的薄适配 |
| `cglab_application_runner` | CLI、组装、退出码和 smoke contract |

应用 executable 链接 application runner 与 RG Vulkan adapter，不直接链接或调用 Vulkan resource API。

## Render driver

`render_driver` 是 move-only opaque state + `render_driver_api` function table。核心调用是：initialize、apply_resource_changes、render、request_resize、shutdown 和 statistics。fake driver 可在没有 Vulkan 的测试中验证 phase 顺序、pause/step、load/unload、resize 和幂等关闭。

资源操作先形成 `resource_change_batch` 行表；渲染操作消费仅在调用期间有效的 `render_frame_packet` SoA views。相同事件、命令与 asset completion 序列会生成确定性的 packet。

## Frame phases

runtime 逐帧执行固定十阶段表：

1. `poll_events`
2. `consume_control_commands`
3. `merge_asset_results`
4. `update_scene_transforms`
5. `update_cameras`
6. `run_sample_systems`
7. `extract_render_packet`
8. `apply_resource_changes`
9. `submit_render_packet`
10. `publish_telemetry`

该顺序是显式契约，不使用运行时依赖图。异步 worker 不持有 Engine 指针，只产生私有值类型结果；主线程在第三阶段合并。resize、pause/step、load/unload 等操作都是 request rows。

## 增加新 Sample

新 Sample 只需要提供启动配置、可选资产和 render program，并复用 application runner。若需要新渲染能力，先扩展 API 无关 packet/resource rows，再在 `src/render_graph_vulkan` 做内容 lowering，在 RG Vulkan runtime 实现物理资源或命令行为；不要在 App 或 Engine 中创建 Vk 对象。
