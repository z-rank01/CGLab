# CG Lab 渲染引擎架构

> v4（2026-08）：补充多应用组合框架、Render Graph 与渲染后端边界。
> 本文描述当前代码，而不是目标架构草图；尚未实现的部分会明确标为后续工作。

当前仓库采用 Vulkan-only 的渐进式架构：应用只做组合，runtime 负责通用生命周期，renderer 负责 Vulkan 和 Render Graph，engine core 保存格式无关的数据与系统。暂不建设完整 RHI。

## 文档导航

- [应用层分离与多 App 框架](ApplicationFramework.md)：应用组合点、共享 runner、sample/runtime 服务和新增 App 流程。
- [Render Graph 与渲染后端边界](RenderGraphAndRHI.md)：当前帧图、资源所有权、`render_backend` 的定位以及未来 RHI 条件。
- [GltfSponzaSample Render Graph 迁移](GltfSponzaSampleRenderGraphMigration.md)：迁移细节、帧事务与 smoke contract。
- [交互界面设计](InterfaceRedesign.md)：控制平面、Web UI 和单窗口路线。
- [基础设施设计](InfrastructureDesign.md)：Job System/benchmark 的触发式路线。

## 当前依赖方向

```text
GltfSponzaSample / TriangleSample
        │
        ├── cglab_application_runner
        │          └── cglab_framework_runtime
        │                    ├── cglab_engine_core
        │                    ├── cglab_platform_sdl
        │                    ├── cglab_asset_runtime
        │                    │       └── cglab_asset_gltf
        │                    └── control_plane
        │
        └── cglab_vulkan_backend
                   ├── cglab_engine_core
                   ├── render_graph
                   └── Vulkan / SDL surface integration
```

依赖只能从应用指向框架和 renderer，再指向 engine core。framework/engine 公共接口不得出现 Vulkan、SDL 或 glTF 类型。

## 分层

> v2 改为四层模型：在原三层草图基础上拆出**引擎框架层**。
> 原因：UI/窗口/IO/帧循环是所有 sample 共享的框架代码，不属于任何一个 app；
> 若留在应用层，每写一个 sample 就要复制一遍（当前 app_sample 的实际问题）。

**上层 - 应用层（samples）**：每个实验是独立 executable，例如 GltfSponzaSample、TriangleSample，以及未来的 Hi-Z/Ray Tracing sample。应用只提供元数据、启动场景、Vulkan render program 和未来的专属 UI hook，不拥有窗口、帧循环、camera、scene、控制平面或 loader worker。详见 [ApplicationFramework.md](ApplicationFramework.md)。

**引擎框架层（engine runtime，新增）**：被所有 app 复用的运行时骨架——
- 帧循环与渲染窗口（当前是 SDL 方案）
- 键鼠 I/O（input_router 路由层）
- Web UI 控制平面（UI ↔ 引擎的协议缝；正式 UI 尚未实现）
- 异步资源加载调度
- 每帧只读 `render_snapshot` 生成与 renderer 调用

**中层 - 业务组件层**: 关乎渲染相关内容，用于实现高效的渲染和方便的管线编辑：
- render graph component（已独立为子模块）
- digital content loader component（已独立为子模块）
- camera component
- scene component
- Vulkan 渲染设备执行模块（位于 `src/renderer/vulkan/`，拥有 device/swapchain/pipeline/geometry arena）
- 未来可能的其他引擎组件

**下层 - 基础设施层（infrastructure）**：
- Job System 并发系统
- async-await 异步框架
- benchmark 设施
- 未来可能的其他基础设施

这些基础设施为业务层服务，提供高性能能力，并与上层完全解耦，以便未来复用于其他仓库。

分成四层后，每个层之间相互隔离解耦，仅通过必要的 interface 接口去对接，且依赖从上往下，不能有反向的依赖。

## 现状盘点（2026-08）

