# CGLab 统一计划单

> 状态：**生效（2026-10-12 修订，同日 A3-1/A3-2/I6 落地）**。本文是全部待办事项的**唯一排序口径**。
> 本轮修订新增：§4 质量欠债（复验发现的 A3 两项缺陷）、§5 第一梯队、§6 第二梯队、
> §7 待决策（计算机动画主线）、§10 复验记录。
> 其余计划文档保留为设计依据与历史记录，不再单独维护顺序：
> `Architecture.md`（as-built 总纲 + 还债清单）、`PerformancePlan.md`（A 系列设计稿 + B/C 路线）、
> `EngineLayerDoDPlan.md`（D/F 设计稿）、`RenderLayerPlan.md`（R 系列设计稿）、
> `EngineRoadmapPlan.md`（M1–M10 动机与改动点细节）、`InterfaceRedesign.md`（I 系列 UI 路线）、
> `infra_and_dcl/InfrastructureDesign.md`（infra I0–I3，2026-08-18 起为 feature/infra-and-dcl 生效设计稿）、RG 子仓 `docs/上传路径与大场景规划.md`（T 系列）。

## 0. 定位与排序原则

目标定位：具备健壮性、延展性，且**高性能第一**的渲染研究框架。
排序原则（沿用 M 系列并修订）：**修复/还债 → 性能地基 → 开发者速度 → 画面 → 规模**。

全程纪律：DoD（SoA / CSR / 存在性代替布尔 / 两遍法）、函数式（纯函数 + `result<T>`，无异常）、
副作用归帧边界出口、稳态零分配；`ArchitectureContract.cmake` 与子仓两个契约脚本保持绿；
每阶段独立提交 + 双仓 ctest 全绿 + 行为变化点 GPU smoke（--validation）+ benchmark 数据记录。

## 1. 执行序列

> 序 1–6 已全部完成（见 §12 阶段与提交记录）；序 7 起为当前待办。

| 序 | 里程碑 | 内容 | 设计细节 | 理由 | 归属 |
|---|---|---|---|---|---|
| 1 | ~~**M4**~~ | R2 compare sampler + 硬件 PCF（reversed-Z 评审随附） | `EngineRoadmapPlan.md` §M4 → `RenderLayerPlan.md` §R2 | 阴影质量第一档；M3 的 debug view 正好做 PCF 前后 A/B | — |
| 2 | ~~**M5**~~ | R3 阴影视锥剔除 + per-pass 遥测 | `EngineRoadmapPlan.md` §M5 → `RenderLayerPlan.md` §R3 | shadow pass 全量画的 CPU 随场景增长；遥测是 M8 数据源 | — |
| 3 | ~~**H1**~~ | 录制路径 DoD 还债（子仓 backend + 主仓热路径 + 启动异常契约） | 本文 §2 | 每帧堆分配/指针追逐是高性能目标的直接债务；小、机械、可测 | — |
| 4 | ~~**M7**~~ | T1b 分块流式上传（跨帧续传 + 进度经 A0 协议上报 + T0–T3 回退） | `EngineRoadmapPlan.md` §M7 + 子仓 `上传路径与大场景规划.md` §五.2/§六 | 1–3 s/GB 单事务挂帧是最大架构级瓶颈；M2（arena 池）已就绪 | — |
| 5 | ~~**M6**~~ | R5 后处理链路（半分辨率 + tonemap + resolve；`--debug-view` 扩 hdr/resolved） | `EngineRoadmapPlan.md` §M6 | 画面完成度最高杠杆；R0b 的 per-pass area 已铺路 | — |
| 6 | ~~**M8**~~ | B1→B3：控制平面收尾 + Web UI + RG 事件浏览器 | `EngineRoadmapPlan.md` §M8 + `InterfaceRedesign.md` I1–I3 | 消费 M5 遥测 + M3 调试状态；Frame Debugger 形态 | — |
| 7 | ~~**A3**~~ | 帧边界健壮性：零 extent 帧 + smoke 契约脱硬编码 | 本文 §4 | 窗口最小化即杀进程（实测复现）；smoke 契约只在最小资产上校准、CullingSample 未生效 | 基建 ✅ |
| 8 | ~~**I6**~~ | UI 视觉与布局重构（设计令牌 + 默认布局配比） | `InterfaceRedesign.md` §5 | 纯前端、零协议/引擎改动、验收直观；开发者速度项 | 基建 ✅ |
| 9 | **R6** | RG 后端能力：FP16 色彩格式 + compute 路径首验 | `RenderLayerPlan.md` §R6 | M9 的前置（枚举表当前无浮点格式，tonemap 实为 LDR 重映射）；compute 首次实机验证 | 实验 |
| 10 | **M9** | IBL：环境贴图 + 预过滤 | `EngineRoadmapPlan.md` §M9 + `RenderLayerPlan.md` §M9 | 材质正确性；lights_table 的第一个真实消费者方向 | 实验 |
| 11 | **M10** | CSM 分阶阴影 | `EngineRoadmapPlan.md` §M10 + `RenderLayerPlan.md` §M10 | 大场景阴影；依赖 M4/M5；**前置：解耦相机/光两路剔除（本文 §3 C3a 已提级）** | 实验 |

