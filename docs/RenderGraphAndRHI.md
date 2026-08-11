# Render Graph 与渲染后端结构

> 状态：as-built，2026-08。
> 当前仓库是 Vulkan-only。文中的 backend boundary 不等于完整 RHI。

## 1. 三个容易混淆的边界

CGLab 当前有三个不同层次的渲染接口：

| 边界 | 使用者 | 解决的问题 | 是否暴露 Vulkan |
|---|---|---|---|
| `engine::render_backend` | engine runtime | 初始化、geometry 生命周期、单帧提交、resize、统计、关闭 | 否 |
| `engine::vulkan::render_program` | Vulkan sample | 描述 sample 的 Vulkan 渲染程序扩展点 | 可以；当前字段尚不需要 Vulkan handle |
| `render_graph` | Vulkan renderer 内部 | pass/resource DAG、状态转换、barrier、transient allocation、帧事务 | core 格式无关，Vulkan lowering 使用 Vulkan |

因此：

- `render_backend` 是 runtime façade，不是 buffer/image/pipeline 级 RHI；
- Render Graph 是帧内工作和资源依赖编译器，不负责窗口、scene、camera 或应用生命周期；
- Vulkan renderer 是两者之间的具体执行模块，也是当前唯一 GPU backend。

## 2. 总体数据流

```text
scene + camera (runtime-owned)
          │
          ├─ build immutable render_snapshot
          │      ├─ frame serial
          │      ├─ view / projection
          │      └─ object { model, geometry_handle }
          │
          ▼
engine::render_backend
          │
          ▼
cglab_vulkan_backend
  ├─ resolve geometry_handle -> private arena ranges
  ├─ acquire swapchain image
  ├─ build/reuse Render Graph plan
  ├─ bind imported Vulkan resources
  ├─ record and submit commands
  └─ present + update statistics
```

反向依赖被禁止：renderer 不读取 scene registry，也不保存 camera 指针；runtime 不访问 VkBuffer、VkImage、descriptor 或 Render Graph handle。

## 3. `engine::render_backend` 契约

公共接口位于 `src/engine/render_backend.h`：

- `initialize(window, backend_config)`：使用中立窗口接口和 backend 配置初始化；
- `upload_geometry(geometry_asset)`：返回 opaque `geometry_handle`；
- `retire_geometry(handle)`：使 handle 进入安全延迟回收；
- `request_resize()`：通知下一帧重建 swapchain 相关状态；
- `render(snapshot)`：消费只在本次调用期间有效的只读快照；
- `shutdown()`：等待/释放 GPU 资源，允许 runtime 幂等关闭；
- `statistics()` / `validation_error_count()`：供 runner、telemetry 和 smoke contract 使用。

初始化与上传使用 `engine::result<T>` 返回值和错误文本。单帧结果使用模式明确的 `frame_status`：

| 状态 | 含义 | Runtime 行为 |
|---|---|---|
| `rendered` | submit/present 成功 | 成功帧计数加一 |
| `skipped` | 暂时不可渲染，例如 swapchain 需要恢复 | 保持事件/控制处理，之后重试 |
| `failed` | 本次运行无法继续 | 结束循环并返回失败退出码 |

这个接口刻意没有暴露通用 GPU resource API。它足以替换 fake backend 做 runtime 单测，也能阻止 Vulkan 向 engine runtime 泄漏。

## 4. Snapshot 与 Geometry 边界

### `render_snapshot`

snapshot 是 runtime 在每帧末端生成的只读视图，包含帧序号、camera view/projection 和可见对象的 model matrix/geometry handle。

`objects` 使用 `std::span`，其生命周期仅覆盖一次 `render(snapshot)` 调用。backend 不得缓存 span 或其中对象的地址。

### `geometry_asset`

loader 和 renderer 之间使用格式无关 DTO：vertex、indices、primitive、material index 和 bounds。DCL adapter 在 worker 中将 `gltf::PerDrawCallData` 转为该 DTO，并暂时把 glTF node transform 烘焙到 vertex position。

### `geometry_handle`

scene 只保存数值 handle。以下信息全部是 Vulkan renderer 私有状态：

- VkBuffer/VMA allocation；
- vertex/index arena offset 与 draw range；
- staging copy regions；
- frames-in-flight 延迟释放 gate；
- arena free spans。

启动资产与运行时资产都通过 `upload_geometry()` 进入同一 geometry arena。renderer 内仍保留少量 legacy helper/dummy resource 作为迁移期实现细节，但它们不构成第二条公共 geometry API。

## 5. Vulkan Renderer 所有权

`cglab_vulkan_backend` 当前拥有：

| 模块 | 所有资源/职责 |
|---|---|
| Instance/surface | Vulkan instance、validation messenger、由 SDL native handle 创建的 surface |
| Device/queue | physical/logical device、graphics queue、command pool |
| Swapchain | image acquire、extent/format、resize、present 状态追踪 |
| Frame resources | command buffer、fence、semaphore、uniform buffer、descriptor |
| Pipeline | shader、descriptor layout、dynamic rendering pipeline |
| Geometry store | device-local vertex/index arena、staging batch、opaque handle map、延迟回收 |
| Render Graph bridge | graph 构建、imported resource binding、Vulkan lowering、frame commit/abort |

