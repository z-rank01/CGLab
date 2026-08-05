# VulkanSample 交互界面与交互能力重构设计案

> 状态：**设计评审稿（仅设计，不含实现）**
> 范围：窗口/IO/相机/场景交互/调试可视化的整体重构
> 前置：`feature/vulkan_sample_dod` 已完成 Render Graph 迁移（见 `docs/VulkanSampleRenderGraphMigration.md`）

---

## 1. 现状分析

### 1.1 当前架构盘点

| 层 | 现状 | 位置 |
|---|---|---|
| 窗口 | SDL3，`sdl_window : window` 抽象类，仅 open/close/poll/resize | `src/_interface/sdl_window.*` |
| 输入 | 自定义 `input_event`（union + enum），15 个键码、3 个鼠标键，事件透传 | `src/_interface/input.h` |
| 相机 | DoD 风格 `camera_container`（transform/config 分离），仅一种 free-fly 模式，右键 look、中键 pan、滚轮 zoom | `src/_interface/camera_*` |
| 场景 | 启动时一次性 CLI `--asset` 加载 glTF，顶点烘焙变换后拼成一个大 buffer，**运行时不可增删** | `src/main.cpp` |
| 渲染 | Render Graph（Upload/Draw 两 pass），带 `debug_dump()`、diagnostics、statistics | `third_party/render-graph` |
| 界面 | **无**。imgui 已在 vcpkg 清单并链接，但未被任何代码使用 | `vcpkg.json` |
| 调试 | stdout 日志 + validation 计数 + smoke-test 计数器校验 | `src/utility/logger.*` |

### 1.2 核心痛点

1. **零可视化反馈**：没有 FPS、帧时间、pass 信息、图结构、资源状态的可视化，调试只能靠日志和退出码。
2. **交互面极窄**：相机只有一种模式；无法切换对象、无法拾取、无法暂停/单步帧。
3. **场景不可变**：模型只能在启动时加载一次，运行时无法加载/卸载/隐藏对象，调一次模型要重启进程。
4. **输入系统不可扩展**：键码硬编码在 `key_code` 枚举里，加键要改三处（枚举、SDL 翻译、相机 switch）；输入直接被相机消费，没有"输入路由"概念，将来 UI 焦点和视口输入必然打架。
5. **无显式状态暴露**：Render Graph 有 `debug_dump()`、compile diagnostics、statistics，全部没有出口。

### 1.3 重构约束（来自项目基因，设计必须遵守）

- **函数式 / DoD 风格**：系统是 free function + 数据块，不是 OO 继承树。UI/交互重构不应引入沉重的类继承体系。
- **CLI/smoke-test 必须保留**：`--smoke-test --frames N --validation` 是 CI 验收手段，任何 UI 重构不得破坏 headless 路径。
- **Render Graph 已接管帧事务**：同步、transient、submission plan 归 RG 管，UI 只能"观测"和"下发意图"，不能绕过 RG 直接操作 Vulkan 对象。
- **不使用 in-render GUI（Dear ImGui 等）作为主交互窗口**（用户明确要求）。imgui 依赖可保留或移除。

---

## 2. 目标与非目标

### 2.1 目标

1. 一个**美观、可停靠、面板化**的引擎式界面（类似 Unreal/Unity 编辑器的工具体验），基于 Web 技术栈实现。
2. 完整的**交互能力**：多模式相机、运行时场景管理（加载/卸载/选中/变换对象）、帧控制（暂停/单步/倍速）。
3. **调试可视化**：性能统计、Render Graph 结构图与 pass 时序、validation 错误流、资源检视。
4. **健壮性**：UI 崩溃不影响引擎；引擎崩溃 UI 能看到原因；协议可演进；线程边界清晰。
5. 不破坏现有 CLI、smoke-test、CTest 验收。

### 2.2 非目标（本期不做）

- 材质编辑器、节点式 shader 编辑器。
- 完整的 ECS/场景序列化（保存/加载场景文件可以作为后续阶段）。
- 多视口、多相机同屏。
- 移动端/远程 Web 访问（协议层预留，但不作为验收项）。

---

## 3. 方案搜罗：交互窗口/界面框架选型

用户的问题"当前的交互窗口有哪些框架？比如 React 等？"——React 本身只是**前端 UI 库**，它需要一个宿主（浏览器内核）才能变成桌面窗口。所以选型实际是两步：**选宿主方案** + **选前端栈**。