**归属口径**：`基建` = 夹具与基础设施，无算法学习价值，可直接实现；`实验` = 渲染/算法实现，**是仓库作者本人的实验内容**，只提供机制用法与踩坑清单，不代为实现（见 §9 文档纪律）。

与原 `EngineRoadmapPlan.md` 的两处调整：

1. **M7 提前到 M6 之前**。原排序 M6（画面）先于 M7（规模/加载）；按"高性能最重要"的定位，
   1–3 s/GB 的挂帧墙应先于色调映射解决。M2 已闭环，无新增前置。
2. **新增 H1（还债段）**。2026-08-16 风格审查（见 §11）发现子仓 Vulkan 录制路径残留每帧堆分配
   与指针追逐（P0–P7 曾在 core 侧消灭同构问题，backend 执行侧当时显式排除在范围外），
   主仓也有两处每帧临时 vector 与启动路径 8 处 `throw`。属"修复/还债"，排在小件能力之前。
3. **2026-10-12 新增 A3 / I6 / R6**。复验发现两项健壮性缺陷（§4），按"修复优先"插到最前；
   UI 重构与 R6 属开发者速度与能力补齐，排在画面项（M9/M10）之前。
   A3-1/A3-2/I6 于同日落地（§5 状态列与 §12），**执行序列序 9（R6）起为当前待办**。

## 2. H1 — 录制路径 DoD 还债

子仓 `src/backend/vulkan/`（验收：子仓 25/25 + 主仓 ctest 全绿 + 7 组 smoke；逐段零分配由代码审查确认）：

- `vk_backend.h:227` `emit_barriers` 栈上新建 `vk_barrier_batch`（3 个 vector，每 pass 每帧 2 次）
  → 成员 scratch + `clear()` 复用（batch 已有 clear 接口，本意即复用）；
- `vk_backend.h:289-290` `begin_raster_pass` 每 raster pass 每帧 `color_infos` 堆分配 → 固定容量 scratch；
- `vk_backend.h:467-474` `get_or_create_image_view` 对 AoS `view_cache` 线性 `find_if` → handle 直寻址列；
- `vk_backend.h:818/1108-1109` barrier lowering 每 buffer op `unordered_map::find`，`:430` 每帧哈希插入
  → handle 稠密索引直接寻址（句柄本是稠密索引）；
- `vk_backend.h:1087-1088`、`vulkan_device.cpp:63` 成员列 `std::vector<bool>` → uint8 列（与 core P3 同口径）；
- `vk_backend.h:558-606` plan 重建 block 复用 O(n²) 双循环 + image/buffer 复制粘贴 → 计数/索引化
  （非稳态帧路径，列第二优先）。

主仓：

- `gltf_render_recipe.cpp:674` `group_segments`（`array<vector,4>`）每帧重建 → recipe_state scratch
  （`:627` `group_scratch` 已有同款示范）；
- `gltf_render_recipe.cpp:754-756` `gpu_positions/colors/intensities` 每帧局部 vector → state scratch 列；
- `gltf_sponza_sample.cpp:82/86/95`、`shadow_sample.cpp:118/123/131` 每帧 `make_unique` owned 发布：
  灯光/太阳/调试请求数据帧间静态 → sample 持久持有 + 裸指针 `publish_state`；
  `shadow_sample.cpp:109` `shared_ptr<bool> plane_fit` → sample 普通成员。
  口径（2026-08-16 复盘）：裸指针方案的原始坑是"发布 update 局部变量"（F3/R1 时期悬空，
  `ffe91a60` 以 shared_ptr 保活修复，M1 再把保活机制化为 owned）——**问题从来是"局部变量"，
  而非"持久持有"**。两种范式各司其职：**帧间静态数据 = 持久持有 + 裸指针发布**（零分配，
  发布点注释"持久对象，非局部变量"）；**每帧新建数据 = owned 发布**（机制防呆）。
  `debug_view_request` 保留 owned（语义即每帧一个请求），作为 owned 范式的范例。
- `engine_runtime.cpp:43/58/64/97/106/126/133/143` 启动路径 8 处 `throw` → `result<T>` 初始化错误
  （`application_runner.cpp:69/112` 已有 catch 边界，平滑替换；健壮性项，无热路径影响）。