这些实现位于 `src/renderer/vulkan/` 下的 context、swapchain、frame、geometry 和 graph 源文件中。目标边界已经稳定，后续可以继续收敛组件类和 RAII 所有权，而不改变 engine runtime API。

## 6. 当前 Render Graph

### Pass 结构

当存在待上传 geometry 时，帧图包含：

```text
RuntimeUploadPass (copy)
        │ writes arena vertex/index buffers
        ▼
DrawPass 或 TrianglePass (raster)
        │ writes imported swapchain image + transient depth
        ▼
final PRESENT state
```

无待上传批次时，renderer 复用不含 upload pass 的图变体。`render_program.pass_name` 决定 raster pass 名称；GltfSponzaSample 使用 `DrawPass`，TriangleSample 使用 `TrianglePass`。

### Resource 类型

- swapchain image：imported image，每帧 acquire 后重新绑定；
- geometry arena/uniform/staging：imported buffer，由 renderer 拥有物理资源；
- depth：Render Graph transient image，由编译计划管理生命周期；
- backbuffer final state：显式设置为 present，下一次 acquire 根据状态 tracker 决定 preserve/discard。

### Graph cache key

cache key 当前包含影响物理计划的状态，例如 swapchain extent/format、upload 图变体和 acquired image 初始状态。仅重新绑定 imported resource 不要求重新编译；结构或兼容性变化才触发 compile。

## 7. 单帧事务

Vulkan renderer 的帧事务为：

1. 收集满足完成帧 gate 的延迟释放资源。
2. acquire swapchain image；处理 out-of-date/resize。
3. 计算 graph cache key，调用 `begin_frame(submitted, completed, key)`。
4. 必要时构建并编译 pass/resource DAG。
5. 把当前 VkBuffer/VkImage 绑定到 imported graph handles。
6. execute/record Vulkan commands。
7. submit 成功后 `commit_frame()`；任何录制、提交或中止路径调用 `abort_frame()`。
8. present 成功后更新 backbuffer state 和统计计数。

`commit_frame()` 是逻辑状态进入下一帧的边界。失败帧不得把未提交的 resource state 污染到后续帧。

## 8. `render_program` 的当前能力

`engine::vulkan::render_program` 是有意保持狭窄的 Vulkan 专属扩展点。目前只有：

- `pass_name`；
- raster clear color。

它足以验证两个 App 可以选择不同 pass 身份，但尚不能声明任意多 pass DAG、创建 sample-owned pipeline 或注入录制 callback。Hi-Z、shadow、ray tracing 等真实 sample 出现时，应在这个 Vulkan 专属边界增加经过验证的能力，而不是扩张 `engine::render_backend`。

推荐演进顺序：

1. 先增加 Vulkan/Render Graph pass registration context；
2. renderer 继续统一添加 upload、backbuffer final state 和 submission 基础 pass；
3. sample 只注册业务 pass；
4. 用第二个真实复杂 sample 验证接口，再决定是否抽取更通用的 program model。

## 9. 为什么当前不是正式 RHI

完整 RHI 通常需要抽象 resource description、memory、pipeline、descriptor、command list/queue、同步、shader 和 feature query。当前接口没有这些能力，这是刻意选择：

- 仓库只有一个真实 backend；
- 过早统一 Vulkan/DX12 会把最低公分母和 API 特例带入 engine core；
- runtime 实际只需要“上传 geometry + 渲染 snapshot”的窄边界；
- Vulkan sample 的实验性 pass 更适合直接使用 Vulkan/Render Graph 能力。

只有满足以下条件之一才重新评估正式 RHI：

- 第二个真实图形 API backend 已进入同一仓库并需要共享大部分 sample；
- 至少两个复杂 sample 证明当前 Vulkan program 接口产生了重复的资源/管线管理代码；
- 自动测试能定义跨 backend 的行为契约，而不是只比较类型名称。

届时应先评估现有 `render_backend` 是否继续作为上层 façade，再在 renderer 下方引入资源级 RHI；不应直接让 RHI 类型进入 scene、camera 或 engine runtime。

## 10. 测试与验收

- `cglab.public_headers`：公共 engine 头不得传递包含 Vulkan、SDL、tinygltf。
- `cglab.engine_runtime`：fake window/backend/asset service 验证生命周期、snapshot 和帧边界合并。
- `cglab.asset_gltf` / `cglab.asset_service`：DTO 转换、异步结果与失败路径。
- Render Graph CTest：DAG、barrier、resource lifetime、frame transaction、Vulkan lowering 和 sample graph。
- GPU smoke：UploadPass 恰好一次，业务 draw pass/present 次数等于成功帧数，validation error 为零。

GPU validation smoke 需要目标机器安装 `VK_LAYER_KHRONOS_validation`；layer 缺失时必须明确失败，不能静默关闭 validation。

## 11. 当前技术债

- Vulkan backend 已按 context/swapchain/frame/geometry/graph orchestration 拆成内部源文件；后续可继续收敛共享状态和 RAII 所有权。
- `render_program` 还不是可注册多 pass 的真正扩展接口。
- glTF hierarchy 尚未保留，node transform 仍在 worker 中烘焙。
- geometry arena 容量和回收策略已有实现，但仍需更完整的容量不足、上传失败和长时间 churn GPU 测试。
- 当前只有 Vulkan backend；不要把文档中的“backend”理解为已经验证的跨 API RHI。