### 3.1 候选方案总览

按"UI 与引擎的关系"分四大类：

| 类别 | 代表 | 一句话描述 |
|---|---|---|
| A. 内嵌 GUI（**已排除**） | Dear ImGui、Nuklear、RmlUi | UI 作为渲染 pass 画进 swapchain |
| B. 系统 WebView 嵌入 | webview/webview、saucer、Ultralight、CEF、WebView2 直用、Qt WebEngine | 在引擎进程内/旁开一个 Chromium/WebKit 窗口，HTML 即界面 |
| C. 分离式进程 | Electron、Tauri、纯浏览器标签页 | 引擎内嵌 HTTP/WebSocket 服务，UI 是独立进程 |
| D. 远程优先 | 自建 WebRTC / VNC 式 | 引擎为 server，画面也走网络（云渲染方向） |

### 3.2 内嵌 GUI（类 A）——为什么排除合理

Dear ImGui 对 Vulkan 调试面板是行业标准，但作为**主交互窗口**确实有明显短板：无 CSS/排版能力、做富文本/树表/图表/可停靠布局要靠自绘拼装、字体渲染质量一般、与"美观"目标相悖。用户排除它是合理的。值得说明的是：**ImGui 与本设计不冲突**，将来若要做"视口内 overlay"（如 gizmo、屏幕空间调试文字），可以再小范围引入，与 Web UI 主界面共存。

### 3.3 系统 WebView 嵌入（类 B）逐项对比

| 方案 | 内核 | 体积/依赖 | 跨平台 | 与现有 SDL 窗口的关系 | 风险 |
|---|---|---|---|---|---|
| **webview/webview** | 系统自带（Win: WebView2 / mac: WKWebView / linux: WebKitGTK） | 极小，单头文件 | 是，但三端内核不同（渲染差异） | 自开独立窗口，与 SDL 窗口并存 | JS bridge 能力弱（bind/eval），大数据通道要自建；Linux 端依赖 GTK |
| **saucer** | 系统 WebView（WebView2/WKWebView/WebKitGTK） | 小 | 是 | 同上 | 较新，社区小于 webview；同样受系统内核差异影响 |
| **WebView2 直用** | Edge Chromium | 零分发体积（用系统 runtime） | **仅 Windows** | 可 HWND 嵌入/独立窗口 | 锁死 Windows，项目 CMake 有 linux-clang preset，违背跨平台基因 |
| **CEF** | 完整 Chromium | 150–250 MB，多进程 | 是，渲染完全一致 | 独立窗口或离屏渲染合成进 SDL | 集成重（sandbox、子进程、分发体积）；对一个 sample 项目过重 |
| **Ultralight** | 自研 WebKit fork | 中（~50MB） | 是（含主机平台） | 可 CPU 位图 → 纹理合成进 Vulkan pass | **商业授权**（indie 免费，商用付费）；不支持 WebGL/WebRTC；JS 引擎是 JSC |
| **Qt WebEngine** | Chromium | 巨大，且引入整个 Qt | 是 | 取代 SDL 成为窗口系统 | 依赖地震级，直接否决 |

### 3.4 分离式进程（类 C）

| 方案 | 描述 | 优点 | 缺点 |
|---|---|---|---|
| **引擎内嵌 server + 浏览器** | 引擎起一个 localhost HTTP/WebSocket 服务（如 cpp-httplib / ixwebsocket / Boost.Beast），React 构建产物用浏览器打开 | 零额外二进制；UI 进程与引擎进程**天然隔离**（UI 崩不影响引擎，符合健壮性目标）；开发体验最好（Vite 热更新、DevTools 全套）；天然支持"第二块屏幕/平板当控制面板" | 两个窗口；用户第一次要手动开浏览器（可自动 `ShellExecute` 打开） |
| **Electron** | UI 打包成 Chromium 桌面应用，走 WebSocket 连引擎 | 单图标体验；窗口管理可定制 | +150MB 分发体积；Node 依赖；对 sample 项目过重 |
| **Tauri** | Rust 壳 + 系统 WebView，走 WebSocket 连引擎 | 体积小 | 引入 Rust 工具链；系统内核差异仍在 |

### 3.5 前端栈（无论选哪类宿主都适用）