**结果（2026-08-16）**：✅ 已提交子仓 `42f8091` + `b01fa91`（view cache 修正）+ 主仓 `4716944c`。子仓：`emit_barriers`
成员 scratch（batch 本有 clear 接口）+ `begin_raster_pass` color_info scratch；`view_cache`
AoS `find_if` → handle 直寻址**每 image 固定容量视图槽**（`std::array<image_view_entry,4>` +
count；首版扁平行 CSR 的 retire 压实存在"create-at-tail 后段序反转 → begins 失配"缺陷，
`b01fa91` 改为无位移的按槽实现）；`pending_imported_images/buffers` unordered_map →
稠密索引列（VK_NULL_HANDLE 哨兵，barrier lowering 每 buffer op 直寻址）；
`vector<bool>` 成员列 → uint8（owned_images/owned_buffers、swapchain_initialized）；
plan 重建 block 复用 O(n²) 双循环 → `index_old_blocks`/`claim_old_block`
（unordered_multimap 等值键索引，image/buffer 两侧共用实现）。主仓：
`group_segments` + 光源 GPU 三列 → recipe_state scratch（帧间复用）；两个 sample
灯光/太阳数据 → 持久持有 + 裸指针发布（发布点注释"持久对象，非局部变量"；
shared_ptr 捕获层因 std::function 可拷贝要求）；`plane_fit` shared_ptr → 持久状态
普通成员；`debug_view_request` 保留 owned（F4 范例）；`engine_runtime::initialize`
8 处 throw → `result<bool>`（构造不再抛，runner 检查结果，catch 边界保留兜底）。
子仓 25/25 + 主仓 42/42 ctest 绿 + 9 组 GPU smoke（--validation，含 ShadowSample/
DamagedHelmet/two_triangles 光剔除场景）全过。

## 3. 触发式后置（不主动排期）

| 事项 | 触发条件 | 出处 |
|---|---|---|
| C1 job system（infra I0–I3：线程池 + MPMC + 任务图） | ~~M7 专用传输线程立项 / 第二个真实并发负载~~ ✅ 2026-08-18 触发成立：§13 dcl 纹理并行解码即"第二个真实并发负载"（InfrastructureDesign §2 首选候选族）；I0/I1 已落地（`f3590553`/`ff55eb84`/`d844e4af`），I2 任务图随 RG 传输线程或第二个多阶段负载评审。2026-08-18 决策：Unity 式 safety system（并发别名检查）先不实现 | `infra_and_dcl/InfrastructureDesign.md` §2/§3 |
| C2 primitive/draw 级与 GPU-driven 剔除 | M5 后 per-pass 遥测显示 CPU 仍是瓶颈 | `PerformancePlan.md` C2 |
| C3 场景层级 `parent_index` + 变换传播（含 UI 层级显示） | 需要层级动画/场景树编辑时 | `PerformancePlan.md` C3 |
| B4 单窗口 webview 壳（I4） | M8 的 B2 落地后评估 | `InterfaceRedesign.md` I4 |
| I5 UI 备选（纹理合成/gizmo/拾取高亮/截图倍速/输入 mapping） | 按需 | `InterfaceRedesign.md` I5 |
| 层级 B：RG 持久资源逻辑句柄化 | 大场景裁剪/热重载需求出现时 | RG 子仓 `ArchitectureAndInternals.md` §13.4 |
| T2/T3 NVMe DMA + GPU 解压 | M7 数据证明仍有量级差距时 | 子仓上传规划 §五.4/§五.5 |
| 专用传输线程（§五.3） | 随 M7 立项一并评审（触发 C1） | 子仓上传规划 §五.3 |
| reversed-Z | ~~M4 评审时给结论~~ ✅ 2026-08-16 结论：**不做**（阴影图正交投影深度线性、D32 精度充足；主 pass 无精度症状；M10 CSM 近阶或大场景实机数据出现 z-fighting 再评估） | `RenderLayerPlan.md` §不做 |
| DI 风格统一（`render_driver` function-table vs `asset_service`/`window` 虚接口） | 下次新增边界接口时一并评审 | `Architecture.md` 还债清单 |
| per-pass GPU timestamp 时序瀑布（I3"定位最慢 pass"完整验收项） | 需要按 pass 定位 GPU 热点时 | `InterfaceRedesign.md` I3 |
| 物理多队列启用 / history 资源语义 | RG 预留能力，无排期 | RG 子仓 `ArchitectureAndInternals.md` |
| pipeline 去重的全量哈希 + 线性扫（`vk_pipeline_store.cpp:79-88/249-258`） | 非每帧路径；批量建管线可测到开销时再议 | H1 审查记录 |
| C3a 相机/光两路剔除解耦 | **已提级**：M10 CSM 硬前置，执行序列见 §6 | M5 记录 + `RenderLayerPlan.md` §M10 |

## 4. 质量欠债（2026-10-12 复验登记）

复验口径：Debug 92/92 target 构建 + 46/46 ctest + 4 组 GPU smoke（`--validation`）+ Release 92/92 构建，全绿。以下两项为复验中新发现的真实缺陷。

