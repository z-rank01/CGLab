# 应用层分离与多 App 框架

> 状态：as-built，2026-08。
> 这里的“多 App”指仓库可以构建多个独立 executable，并复用同一套 runtime/renderer；不表示同一进程中热切换 App。

## 1. 目标

CGLab 的 sample 用于隔离不同图形实验。新增一个 sample 时，应用层只应描述“这个实验是什么”，不应复制通用引擎骨架。

应用负责：

- 应用名称与元数据；
- 启动 geometry 或 required startup asset；
- Vulkan render program 配置；
- 可选的每帧应用更新；
- 未来的专属 UI hook。

共享框架负责：

- CLI 解析、退出码、validation/smoke contract；
- SDL 窗口和事件泵；
- camera、scene 和 input router 所有权；
- 控制平面与遥测；
- glTF worker 和帧边界结果合并；
- render snapshot 生成、resize 和 renderer 生命周期；
- 确定性的 shutdown 顺序。

## 2. 构建与组合结构

```text
App executable
  ├─ setup(options) -> application_run_request
  │                    ├─ framework::runtime_config
  │                    ├─ framework::sample
  │                    └─ unique_ptr<engine::render_backend>
  │
  └─ apps::run_application(...)
          └─ framework::engine_runtime
                 ├─ interface::window          (默认 SDL3)
                 ├─ scene/camera/input
                 ├─ framework::asset_service  (默认 glTF worker)
                 ├─ control_plane
                 └─ engine::render_backend    (当前 Vulkan)
```

相关 CMake target：

| Target | 职责 | 主要公共依赖 |
|---|---|---|
| `cglab_engine_core` | geometry、snapshot、camera/input/scene 等格式无关数据与系统 | 标准库、GLM |
| `cglab_platform_sdl` | SDL 窗口实现与事件转换 | engine core、SDL3 |
| `cglab_asset_gltf` | glTF/GLB adapter | engine core；DCL 为私有实现依赖 |
| `cglab_asset_runtime` | 异步 asset service 和格式分发 | cglab_asset_gltf |
| `cglab_framework_runtime` | 通用生命周期、帧 phase 和服务所有权 | engine core；platform/asset/control 为实现依赖 |
| `cglab_vulkan_backend` | Vulkan device/swapchain、geometry arena、Render Graph 执行 | engine core；Vulkan/Render Graph 为实现依赖 |
| `cglab_application_runner` | CLI、组装、退出码与 smoke 检查 | framework runtime |

应用 executable 通常只链接 `cglab_application_runner` 和 `cglab_vulkan_backend`。

## 3. 公共组合接口

### `framework::runtime_config`

包含窗口、工作目录、frames-in-flight、validation 和控制平面策略。它不包含 Vulkan instance/device/swapchain 配置。

### `framework::sample`

`sample` 是浅数据结构，不建立 sample 类继承树：

- `name`：应用元数据；
- `startup_geometry`：内存 geometry，适合 TriangleSample 和 smoke；
- `required_startup_asset`：进入帧循环前必须完成的资产；
- `update`：可选的应用更新函数。

### `framework::runtime_services`

`update` 通过受控服务访问 scene、camera、异步资产请求和应用消息。它不会暴露 renderer、Vulkan handle、geometry arena 或 Render Graph 内部对象。

服务引用仅在当前 update 调用期间使用。应用不得缓存其中的引用或 callback 作为跨帧所有权。

### `apps::application_run_request`

这是 executable 与共享 runner 的组合包：runtime 配置、sample、backend、frame limit 以及 validation/smoke 策略在这里汇合。renderer 以 `unique_ptr` 转移给 runtime，所有权唯一。

## 4. 启动流程

```text
main
  -> 共享 CLI 解析
  -> App setup callback
  -> 构造 application_run_request
  -> engine_runtime::configure_sample
  -> engine_runtime::initialize
  -> engine_runtime::tick
  -> engine_runtime::shutdown
  -> validation/smoke counter 检查
  -> 统一退出码
```