- **UI 框架**：React（生态最大，dock/graph 组件最全）。Vue/Svelte 亦可，差别不大。
- **可停靠布局**：`rc-dock` 或 `dockview`（dockview 更新更活跃，支持浮窗、分组、序列化布局）。
- **Render Graph 可视化**：`React Flow`（节点图事实标准，直接消费 RG 的 `debug_dump()` JSON）。
- **图表（帧时间/pass 时序）**：`ECharts` 或 `uPlot`（高频时序选 uPlot 更轻）。
- **构建**：Vite + TypeScript。

### 3.6 选型结论

**推荐：混合方案 —— 「引擎内嵌控制服务（WebSocket/JSON-RPC）+ React SPA 前端」，宿主分两档：**

- **开发档**：Vite dev server + 浏览器，热更新，DevTools 全开。
- **使用档**：引擎启动时内嵌静态 server 托管 React 构建产物，自动打开默认浏览器；可选（后续）用 `webview/webview` 或 saucer 包一层获得"单应用窗口"体验。

理由：
1. **健壮性最强**：UI 与引擎进程隔离是免费获得的；引擎崩了 UI 还能看到最后的遥测和断线原因。
2. **不污染渲染路径**：SDL 窗口纯粹是 Vulkan viewport，UI 不进 swapchain，不动 Render Graph 的 pass 结构，不出现在同步/cache key 里。
3. **协议即产品**：WebSocket 控制平面同时服务于浏览器、未来的 Electron/Tauri 壳、自动化测试脚本（Python 直连 WebSocket 做集成测试！）——测试能力大幅提升。
4. **跨平台无内核差异**：开发/验收都用 Chromium/Chrome 即可；类 B 方案可作为"打包体验"的可选增强而非地基。
5. 对 sample 项目体量合适：无 CEF/Electron 的重依赖，vcpkg 加一个 WebSocket 库即可。

---

## 4. 总体架构设计

### 4.1 分层视图