| # | 症状 | 证据 | 根因 | 处置 |
|---|---|---|---|---|
| A3-1 | 窗口最小化 → 应用 exit 1 | 同一条 `TriangleSample --smoke-test --frames 60`：最小化窗口 = exit 1，正常窗口 = exit 0 | **三段链（2026-10-12 实测修正，原登记不完整）**：① `SDL_GetWindowSize` 在最小化时仍返回最小化前的尺寸（Windows 实测 1280×720），`sdl_vulkan_surface_adapter.cpp` 的 `drawable_extent` 因此看不出异常；② `VkSurfaceCapabilitiesKHR.currentExtent` 此时为 0×0，且 `min/maxImageExtent` **同为 0**，`std::clamp(0, 0, 0)` 把非法值"合法化"→ 交换链被建成 0×0，extent 存为 {0,0}；③ 下一帧 `frame_environment.extent = {0,0,1}`，recipe 的 transient 图描述尺寸为 0 → `vk_backend.h:195` 校验失败 → `frame_status::failed` 穿透 `application_runner.cpp` 变 `EXIT_FAILURE` | 引擎侧识别"窗口不可呈现"降级为跳过（`window::is_minimized()` 事件驱动 + `submit_render_packet` 帧边界出口），子仓 `create_swapchain` 对 0 值兜底；有界帧数运行按上限收尾为**降级完成**而非空转，契约只在"零提交且确实发生过最小化跳过"时接受降级 |
| A3-2 | smoke 契约只在最小资产上成立 | `expected_indirect_groups_per_frame = 3` 按 `assets/triangle.gltf` 校准，而分组数 = 各 pass 的 draw 行数（大资产实测 5）；`culling_sample.cpp:60-68` 未设三字段，契约对其空转 | 契约硬编码"每 pass 一组"，与实际（组 × arena 分段）耦合；"三字段缺省 = 不校验"无显式语义 | 期望值改由 driver 从 `frame_plan` 归约（`last_frame_raster_passes`/`last_frame_draw_groups`），与后端实测累计值**两路交叉校验**；`expected_indirect_groups_per_frame` 删除，`expected_draw_passes_per_frame` 更名 `expected_raster_passes_per_frame`；CullingSample 补齐字段 |

**A3 执行序列**

| 序 | 内容 | 落点 | 验收 | 归属 | 状态 |
|---|---|---|---|---|---|
| A3-1 | 零 extent 帧跳过 + 窗口最小化降级 | `window.h`/`sdl_window`、`engine_runtime`（帧边界出口）、子仓 `vk_runtime.cpp` | 最小化窗口 smoke 退出码 0 且帧计数不推进；常规 smoke 契约逐字不变；新增单测覆盖 | 基建 | ✅ `89398127` + 子仓 `740b625` |
| A3-2 | smoke 契约去硬编码 | `render_backend.h`、`render_graph_driver.cpp`、`application_runner`、四个 `*_sample.cpp` | 多 mesh 资产 smoke 通过；CullingSample 补契约字段；反向验证（改动声明值）如期失败并打印计划规模 | 基建 | ✅ `4bb89d39` |

## 5. 第一梯队执行计划（2026-10-12 立项）

排序原则不变（修复/还债优先），四项均独立提交、独立验收。

| 序 | 阶段 | 内容 | 验收 | 归属 | 状态 |
|---|---|---|---|---|---|
| 1 | **A3-1** | 零 extent 帧跳过 + 窗口最小化降级 | 最小化窗口 exit 0；常规 smoke 全绿 | 基建 | ✅ `89398127` + 子仓 `740b625` |
| 2 | **A3-2** | smoke 契约由实际发射行推导 | 多 mesh 资产 smoke 通过；CullingSample 契约生效 | 基建 | ✅ `4bb89d39` |
| 3 | **I6** | UI 视觉与布局重构（详见 `InterfaceRedesign.md` §5） | 设计令牌统一；默认"控制窄栏 + 数据主区"；`--no-ui` 与协议零改动 | 基建 | ✅ `3906f7db` |
| 4 | **R6** | FP16 色彩格式 + compute 路径首验（详见 `RenderLayerPlan.md` §R6） | 浮点 RT 端到端；compute dispatch 实机零 validation 错误；LDR 路径回归逐字节不变 | 实验 | 待实施 |

依赖：R6 无前置。R6-1（子仓能力）完成后才能开 M9。

## 6. 第二梯队执行计划（2026-10-12 立项）