**已解决**（不要重复劳动，也不要推翻）：
- 框架复用：`cglab_framework_runtime` 已拥有窗口、显式帧 phase、camera/scene、控制平面与 asset service；应用不再复制帧循环
- 渲染边界：Vulkan-free `engine::render_backend` / `render_snapshot` / `geometry_handle` 已落地；renderer 不再持有可变 camera/scene 指针
- GPU geometry：启动资产和运行时资产统一走 geometry arena，scene 只保存 opaque handle；上传/延迟回收状态留在 Vulkan renderer
- 构建边界：已有 `cglab_engine_core`、`cglab_platform_sdl`、`cglab_asset_gltf`、`cglab_asset_runtime`、`cglab_framework_runtime`、`cglab_vulkan_backend`、`cglab_application_runner`
- Sample 复用：GltfSponzaSample 已迁移到共享 runtime；TriangleSample 以独立 `TrianglePass` 验证第二个 exe（应用文件 100 行以内）
- 应用入口：`cglab_application_runner` 统一 CLI、runtime/backend 组装、退出码、validation 和 smoke counter 检查
- 资源服务：framework 只依赖 `asset_service` 接口；`cglab_asset_runtime` 是默认实现，测试可注入 fake service
- 输入路由：物理输入 → 逻辑动作的绑定表已数据化（`input_router.h`）；wasd 不再定死，未来换成 JSON 序列化驱动是顺手的事
- 相机 SoA：`camera_container` 已是 SoA + 纯函数（fly/orbit/bookmark 多模式），为未来多相机做的准备已到位
- 场景：`scene_registry` 为 flat component-based 槽位存储；多对象、运行时异步加载 glTF 已实现
- UI 解耦缝：控制平面（JSON-RPC over WebSocket）让 UI 进程与引擎只通过协议对话——目前全仓库最干净的解耦点，应固化为正式 interface
- 业务组件单测：camera / scene / json_rpc 均不依赖 Vulkan、有 CTest 覆盖

**部分解决（欠债清单）**：
- glTF 节点变换在加载时烘焙进顶点，**层级信息丢失**（静态展示够用；未来动画/局部变换需还这笔债）
- scene 目前为 flat 结构，无 parent-child 层级
- Vulkan backend 已成为独立 target 和 backend façade；context/swapchain/frame/geometry/graph 已拆成 backend 内部源文件，不影响 framework API

**未解决**：
- Vulkan backend 内部仍可继续把共享状态收敛为更细的组件类；当前已完成职责级源文件拆分
- Vulkan `render_program` 目前只能配置 pass name/clear color，尚不能注册复杂多 pass 图
- 基础设施完全空白（job system、benchmark）
- 当前 asset 分发支持 ASCII `.gltf` 和 Binary `.glb`；fbx/obj 仅保留 adapter 扩展点

## Render Graph 与 RHI 定位

当前采用以下明确约束：

- 仓库保持 Vulkan-only；不因可能的 DX12 需求提前抽象 Vulkan 资源细节。
- `engine::render_backend` 是 runtime 的生命周期/提交边界，不是完整 RHI。它不提供 buffer、image、pipeline 或 command list 抽象。
- `engine::vulkan::render_program` 是 Vulkan sample 的扩展点，可以逐步暴露 Render Graph/Vulkan 能力，但不得反向泄漏到 framework 或 engine core。
- Render Graph 位于 renderer 内部，负责编译 pass/resource DAG、barrier、transient resource 和帧事务；它不拥有应用生命周期。

完整结构、所有权表和未来 RHI 触发条件见 [RenderGraphAndRHI.md](RenderGraphAndRHI.md)。

## 已知问题与计划

**1. 引擎框架层抽取（已完成）**
- runtime、asset service、backend snapshot/geometry handle 边界和第二个 TriangleSample 已落地。
- CPU/Render Graph 自动测试覆盖公共头隔离、fake runtime、glTF adapter 与异步 asset service。
- 剩余工作是把 Vulkan backend 的共享状态进一步收敛为组件类、扩展真实多 pass `render_program`，以及在装有 validation layer 的机器上补 GPU validation smoke。

**2. 业务组件层完善**
- camera 组件：
    1. 所有权随框架层抽取下沉业务层；
    2. 未来补 spine 滑轨、追踪等功能（当前嵌套太深的问题随抽取解决；保持惰性+纯函数风格）；
    3. 补视锥体、远近平面管理等引擎常见 camera 功能。
