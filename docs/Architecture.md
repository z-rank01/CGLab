## CG Lab 渲染引擎架构

> v2（2026-08）：根据 InterfaceRedesign P0–P2 落地后的现状修订。
> v1 的三层思想与 DoD/函数式准则保持不变；本版修正现状盘点、补齐层级图、重排优先级。

当前的渲染引擎还没有实现真正的解耦合和高性能，我的理想思维框架图应该是分层分模块，且高性能，极低耦合的。

### 层

> v2 改为四层模型：在原三层草图基础上拆出**引擎框架层**。
> 原因：UI/窗口/IO/帧循环是所有 sample 共享的框架代码，不属于任何一个 app；
> 若留在应用层，每写一个 sample 就要复制一遍（当前 app_sample 的实际问题）。

**上层 - 应用层（samples）**：主要功能就是我自己可以编写和制作不同的多个 .exe 应用。我会实验性的创建一些 APP 来编写一些特性，比如尝试 Hi-Z 阴影，比如尝试 Ray Tracing 等，都会单独编写一个 hierarchical_z_sample, ray_tracing_vk_sample, ray_tracing_dx_sample 等。
**应用层的 app 只应提供：场景内容、render pass 组织、专属 UI 面板。**

**引擎框架层（engine runtime，新增）**：被所有 app 复用的运行时骨架——
- 帧循环与渲染窗口（当前是 SDL 方案）
- 键鼠 I/O（input_router 路由层）
- 引擎界面 UI（当前是 web 系框架）与控制平面（UI ↔ 引擎的协议缝）
- 异步资源加载调度

**中层 - 业务组件层**: 关乎渲染相关内容，用于实现高效的渲染和方便的管线编辑：
- render graph component（已独立为子模块）
- digital content loader component（已独立为子模块）
- camera component
- scene component
- 渲染设备执行模块（swapchain/帧提交/pipeline，当前埋在 vulkan_sample 里，待拆）
- 未来可能的其他引擎组件

**下层 - 基础设施层（infrastructure）**：
- Job System 并发系统
- async-await 异步框架
- benchmark 设施
- 未来可能的其他基础设施

这两个基础设施为业务层服务，提供高性能能力。且和上层完全解耦，意味着可以用于其他仓库和主题的内容。

分成四层后，每个层之间相互隔离解耦，仅通过必要的 interface 接口去对接，且依赖从上往下，不能有反向的依赖。

### 现状盘点（2026-08，InterfaceRedesign P0–P2 之后）

**已解决**（不要重复劳动，也不要推翻）：
- 输入路由：物理输入 → 逻辑动作的绑定表已数据化（`input_router.h`）；wasd 不再定死，未来换成 JSON 序列化驱动是顺手的事
- 相机 SoA：`camera_container` 已是 SoA + 纯函数（fly/orbit/bookmark 多模式），为未来多相机做的准备已到位
- 场景：`scene_registry` 为 flat component-based 槽位存储；多对象、运行时异步加载 glTF 已实现
- UI 解耦缝：控制平面（JSON-RPC over WebSocket）让 UI 进程与引擎只通过协议对话——目前全仓库最干净的解耦点，应固化为正式 interface
- 业务组件单测：camera / scene / json_rpc 均不依赖 Vulkan、有 CTest 覆盖

**部分解决（欠债清单）**：
- camera / scene / loader 的**所有权**仍在 app_sample（组件实体已在，生命周期仍由应用层管理）
- glTF 节点变换在加载时烘焙进顶点，**层级信息丢失**（静态展示够用；未来动画/局部变换需还这笔债）
- scene 目前为 flat 结构，无 parent-child 层级

**未解决**：
- app_sample / vulkan_sample god object：每写一个 sample 都要复制帧循环、窗口、控制平面、异步加载
- 渲染设备执行模块未独立，RHI 决策未做（见下）
- 基础设施完全空白（job system、benchmark）
- DCL 仅支持 ASCII `.gltf`（`.glb`/fbx/obj 未支持）

### 待决策：RHI（渲染设备抽象）

app 清单含 ray_tracing_dx_sample，涉及跨 API：
- **选项 A（默认）**：本仓库 sample 全部 Vulkan-only，DX RT 放独立仓库。
- **选项 B**：本仓库多 API——需先划 RHI 抽象层，工作量大，必须在层级图中显式占位后再动工。

在未做决定前，一切重构不得把 Vulkan 类型泄漏进业务组件层接口（scene/camera 目前已满足）。

### 已知问题与计划（重排后）

对应到前面的层级和模块，当前除了 render graph 和 digital content loader 通过子模块单独分出来以外，其余部分要么没实现、要么耦合在一起。所以我个人的接下来的计划是（v2 重排，理由见每条附注）：

**1. 引擎框架层抽取（最高优先，先于 job system）**
- app_sample → 可复用 engine_runtime 库：窗口、帧循环、相机容器、控制平面、scene_registry、异步加载调度全部下沉框架层；app 只剩场景内容 + pass + 专属面板。
- vulkan_sample 拆分为"渲染设备执行模块"（业务组件层）+ app 侧 pass 组织；VulkanSample.exe 变成第一个"瘦 app"，同时作为回归基准。
- 验收：用 engine_runtime 写一个 <100 行的第二个 sample exe 可运行；现有 CTest / smoke / 端到端验证全绿。
- 附注：camera/scene"从 app 层摘出来"是这一步的自然结果，不单独立项。

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

### 横切准则（全层适用）

数据与线程：
- 每个 component 数组**单一写者**（主线程）；worker/job 只读快照或写私有结果区，**帧边界合并**（P2 loader 即此模式，上升为准则）。
- 跨线程只交换值类型，worker 永不持有引擎指针。
- 业务组件不依赖 Vulkan，必须可 CTest 单测。

错误处理：统一 result/expected 风格（`_callable` 链为雏形），不吞异常、不用裸 bool 表达失败语义（新代码）。

目录布局目标（随框架层抽取渐进迁移，不搞一次性大搬家）：
- `src/infra/`（基础设施，稳定后抽 submodule）
- `src/engine/`（业务组件：camera / scene / device …）
- `src/framework/`（runtime / control_plane / ui shell）
- `src/apps/`（samples）

### 设计与代码准则

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