| 序 | 阶段 | 内容 | 前置 | 验收 | 归属 |
|---|---|---|---|---|---|
| 1 | **M9** | IBL：环境贴图 + 预过滤（拆两步，见 `RenderLayerPlan.md` §M9） | R6-1 | 金属材质反射环境；预过滤在加载期一次完成，稳态帧零额外计算 | 实验 |
| 2 | **M10** | CSM 分阶阴影（见 `RenderLayerPlan.md` §M10） | C3a 解耦 + M4/M5 | 近远影清晰度对比；每阶 draw 计数进遥测 | 实验 |
| 3 | **C3a** | 相机/光两路剔除解耦 | — | 视锥外遮光体仍投影；M5 既有剔除语义单测不变 | 实验 |

**M9 拆分（两步独立验收）**

| 步 | 内容 | 说明 | 归属 |
|---|---|---|---|
| M9-1 | compute 首个真实负载：运行时生成 BRDF LUT | 顺带完成 compute 路径首验（R6-2 的**能力**验收，此处是**真实负载**验收） | 实验 |
| M9-2 | skybox + 预过滤 mip chain + irradiance + `gltf.frag` 接入 specular IBL | 主链路 | 实验 |

## 7. 待决策：计算机动画主线（未排期）

复验确认：全仓无 animation/skin/skeleton/joint/morph/keyframe 任何实现；`Architecture.md` 把动画划在范围外；M9/M10 之后路线图无动画后续里程碑。作为仓库目标之一，动画需显式立项，当前**未进入执行序列**。

待定口径（下一轮决策点）：

| 决策 | 选项 |
|---|---|
| 落点 | DCL 侧新增 animation/skin 行表 vs 主仓独立动画层 |
| 蒙皮求值 | CPU 蒙皮（SoA 行表 + 帧边界副作用）vs GPU 蒙皮（storage buffer + compute，需 R6-2 已就绪） |
| 前置 | C3 场景层级（`parent_index` + 变换传播）是否随动画一并提级 |

**归属：实验**（全部）。行表设计、蒙皮求值、算法体均为作者本人的实验内容；需要时只补机制用法与踩坑清单。

## 8. 验收欠债登记（已实施、数据未闭环）

- **Sponza 实机数据回填**：`telemetry.frame.counters.{visible,culled,draws}`、`telemetry.load` 分段、
  剔除前后帧时对比（`PerformancePlan.md` §A1/A2 验收口径）——待有 Sponza 资产的机器；
  与本节"运行期 per-object draws 未接线"合并处理。
- **M2 多 arena GPU 实机路径**：>256MB 真实资产加载（现仅 benchmark 逻辑级覆盖 arena_count=3）。
- **运行期 per-object draws 未接线**：场景树 "Draws" 列读注册期 `scene_object::draw_count`
  （`scene_registry.h:174` 恒传空 draws），handle 路径恒 0；随 Sponza 数据回填一并接线。
- ~~**大资产 smoke 未复验**~~ ✅ 2026-10-12：A3-2 后用多 mesh 资产
  （`scripts/gen_stream_glb.ps1` 产物的 100 mesh / 91.2MB `build/stream_test.glb`）
  复验通过——契约去硬编码后仍守住不变式，且证明分组期望不再与资产规模耦合。

## 9. 推进纪律

- 每阶段：实现 → 构建（vcvars64 环境）→ **子仓 + 主仓 ctest 全绿**（主仓构建需保持
  `CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS=ON`，默认随 `BUILD_TESTING`，2026-08-16 已修正缓存）
  → 独立提交（子仓 `[refactor]`/`[feature]`，主仓 `[feature]`/`[refactor]`/`[test]`/`[docs]`）
  → 回填本文 ✅ + commit。
- GPU smoke（Triangle/GltfSponza/Shadow/Culling 默认 + `--debug-view`
  shadow/depth/hdr/resolved 共 8 组，`--validation`）在行为变化点复验；benchmark/measure
  数据记录，以数据为准。
- `ArchitectureContract.cmake` 与子仓 `BackendOwnershipContract`/`CoreLayoutContract` 全程绿；
  新风格规则先落契约检查再实施。

**坑清单（2026-10-12 实测所得）**