- scene 组件：
    1. **层级 ≠ 嵌套继承**：层级关系用 flat SoA + `parent_index` 列实现，配一个 transform 传播 system（manager tick 时自顶向下算 world matrix）——component based 与多层级 transform 不矛盾；
    2. camera 也登记为场景物体；支持空物体与 sphere/cube 等标准几何体；
    3. 多 asset 加载已支持，后续接层级后取消"烘焙节点变换"的债。
- DCL：补 `.glb`（当前 LoadASCIIFromFile 不支持二进制）；fbx/obj 后置。

**3. 基础设施层（触发式启动，详见 InfrastructureDesign.md）**
- **不在只有一个并发负载时抽象**（反 yak-shaving 条款）。当前 loader worker 保持专用线程。
- 触发条件：出现第二个真实并发负载（候选：shader 编译、多 glTF 并行解析、纹理流送）。
- 阶梯：最小 job system（线程池 + future）→ benchmark 验收 → 再评估是否做 async-await。

**4. 引擎 UI 与单窗口（框架层稳定后，详见 InterfaceRedesign.md v2）**
- 正式 UI 面板（资产、场景、rg debug 图示等），需要保证延展能力和健壮性，类似 Unity 可拼接拉伸的界面。
- 渲染窗口已可用且简单；单窗口集成（B1/B2）在框架层稳定后进行。
- 键鼠 IO 已接口化（input_router）；后续做 mapping 的 JSON 序列化方案。

## 横切准则（全层适用）

数据与线程：
- 每个 component 数组**单一写者**（主线程）；worker/job 只读快照或写私有结果区，**帧边界合并**（P2 loader 即此模式，上升为准则）。
- 跨线程只交换值类型，worker 永不持有引擎指针。
- 业务组件不依赖 Vulkan，必须可 CTest 单测。

错误处理：统一 result/expected 风格（`_callable` 链为雏形），不吞异常、不用裸 bool 表达失败语义（新代码）。

当前目录职责（旧 camera/scene 物理目录后续再渐进整理，不做一次性搬家）：
- `src/apps/`：应用入口与共享 application runner；
- `src/framework/`：runtime、sample/runtime services、asset service interface；
- `src/engine/`：geometry 和 render backend 等中立核心接口；camera/scene 目前仍在旧物理目录，由 `cglab_engine_core` 聚合；
- `src/renderer/vulkan/`：Vulkan renderer 和 Vulkan render program；
- `src/asset/`：glTF adapter 与默认 asset worker；
- `src/infra/`：尚未创建，满足触发条件后再落地。

## 设计与代码准则

大部分代码希望遵循：
- Data Oriented 风格最最基本
    1. Database 的数据存储，合理设计 structure。即使是大量的 bool，也应该类似 structure { a, b} 这种关系型数据库的样式，将状态变成其中的 row；
    2. SoA，坚持内存命中友好，同样也需要合理设计 structure，不要太大的 SoA，也不能太细碎；
    3. Component Based，抗拒 Object-Oriented Hierarchy Classes 这类嵌套和继承的设计，考虑通过 manager 去 tick 所有 component，类似下面这种，而不是 unity 那种每个脚本自己 update
    4. 函数式风格，不需要严格遵守，但需要和 DoD 风格结合起来，使代码更健壮更易 debug。比如惰性求值（优先抽象层面完成逻辑，类似 C++ view 设计，当前的 render graph 也是类似的，先 compile 组织，最后才创建 physical resource）、模式匹配（switch-case + SoA > 一大堆 if + enum/bool）、副作用放到最后（和惰性求值类似，处理完无副作用的部分之后，才是真正会影响数据结构的副作用函数）、函数优先（这一点可以不用，不做过于极端的函数式）
```C
  PhysicsManager.Update(allPhysicsComponents)   // 先全部更新物理
  ControlManager.Update(allControlComponents)   // 再全部更新控制
  AnimationManager.Update(allAnimationComponents) // 再全部更新动画
  RenderManager.Update(allRenderComponents)     // 最后全部渲染
```
system 的执行顺序初期用**显式 phase 列表**定死（如上），不引入依赖图调度器，直到出现真实排序冲突再说。