```
┌─────────────────────────────────────────────────────────────┐
│  UI 进程（浏览器 / 可选 webview 壳）                          │
│  React SPA：Dock 布局 · 场景树 · 检视器 · 图表 · RG 图视图     │
└──────────────▲───────────────────────────▲──────────────────┘
               │ 命令 (JSON-RPC request)    │ 遥测/事件 (push)
               │ WebSocket (localhost)      │
┌──────────────┴───────────────────────────┴──────────────────┐
│  Control Plane（新模块 src/control_plane）                    │
│  · WsServer：连接管理、协议编解码、schema 版本协商             │
│  · CommandQueue：UI→引擎，无锁 MPSC，帧边界消费                │
│  · TelemetryHub：引擎→UI，节流（10–30Hz）、增量 diff          │
│  · SessionApi：场景/相机/帧控制/调试 四组 JSON-RPC 方法        │
└──────────────▲───────────────────────────▲──────────────────┘
               │ command / intent           │ snapshot / event
┌──────────────┴───────────────────────────┴──────────────────┐
│  Engine Core（现有 + 重构）                                   │
│  app_sample（编排） → vulkan_sample（渲染，RG 帧事务）         │
│  scene_registry（新） camera_rig（重构） input_router（重构）  │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 线程模型

| 线程 | 职责 | 规则 |
|---|---|---|
| 主线程（现有） | SDL 事件、输入路由、相机 tick、帧编排、Vulkan/RG | 唯一允许触碰 Vulkan/RG 的线程 |
| IO/网络线程 | WebSocket 收发、JSON 编解码 | 只与 CommandQueue / TelemetryHub 交换**值类型快照**，绝不持有引擎指针 |
| 资产加载线程池（1–2 个） | glTF 解析、顶点处理 | 产出"待提交场景包"，主线程在帧边界做 GPU 上传（走 RG UploadPass 模式） |

关键不变量：**Vulkan 对象只在主线程出现**；`CommandQueue` 只在每帧 `tick` 开头 drain 一次，所有命令在**帧边界**生效——这与 Render Graph 的帧事务模型天然对齐（命令改变场景/相机 → 改变 cache key 或 uniform → 下一帧 compile/execute），不会出现帧中撕裂。

### 4.3 通信协议

- 传输：WebSocket（localhost，默认端口可配置，CLI `--ui-port` / `--no-ui`）。
- 编码：JSON-RPC 2.0。
  - 命令：`{"method":"scene.load_asset","params":{...},"id":7}` → 带 result/error 的 response。
  - 遥测：server push notification，无 id：`{"method":"telemetry.frame","params":{...}}`。
- 版本：握手时交换 `protocol_version`，不匹配的 UI 显示升级提示而非静默失败。
- Schema：每个方法的 params/result 用 nlohmann-json 手工约束（项目已有该依赖），UI 侧用 TypeScript 类型镜像。所有命令**先验证后入队**，非法参数直接返回 JSON-RPC error，不触碰引擎。

### 4.4 遥测通道（引擎 → UI）

分级节流，避免每帧 60Hz 全量推 JSON：

| 数据 | 频率 | 内容 |
|---|---|---|
| 帧统计 | 10 Hz | fps、frame_time（滚动均值/分位）、draw call 数、RG pass 执行计数（复用 `vulkan_run_statistics`） |
| 相机状态 | 10 Hz | position/yaw/pitch/fov/模式 |
| 场景快照 | 变更时 | 对象列表、选中态、可见性、transform |
| RG 图结构 | 仅 recompile 后 | `debug_dump()` 输出（pass 节点、资源、依赖边） |
| pass 时序 | 10 Hz | Vulkan timestamp query（每 pass GPU 耗时） |
| validation 流 | 即时 | severity/message，UI 侧做成控制台 |
| 生命周期事件 | 即时 | device lost、swapchain recreate、asset 加载完成/失败 |

### 4.5 渲染画面呈现

- SDL 窗口保持为**纯 viewport**。UI 是独立窗口（浏览器/壳）。
- 桌面"单窗口编辑器"体验（可选增强）：webview 壳作为主窗口，SDL 窗口保持独立；真正的"单窗口内嵌"（把 Vulkan swapchain 内容嵌进 webview 面板）需要共享纹理或像素回读，成本高、收益低，**本期明确不做**，在文档中标注为已知取舍。

---

## 5. 交互能力设计

### 5.1 输入系统重构：`input_router`

现状：事件 → 相机直接消费。目标：事件 → 路由器 → 上下文栈分发。

```
SDL event
  → input_event（保持现有 union 结构，键码扩容为完整 scancode 映射表）
  → input_router
      ├─ UI 捕获层（编辑器快捷键：Ctrl+O 打开资产、F11 全屏、Space 播放/暂停）
      ├─ 相机上下文（仅当 viewport 聚焦 & 未被 UI 捕获）
      ├─ 拾取/选择上下文（左键点选对象）
      └─ 调试上下文（F1 切统计、F2 切图视图）