| # | 坑 |
|---|---|
| 1 | **`SDL_GetWindowSize` 在最小化时不归零**（Windows 上保持最小化前的尺寸），窗口尺寸**不能**作为"可否呈现"的判据。可信来源只有两个：`SDL_GetWindowFlags` 的 MINIMIZED 位，或 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` 的 `currentExtent`。 |
| 2 | 同上状态下的 `minImageExtent`/`maxImageExtent` **也是 0**，`std::clamp(0, 0, 0)` 会把非法值"合法化"——对来自能力查询的尺寸做 clamp 前必须先有下限。 |
| 3 | 复现窗口类缺陷可用 `Start-Process -WindowStyle Minimized`（无需人工最小化）；程序内构造用 `SDL_MinimizeWindow`（最小化前后 `SDL_GetWindowSize` 都不变）。 |
| 4 | **改共享头后必须整体重建再测**：本轮 A3-2 期间出现"只重建目标子集 → 各 TU 对结构体布局认知不一致 → `render_statistics` 读到 `0xCCCC…` 未初始化栈内存"的假缺陷。判据是 ninja 的 `no work to do`，不是"我曾经编译过"。 |

**文档纪律：归属口径**（2026-10-12 确立）

本仓库是仓库作者本人的算法实验台——框架提供机制与约束，算法由作者自己写。

| 归属 | 范围 | 约定 |
|---|---|---|
| **基建** | 健壮性、测试夹具、构建脚本、UI 杂活 | 无算法学习价值，可直接实现并提交 |
| **实验** | 渲染效果、算法、数据结构设计 | **不代为实现**。文档只提供三类内容：机制用法（接口事实）、踩坑清单、验收/契约设计 |

落到文档：`实验` 项只写"机制用法（不实现）"小节（如 `RenderLayerPlan.md` §R6），
不写行表设计、不写算法体、不给出实现步骤；设计决策留给作者。

## 10. 复验记录（2026-10-12，A3-1/A3-2/I6 落地后）

| 项 | 结果 |
|---|---|
| Debug 构建 | ✅ 全 target（含 4 个 sample + 全部 ctest 目标） |
| Release 构建 | ✅ 全 target |
| CTest | ✅ 46/46 通过（3.5s） |
| 子仓 CTest | ✅ 26/26 通过（含 `resource_description_lowering` / 两个契约） |
| GPU smoke ×4（`--validation`） | ✅ Triangle / GltfSponza / Shadow / Culling 全 exit 0，零 validation 错误 |
| GPU smoke `--debug-view` ×4 | ✅ shadow / depth / hdr / resolved 全 exit 0 |
| 最小化窗口（A3-1 回归） | ✅ `TriangleSample --smoke-test --frames 60` 最小化启动 exit 0，stderr 干净，Warning 记录跳过帧数 |
| 多 mesh 资产 smoke（A3-2） | ✅ `GltfSponzaSample ... --asset build/stream_test.glb`（100 mesh / 91.2MB）exit 0 |
| 契约反向验证（A3-2） | ✅ 把声明的 pass 数改成错误值 → exit 1 且打印 `plan raster_passes=3 … declared=5` |
| 控制平面 + Web UI | ✅ WS 监听 + HTTP 托管 `ui/dist`（index.html + 560.9KB JS + 138.7KB CSS 全 200） |
| UI 令牌贯通 | ✅ `ui/src` 内除 `styles.css`/`theme.ts` 外无颜色字面量；构建产物含全部 11 个面板 id 与 `layout.v3` |
| 环境 | RTX 5060 Ti / Vulkan SDK 1.4.328.1 / MSVC 19.44 / Ninja |

## 11. 不做（当前范围外）

- deferred/G-Buffer（若立项，查看器复用 M3 机制）、GI 本体（M9 只做 IBL）、PCSS 软阴影、透明物投影；
- 完整阶段纯化（阶段间 in/out 表化）——D3 已评估，成本高收益存疑；
- 后端执行侧以外的既有形态：bindless 默认资源内 default normal map（PBR 语义下沉）、
  `vk_graph_executor`/`vk_runtime` 双资源表中心、retirement 命名——记录在 `Architecture.md` 还债清单，
  待触碰相邻代码时顺手收敛，不单独排期。

## 12. 阶段与提交记录

| 阶段 | 内容 | 提交 |
|------|------|------|
| M1 | F4 帧通道保活机制化 | ✅ 2026-08-16：`33e0285e` |
| M2 | T1a 可增长 arena 池 | ✅ 2026-08-16：`22e4521d` |
| M3 | R4 调试可视化（阴影图/深度查看器） | ✅ 2026-08-16：`ef1a1687`（子仓 `4d7a4b5`；修复 `acdf5467`/`fcb9b403`） |
| M4 | R2 compare sampler + 硬件 PCF | ✅ 2026-08-16：`0f1348ec`（子仓 `6cf61c5`；reversed-Z 评审结论=不做，见 RenderLayerPlan §不做） |
| M5 | R3 阴影视锥剔除 + per-pass 遥测 | ✅ 2026-08-16：`a74ca942`（recipe 侧光视图剔除 + per-pass draw 计数；42/42 ctest + 7 组 smoke + two_triangles/DamagedHelmet 实机验证） |
| H1 | 录制路径 DoD 还债 | ✅ 2026-08-16：子仓 `42f8091` + `b01fa91`、主仓 `4716944c` |
| M7 | T1b 分块流式上传 | ✅ 2026-08-16：`e5cad5c8`（子仓 docs `7c8aff0`；engine 逐帧分片 + A0 进度上报；91.2MB 12 片加载帧时间≈7ms 有界，资产生成脚本见 scripts/gen_stream_glb.ps1） |
| M6 | R5 后处理链路 | ✅ 2026-08-16：`4a514b3c`（半分辨率 RT + resolve（ACES tonemap + vignette）+ `--debug-view` hdr/resolved + `resolve_draw_count` 遥测；42/42 ctest + 10 组 smoke + DamagedHelmet 屏幕捕获） |
| M8 | B1→B3 控制平面 + RG 事件浏览器 | ✅ 2026-08-18：B1（`0e671a96`）+ B2（`da2f6d88`，ui/ 正式 Web UI + debug.set_view 交互切换）+ B3（`65a1adf8` + 子仓 `bec36d2`：debug_dump JSON 内省 + rg.get_dump/telemetry.rg 下发 + 手写 SVG DAG 面板；GPU timestamp 瀑布后置，见 §3） |
| C1-I0 | infra benchmark 设施 | ✅ 2026-08-18：`f3590553`（src/infra 落地，cglab.infra_benchmark；基线：thread-per-task 165µs/task vs 队列 0.28–1.45µs/task，吞吐拐点 2 workers，数据见 `infra_and_dcl/InfrastructureDesign.md` §5） |
| C1-I1 | 最小 job system + asset_service 迁移 | ✅ 2026-08-18：`ff55eb84` + `d844e4af`（固定池 + 有界队列 + CV + future + 主线程回调队列；验收：空任务 1.0–3.0µs<10µs、CPU 密集并行比 12.6%≤40%、46/46 ctest + 2 组 smoke；数据见 §5 同表） |
| dcl-接口 | dcl 公共加载接口 + image_decode_executor 缝 | ✅ 2026-08-18：子仓 `7951f4e` + 主仓 `2dc686ca`（`dcl::load_asset` 扩展名分发 + `load_options`/`load_report` + 函数表缝；telemetry.load 三段计时接线；Sponza 73 URI 纹理启动加载 29.4s 与旧路径逐字节一致） |
| dcl-并行解码 | job system 并行图像解码执行器 + asset_service 在途节流 | ✅ 2026-08-18：`99b6659a`（每图一任务入池；节流 ≤4 防嵌套提交全池堵死；Sponza 启动加载 29.4s→12.6s） |
| dcl-转换层 | RGBA8 整块 memcpy + accessor 类型化整块展开 | ✅ 2026-08-18：子仓 `64ba4fe` + 主仓 `a74d4d13`（Sponza 启动加载 → Release 7.1–8.2s / Debug 13.6s，vs 初始 Release ~31s / Debug 76–82s） |
| 合并门 | feature/infra-and-dcl 尖端复验（合并 main 前最后一道门） | ✅ 2026-08-22：尖端 `82aac31e`（RG `b63cbf3b` / dcl `324e43c7`，指针提交晚于此前各阶段回填，故单独复验）46/46 ctest（dcl/infra/RG/主仓全覆盖）+ 8 组 GPU smoke（`--validation`：Triangle/GltfSponza/Shadow/Culling 默认 + `--debug-view` shadow/depth/hdr/resolved）全绿 |
| 复验 | main 全量复验（构建 + ctest + smoke + UI）| ✅ 2026-10-12：见 §10；同时登记两项缺陷为 §4 A3 |
| A3-1 | 零 extent 帧：窗口最小化不再杀进程（引擎降级跳过 + 子仓交换链 0 值兜底） | ✅ 2026-10-12：`89398127` + 子仓 `740b625`（最小化启动 smoke exit 0；单测新增最小化用例；46/46 ctest + 26/26 子仓 + 8 组 smoke） |
| A3-2 | smoke 契约脱硬编码（driver 归约计划规模 + 后端实测交叉校验） | ✅ 2026-10-12：`4bb89d39`（多 mesh 资产 smoke 通过、反向验证有牙齿、CullingSample 契约生效） |
| I6 | UI 视觉与布局重构（设计令牌 + `theme.ts` 取色口 + 控制窄栏默认布局） | ✅ 2026-10-12：`3906f7db`（令牌贯通 grep 干净；11 面板零回归；协议/golden 逐字节不变） |
| R6 | RG 后端能力：FP16 色彩格式 + compute 路径首验 | 待实施（§5 序 4；机制用法见 `RenderLayerPlan.md` §R6） |
| M9-1 | compute 真实负载：运行时 BRDF LUT | 待实施（§6） |
| M9-2 | IBL 主链路：skybox + 预过滤 + irradiance | 待实施（§6） |
| M10 | CSM | 待实施（§6） |
| C3a | 相机/光两路剔除解耦 | 待实施（§6） |

## 13. 资产加载性能登记（2026-08-18，立项意向：dcl 结构与性能优化）

实测（`NewSponza_Main_glTF_003.gltf`，vertex+index 192.5MB，MSVC Debug，两次）：
`load_us` 82603529 / 76373045，其中 merge 5999/529us、upload 552795/531820us——
**约 99% 耗在 worker 解析段**。渲染管线本身健康
（Debug 遥测 instances=115 / visible=115 / main_draws=405 / presented 递增，窗口截图确认出图）。

调用链：`asset_service` → `gltf_adapter` → dcl `gltf_loader` → tinygltf。

**分段实测（2026-08-18 二次，临时打点测后即弃；该资产几何 192.5MB + 纹理 2.6GB/73 张；
绝对值随机器负载波动约 ±2x，比例稳定）**：

| 段 | Debug | Release | 说明 |
|---|---|---|---|
| tinygltf 全程（JSON + buffer 读取 + stb 解码） | ~140s（~84%） | ~31-72s（~82-91%） | **2.6GB 纹理 I/O + PNG/JPEG 解码主导**，JSON 占比可忽略 |
| dcl images RGBA 归一化 | ~23.7s（~14%） | ~6.2-6.5s（~8-17%） | 逐像素 lambda 循环 |
| dcl meshes/accessor 展开 | ~2.8s（~1.7%） | ~0.5s（~1%） | double 中间态，占比小 |
| materials/nodes | <0.1s | ≈0 | — |

结论修正：初判"dcl 两个手写循环为主嫌疑"被实测推翻——对纹理重型资产，卡点是
tinygltf 阶段的纹理解码（Release 下同为数十秒级，并非"正常"）；dcl 循环只对
几何主导型资产才重要。

附带发现（已澄清，非缺陷）：

- 场景树 "Draws" 列读注册期 `scene_object::draw_count`，handle 路径恒 0
  （`scene_registry.h:174` 恒传空 draws），Release 同；运行期 per-object draws 未接线，
  与 §8 Sponza 回填项一并处理。
- smoke 契约 ~~`expected_indirect_groups_per_frame=3`（`gltf_sponza_sample.cpp:69`）按小资产
  "每 pass 一组"校准，Sponza 实测 5 组/帧——大资产跑 smoke 双配置都会失败，契约口径问题。~~
  ✅ 2026-10-12 已修复（A3-2，`4bb89d39`，见 §4）：该字段删除，期望由计划归约。
- 启动资产加载主线程空转等待（`engine_runtime.cpp:111-125`），窗口假死约 80s
  （控制平面 IO 线程不受影响）；大资产 Debug 迭代建议日常用 RelWithDebInfo。

**立项意向（下一个 feature 分支）**：dcl 仓结构与性能优化，按实测收益排序：

1. **纹理解码管线**（80-90%）：✅ 并行 stb decode 已落地（2026-08-18，`99b6659a`，
   job system 注入 `image_decode_executor` 缝 + asset_service 在途节流 ≤4 防嵌套死锁，
   Sponza 29.4s→12.6s）；后续：纹理延迟/异步解码（几何先行注册，配合 M7 流式口径）；
   资产侧建议离线压缩（KTX2/分级），2.6GB 属异常形态。
2. **dcl RGBA 归一化**（8-17%）：✅ 已落地（子仓 `64ba4fe`，component==4 整块 memcpy）。
3. **dcl accessor double 中间态**（1-2%）：✅ 已落地（子仓 `64ba4fe`，紧排常用类型
   整块展开；double 接口未动）。
4. **tinygltf 整体自写替换：评估结论为不做**。瓶颈在纹理解码而非 JSON/转换，自写解决
   错误的问题；兼容性长尾（sparse/Draco/meshopt/KTX2/data URI/扩展机制）全要自扛。
   可选折中：仅当"JSON 极大+纹理少"资产成为常态时，自写 JSON+accessor 聚焦层
   （保 `gltf_loader.h` 接口，tinygltf 作异态回退）。大资产优先 .glb。
5. **dcl 公共加载接口**（2026-08-18 设计结论；✅ 已落地：子仓 `7951f4e` + 主仓 `2dc686ca`）：
   统一自由函数形态 `dcl::load_asset(path, options, report*) -> result<asset_database>` 按扩展名分发，
   各格式保同签名入口（`load_gltf`/`load_fbx`/`load_obj`）——不用虚接口类
   （loader 是无状态纯函数，vtable 无收益）；`load_options`/`load_report` 预留并行/延迟
   解码开关与三段计时装点（主仓 `load_report.parse/convert/decode_us` 数据源），
   接口一次定型不再变动；fbx 占位类（namespace fbx + PascalCase，风格发散）作废；
   主仓 `geometry_loader` 收敛为转调 dcl 分发。
   **并行解码的归属**（同日修正）：管线所有权归 dcl（解析→解码→转换是 dcl 内部流程，
   主仓不组合 dcl 阶段）；`load_options` 内定义 `image_decode_executor` 函数表缝，
   主仓以组合根身份注入 job system 实现（对齐 render_driver 函数表 DI 惯例；
   本缝即 §6"DI 风格统一"评审的实验田）。dcl 保持零依赖；infra 抽仓后 dcl 可自带
   默认并行执行器。
