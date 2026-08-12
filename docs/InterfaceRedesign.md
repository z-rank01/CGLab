# GltfSponzaSample 交互界面与交互能力设计案（v2）

> v2（2026-08）：P0–P2 已落地（`0a42ec44` 控制平面 / `9ce28037` 相机与输入 / `2a466de1` 场景系统）。
> 本版按 `Architecture.md` v2 的四层模型**重排路线图**：engine runtime 抽取（I0）先于正式 Web UI。
> v1 的方案搜罗（§3 选型分析）、协议与线程模型（§4）、单窗口路线（§4.5/4.6）、健壮性清单（§6）
> 结论全部沿用且已被实现验证，本文不再重复论证，只保留结论与偏差记录。

## 1. 已落地（as-built）

| 阶段 | 交付 | 关键事实 |
|---|---|---|
| **P0 控制平面** | `src/control_plane/`：JSON-RPC 2.0 over WebSocket（localhost:17381，ixwebsocket） | IO 线程解析校验入队、主线程帧边界消费；10Hz `telemetry.frame`；`session.init/debug.echo/frame.pause/resume/step`；CLI `--no-ui/--ui-port/--ui-open-browser` |
| **P1 相机与输入** | `input_router.h` 绑定表；fly/orbit/bookmark 多模式相机（SoA 不变） | `camera.set_mode/set_params/get_state/bookmark.*`；FOV 钳制修复（min/max_fov 可配） |
| **P2 场景系统** | `src/scene/scene_registry.h`；统一 geometry arena；异步 glTF 加载 | `scene.load_asset/unload/set_visibility/set_transform/select/list`；`telemetry.scene`；fly 左键射线拾取；启动资产登记只读条目 |
| dev console | `scripts/control_plane_dev_console.html`（非正式 UI，不入库提交） | 帧控制、相机面板、场景面板（列表/加载/显隐/选中/变换） |

**与 v1 设计的偏差（刻意决策，非遗漏）**：
1. per-object 变换用 **push constant mat4**（v1 写的是 per-draw uniform 数组）——不改 descriptor 布局，代价最小。
2. 运行时几何进 **geometry arena**（64MB+16MB device-local，bump+空闲链表回收），上传批次经 RG cache key 图变体做一次性 copy（v1 写的是 ring staging）——与 legacy `mesh_upload_pending` 同一模式，RG 无需结构改动。
3. ~~glTF 节点变换在 worker 线程侧烘焙进顶点~~（已作废）：DCL 重构后 adapter 保留 node/parent/local transform 行，world transform 由主线程合并时沿 parent 链解析；层级信息不再丢失。
4. 拾取为 fly 模式左键（v1 未定具体键位）；orbit 左键仍是环绕，不冲突。

## 2. 剩余目标（未做部分）

1. **正式 Web UI**：面板式、可拼接拉伸（dock 布局）的编辑器界面，取代 dev console。
2. **Render Graph 可视化**：pass/resource DAG + 每 pass GPU 时序。
3. **单窗口集成**：B1（webview 壳 + 原生视口嵌入）先行，B2（UI 纹理合成进 swapchain）备选。
4. **输入 mapping 序列化**：绑定表 JSON 化（input_router 数据结构已就绪）。
5. **帧调试增强**：截图、倍速、统计直方图（v1 §5.5，按需排入）。

## 3. 新路线图（与 Architecture v2 对齐）

| 阶段 | 内容 | 验收 |
|---|---|---|
| **I0 Engine runtime 抽取（已完成）** | `cglab_engine_runtime` 持有窗口/帧循环/camera/scene/control/asset service；GltfSponzaSample 与 TriangleSample 共享 runtime | TriangleSample 应用文件 100 行以内；runtime 可注入 fake window/backend/asset service 做 CTest |
| **I1 协议收尾 + UI 托管** | 协议文档化（methods/telemetry 的 JSON Schema 固化进 `docs/`）；控制平面加 HTTP 静态文件服务，`--ui-open-browser` 真正生效 | 启动引擎即开浏览器可用 dev console；协议文档与实现对齐有测试 |
| **I2 正式 Web UI v1** | `ui/`（Vite+React+TS）：dock 布局（dockview）、场景树/检视器/相机/帧控制/资产加载/console 面板，布局 localStorage 持久化 | 浏览器打开即完整覆盖 dev console 全部能力；UI 关掉引擎无恙 |
| **I3 RG 可视化** | `debug_dump()` JSON 化（passes/resources/edges）经控制平面下发；React Flow DAG；pass timestamp 时序瀑布 | recompile 后图自动更新；能定位最慢 pass |
| **I4 单窗口 B1** | webview 壳（saucer/CEF/Ultralight 选型 spike 先行）+ SDL 视口 HWND 子区域嵌入；焦点规则：进视口归引擎、出视口归 UI | 单窗口编辑器外观；画面零拷贝零延迟；`--no-ui`/浏览器模式仍可用 |
| **I5 按需备选** | B2（UI 纹理 → RG overlay pass + 输入转发）；gizmo 与拾取高亮；截图/倍速；输入 mapping JSON 化 | 以 I4 体验与授权/体积评估为准 |

依赖关系：I0 已完成；I1 可直接开始。I2 依赖 I1；I3 依赖 I2；I4 依赖 I2；I5 依赖 I4 选型结论。

**与原 v1 排序的差异**：原 P3（Web UI）前插入 I0/I1。原因——Architecture v2 确认 app_sample god object 是复用的最大瓶颈，且正式 UI 的所有面板都应挂在 engine runtime API 上而不是 app_sample 的私有结构上；dev console 已够日常调试，UI 正式化不再是最高优先。

## 4. 沿用不变的 v1 设计结论（速查）

- **选型**：分离式 web（当前形态）→ B1 系统 WebView（单窗口时）；前端栈 React+TS；排除内嵌 GUI（imgui 类）的理由不变。
- **协议**：JSON-RPC 2.0；命令全部帧边界生效；UI 永不直接触碰引擎内存；握手版本协商 + 未知 method 标准错误。
- **线程模型**：IO 线程解析校验入队；主线程消费命令/应用加载结果；跨线程只交换值类型；telemetry 推送有界背压。
- **单窗口路线**：A（像素流）不作为主路线；B1 先行（零拷贝、中等代价、有原生接缝）；B2 备选（无缝但动 RG/输入/焦点，授权与体积待评估）。**三条路线均不需要返工协议层**——这是 P0 协议优先路线的核心收益。
- **健壮性**：进程隔离、命令 schema 校验、资产加载事务性、headless 不变（smoke 契约一行不动）、协议可演进、协议级回放测试文化。
- **面板信息架构**：Scene Tree / 中央（视口+Graph+Stats）/ Inspector / Console 四区，dock 布局，深色引擎工具风。

## 5. 还债清单（计入对应阶段）

- I0：已完成 camera/scene/loader 所有权下沉与 Vulkan renderer target/façade；renderer 内部物理拆文件留作不改变接口的维护任务。
- I1：`docs/` 补协议 JSON Schema（从 `json_rpc.cpp` 的校验逻辑反生成，避免文档漂移）。
- I2：拾取高亮（选中对象描边/变色需要 shader 或 overlay pass，随正式 UI 一起做）。
- scene 层级（flat SoA + parent_index + transform 传播 system）属 Architecture 计划 2，完成后 Interface 侧补"场景树按层级显示"（当前 flat 列表）。