```

- **Action Mapping**：从"键码 → 逻辑动作"改为可配置映射表（`move_forward`/`look`/`select`/`toggle_pause`…），JSON 配置可重绑定。相机不再 switch 键码，而是查询 action 状态（DoD 友好：`action_state_table` 就是一块数据）。
- 现有 `camera_system.h` 的 `process_event` 逻辑保留为"默认 fly 绑定"的一个实现，测试 `cglab.camera_input` 继续有效。

### 5.2 相机系统重构：`camera_rig`

现状：单一 free-fly。目标：**模式化相机**：

| 模式 | 交互 | 适用 |
|---|---|---|
| Fly（现有，保留） | 右键 look + WASDQE | 自由巡场 |
| Orbit / Turntable | 左键环绕焦点、滚轮 dolly、中键 pan | 检查单个模型 |
| First-person walk | 重力/碰撞（简化版：固定高度平面） | 大场景漫游 |
| Bookmark | N 个保存机位，数字键/UI 跳转，支持插值飞行 | 调试对比 |

- 数据结构仍是 SoA：`camera_container` 扩展 `mode` 字段 + 每模式参数块（orbit 有 focus_point/distance；fly 有 speed）。
- 每帧由 `camera_update_context` 驱动（现有结构可复用），模式切换 = 换 update 函数指针/std::function，符合函数式基因。
- UI 检视面板可直接编辑 `camera_config` 数值（fov/near/far/速度/灵敏度），通过 `camera.set_params` 命令下发，帧边界生效。

### 5.3 场景系统：`scene_registry`（运行时对象管理）

现状：启动时烘焙成一个 vertex/index 大数组。目标：

- 引入轻量 **registry**（不是完整 ECS）：
  - `object_id` → { mesh 引用、transform、可见性、名称、source asset }
  - 存储保持 DoD：parallel vectors，id 即索引。
- **运行时加载管线**：
  1. UI 发起 `scene.load_asset`（路径/拖放文件）。
  2. IO 线程用现有 `GltfLoader/GltfParser` 解析 → 产出 `pending_scene_package`。
  3. 主线程帧边界接收 → 走现有 UploadPass 模式上传 GPU（staging→local buffer 扩展为**分段/多 buffer** 或 ring staging）。
  4. 成功后 telemetry 推送新场景快照；失败返回 error 且引擎状态不变（事务性）。
- 支撑操作：加载、卸载（延迟到 frames-in-flight 完成后释放，复用 RG 延迟销毁机制）、显示/隐藏、transform 编辑（UI 数值输入先行，gizmo 后期）、选中（射线拾取：主线程 CPU 射线 vs AABB 先行）。
- 这要求渲染侧从"单一 vertex/index buffer + 单一 draw"演进为"**per-object draw range / 多 draw call + per-object uniform（或 push constant）**"——这是本次重构中对渲染侧最大的改动，需要在设计中明确：uniform 从单个 MVP 变为 per-draw model 数组。

### 5.4 Render Graph 可视化

RG 已有 `debug_dump()`（确定性 dump）与 compile diagnostics——这是本项目做图可视化的**现成金矿**：

- recompile 后把 dump 序列化为 JSON：`{passes:[{id,name,reads,writes}], resources:[{id,name,lifetime}], edges:[...]}`。
- UI 用 React Flow 渲染 DAG：pass 为节点、资源为边/中间节点，点击节点显示 diagnostics/statistics。
- 附加：每 pass GPU 时序（timestamp query）→ 时序瀑布图。这是调试 render graph 最想要的能力，也是"引擎级界面"观感的关键。

### 5.5 帧控制与调试

- 播放/暂停/单步（命令 `frame.pause` / `frame.step`）：主循环暂停渲染但**保持事件泵与 UI 通道存活**（跳过 draw，仍 poll/present 最近帧或保持 acquire）。
- 倍速（delta_time 缩放）→ 相机/动画调试。
- 截图（swapchain 回读 PNG，stb 已有）→ `debug.screenshot`，UI 按钮 + 快捷键。
- RenderDoc：文档化"以 RenderDoc 注入启动"流程即可，不做代码耦合。
- 统计面板：fps/frame time 直方图、validation 计数（现有 atomic 计数器导出）、RG statistics。

### 5.6 UI 面板布局（React SPA 信息架构）

```
┌────────────────────────────────────────────────────┐
│ Menu: File(open asset) View(panels) Debug          │
├──────────┬───────────────────────────┬─────────────┤
│ Scene    │   (视口在 SDL 原生窗口)     │ Inspector   │
│ Tree     │                           │ · 对象属性   │
│ · 对象列表│  ┌──────────────────────┐ │ · 相机参数   │
│ · 显隐   │  │ Graph View (ReactFlow)│ │             │
│          │  │ Stats (uPlot)         │ │             │
│          │  └──────────────────────┘ │             │
├──────────┴───────────────────────────┴─────────────┤
│ Console: validation 流 / 日志 / 命令回显             │
└────────────────────────────────────────────────────┘
布局可拖拽重排（dockview），布局状态 localStorage 持久化
```

视觉风格：深色主题、JetBrains/VSCode 系配色、等宽数字、面板间距紧凑——"引擎工具"观感主要靠一致的设计 token 而非花哨效果。

---

## 6. 健壮性设计

1. **进程隔离**：UI 崩溃/关闭 → 引擎检测到 WS 断开，继续渲染（或按配置等待重连）。引擎崩溃 → UI 显示断线 + 最后遥测。
2. **命令验证**：所有入队命令过 schema + 值域检查（如 fov ∈ [1,175]）；非法命令返回 error 不执行。
3. **背压**：遥测队列有界，UI 慢消费时丢旧帧统计（带 dropped 计数），不阻塞渲染线程。
4. **资产加载事务性**：解析失败/格式不支持 → 引擎状态不变 + error；GPU 上传失败 → 对象标记 failed，可重试可卸载。
5. **headless 不变**：`--no-ui`（或 smoke-test 隐含）时 Control Plane 不启动；smoke-test 的帧契约、计数器、validation 检查一行不动。新增 `--ui-port`、`--ui-open-browser` 参数走现有 `application_options` 体系。
6. **协议可演进**：握手版本 + 未知 method 返回标准 `method_not_found`，UI 灰化对应面板而非崩溃。
7. **确定性**：相机/场景命令都在帧边界生效，配合固定 delta_time 模式可做**协议级回放测试**（录制命令流 → 重放 → 对比遥测序列），与 RG 的确定性 dump 测试文化一致。
8. **测试策略**：
   - C++：input_router 映射测试、scene_registry 单元测试、命令 schema 验证测试（沿用现有 CTest 模式）。
   - 协议级：Python 脚本直连 WebSocket 跑集成场景（加载资产 → 断言遥测快照）。
   - 前端：Vitest 组件测试（可选）。

---

## 7. 分阶段路线图

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P0 协议地基** | Control Plane（WS server、CommandQueue、TelemetryHub）、握手/版本、帧统计遥测、echo/暂停/单步命令 | Python 脚本连上能收遥测、能暂停/单步；smoke-test 全绿 |
| **P1 相机重构** | input_router + action mapping；camera_rig 多模式（fly/orbit/bookmark）；相机参数命令与遥测 | 现有 camera_input CTest 保持绿 + 新映射测试；UI 原型面板可切模式调参 |
| **P2 场景系统** | scene_registry、运行时 glTF 加载（异步）、多 draw/per-object uniform、显隐/选中/卸载 | 运行中加载第二个模型不重启；卸载不泄漏（frames-in-flight 延迟销毁） |
| **P3 Web UI** | React SPA：dock 布局、场景树、检视器、统计图表、console | 浏览器打开即完整可用；UI 关掉引擎无恙 |
| **P4 RG 可视化** | debug_dump JSON 化、React Flow 图视图、pass timestamp 时序 | recompile 后图自动更新；能定位最慢 pass |
| **P5 打磨（可选）** | webview/saucer 壳打包单窗口、gizmo、拾取高亮、截图、布局持久化完善 | — |

依赖关系：P0 是一切的地基；P1/P2 可并行；P3 在 P0 后即可起步（先用假数据/帧统计）；P4 依赖 P3。

---

## 8. 主要风险与权衡

| 风险 | 影响 | 缓解 |
|---|---|---|
| 渲染侧多 draw/per-object 改动波及 RG 图结构 | cache key、uniform 布局、descriptor 都要动 | 放在 P2 独立阶段；保留单模型路径作为回归基线 |
| WebSocket 库选型 | 引入新 vcpkg 依赖 | 候选：ixwebsocket（轻、活跃）、Boost.Beast（重但稳）、cpp-httplib+自写 WS（最少依赖）；P0 前做一次 spike 决定 |
| 双窗口体验被吐槽 | "不像一个应用" | P5 用 webview 壳收敛；README 说明取舍 |
| 浏览器内核差异 | 调试体验不一致 | 开发/CI 统一 Chrome；UI 不用实验性 CSS |
| 遥测/命令 JSON 开销 | 高频下 CPU 浪费 | 分级节流 + 增量 diff；真有瓶颈再换 MessagePack（协议层预留编码协商字段） |
| 键码映射膨胀 | 维护成本 | scancode 翻译表集中一处生成，action 层与物理层解耦 |

---

## 9. 附录：与现有代码的映射

| 新模块 | 建议位置 | 复用/影响 |
|---|---|---|
| `control_plane` | `src/control_plane/`（新 CMake target） | 用 nlohmann-json（已有）；被 `app_sample` 持有 |
| `input_router` | `src/_interface/input_router.*` | `input.h` 键码表扩容；`camera_system` 改为消费 action |
| `camera_rig` | `src/_interface/camera_*` 扩展 | `camera_container` 加 mode；`cglab_camera_input_tests` 保持 |
| `scene_registry` | `src/scene/`（新） | 复用 `dcl::gltf` loader；`main.cpp` 的启动加载逻辑下沉为 registry 的初始填充 |
| 渲染多 draw 改造 | `src/vulkan_sample.cpp` | uniform 从单 MVP → per-draw；UploadPass 支持增量 |
| Web 前端 | `ui/`（新目录，Vite + React + TS） | 与 C++ 通过 `docs/` 中的协议文档同步 |
| CLI 扩展 | `src/application_options.*` | 加 `--ui-port`/`--no-ui`/`--ui-open-browser`；现有参数不变 |