setup callback 只负责应用特有决策。解析失败、setup 失败、runtime 异常、renderer 失败或 smoke contract 不满足时，runner 返回失败退出码。

required startup asset 与运行时资产共用同一个 asset service 和 geometry 上传路径；区别只是 runtime 会在正式帧循环前等待 required asset 完成。

## 5. 固定帧 Phase

runtime 每次循环按以下顺序执行：

1. 采集平台事件，处理 close、resize 和拾取输入。
2. 在帧边界消费控制命令。
3. 在帧边界合并 asset worker 完成结果并上传 geometry。
4. 更新 input router 和 camera systems。
5. 调用可选的 `sample.update(runtime_services, delta_time)`。
6. 从可见 scene objects 生成只读 `render_snapshot`。
7. 按 pause/step 状态决定是否调用 renderer。
8. 发布 frame/scene telemetry。

暂停时事件、控制命令和加载结果仍继续处理，只跳过 render；`frame.step` 消费一个待执行步数并允许一帧渲染。

## 6. Shutdown 所有权

shutdown 可以重复调用，顺序固定为：

1. 停止控制平面接收新命令；
2. 停止并 join asset worker；
3. 释放控制平面；
4. 保存 renderer statistics/validation 结果，关闭并释放 renderer；
5. 最后销毁窗口。

renderer 的 GPU idle 和资源销毁属于 backend 的 `shutdown()` 实现，应用不参与该过程。

## 7. 新增一个 App

新增 Vulkan sample 的最小步骤：

1. 在 `src/apps/` 新建一个入口文件。
2. 调用 `apps::run_application(argc, argv, name, setup)`，复用共享 CLI。
3. 在 setup 中创建 `runtime_config`、`sample` 和 Vulkan `render_program`。
4. 通过 `engine::vulkan::create_backend(program)` 创建 backend。
5. 在根 CMake 中新增 executable，只链接 runner 与 Vulkan backend。
6. 添加至少一个有限帧数测试或 smoke 验收，确保不会复制帧循环。

简化后的组合形态：

```cpp
return apps::run_application(argc, argv, "MySample", [](const apps::application_options& options)
{
    framework::runtime_config runtime{/* neutral runtime settings */};
    framework::sample sample{.name = "MySample", .startup_geometry = make_scene()};
    engine::vulkan::render_program program{.pass_name = "MyPass"};
    return apps::application_setup_result{.request = apps::application_run_request{
        .runtime = std::move(runtime),
        .sample = std::move(sample),
        .renderer = engine::vulkan::create_backend(std::move(program)),
        .frame_limit = options.frame_limit,
        .require_validation_clean = options.validation,
    }};
});
```

当前 `TriangleSample` 的完整入口为 57 行，是新增 App 不复制 runtime 的验收样例。

## 8. 边界规则

- 应用中不得出现窗口循环、control server 或 loader thread。
- framework/engine 公共头不得 include Vulkan、SDL 或 DCL/glTF 头。
- runtime 不知道具体 renderer 类型，只依赖 `engine::render_backend`。
- renderer 不保存可变 scene/camera 指针，只消费单次调用有效的 snapshot。
- sample 可以选择 Vulkan render program，但 Vulkan 类型不得进入 `framework::sample`。
- 不为 sample 建立深继承树；优先使用值类型配置和函数组合。

## 9. 当前限制与演进点

- 当前每个 App 是独立进程/独立 executable，尚无 launcher 或进程内 sample 热切换。
- `render_program` 目前只提供 pass 名称和 clear color，复杂多 pass sample 仍需扩展 Vulkan 专属接口。
- 应用专属 UI hook 尚未接入正式 Web UI。
- CLI 目前由所有 sample 共享同一组选项；出现真正不同的参数需求后，应扩展 setup 输入或增加应用自定义参数域，不能重新复制 parser。
