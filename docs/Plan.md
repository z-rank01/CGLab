# CGLab 统一计划单

> 状态：**生效（2026-08-16）**。本文是全部待办事项的**唯一排序口径**。
> 其余计划文档保留为设计依据与历史记录，不再单独维护顺序：
> `Architecture.md`（as-built 总纲 + 还债清单）、`PerformancePlan.md`（A 系列设计稿 + B/C 路线）、
> `EngineLayerDoDPlan.md`（D/F 设计稿）、`RenderLayerPlan.md`（R 系列设计稿）、
> `EngineRoadmapPlan.md`（M1–M10 动机与改动点细节）、`InterfaceRedesign.md`（I 系列 UI 路线）、
> `InfrastructureDesign.md`（infra I0–I3）、RG 子仓 `docs/上传路径与大场景规划.md`（T 系列）。

## 0. 定位与排序原则

目标定位：具备健壮性、延展性，且**高性能第一**的渲染研究框架。
排序原则（沿用 M 系列并修订）：**修复/还债 → 性能地基 → 开发者速度 → 画面 → 规模**。

全程纪律：DoD（SoA / CSR / 存在性代替布尔 / 两遍法）、函数式（纯函数 + `result<T>`，无异常）、
副作用归帧边界出口、稳态零分配；`ArchitectureContract.cmake` 与子仓两个契约脚本保持绿；
每阶段独立提交 + 双仓 ctest 全绿 + 行为变化点 GPU smoke（--validation）+ benchmark 数据记录。

## 1. 执行序列

| 序 | 里程碑 | 内容 | 设计细节 | 理由 |
|---|---|---|---|---|
| 1 | **M4** | R2 compare sampler + 硬件 PCF（reversed-Z 评审随附） | `EngineRoadmapPlan.md` §M4 → `RenderLayerPlan.md` §R2 | 阴影质量第一档；M3 的 debug view 正好做 PCF 前后 A/B |
| 2 | **M5** | R3 阴影视锥剔除 + per-pass 遥测 | `EngineRoadmapPlan.md` §M5 → `RenderLayerPlan.md` §R3 | shadow pass 全量画的 CPU 随场景增长；遥测是 M8 数据源 |
| 3 | **H1** | 录制路径 DoD 还债（子仓 backend + 主仓热路径 + 启动异常契约） | 本文 §2 | 每帧堆分配/指针追逐是高性能目标的直接债务；小、机械、可测 |
| 4 | **M7** | T1b 分块流式上传（跨帧续传 + 进度经 A0 协议上报 + T0–T3 回退） | `EngineRoadmapPlan.md` §M7 + 子仓 `上传路径与大场景规划.md` §五.2/§六 | 1–3 s/GB 单事务挂帧是最大架构级瓶颈；M2（arena 池）已就绪 |
| 5 | **M6** | R5 后处理链路（半分辨率 + tonemap + resolve；`--debug-view` 扩 hdr/resolved） | `EngineRoadmapPlan.md` §M6 | 画面完成度最高杠杆；R0b 的 per-pass area 已铺路 |
| 6 | **M8** | B1→B3：控制平面收尾 + Web UI + RG 事件浏览器 | `EngineRoadmapPlan.md` §M8 + `InterfaceRedesign.md` I1–I3 | 消费 M5 遥测 + M3 调试状态；Frame Debugger 形态 |
| 7 | **M9** | IBL：环境贴图 + 预过滤 | `EngineRoadmapPlan.md` §M9 | 材质正确性；lights_table 的第一个真实消费者方向 |
| 8 | **M10** | CSM 分阶阴影 | `EngineRoadmapPlan.md` §M10 | 大场景阴影；依赖 M4/M5 全部就绪；**前置约束：M5 的光剔除在相机可见集上游（视锥外遮光体不投影），CSM 立项前需解耦两路剔除** |

与原 `EngineRoadmapPlan.md` 的两处调整：

1. **M7 提前到 M6 之前**。原排序 M6（画面）先于 M7（规模/加载）；按"高性能最重要"的定位，
   1–3 s/GB 的挂帧墙应先于色调映射解决。M2 已闭环，无新增前置。
2. **新增 H1（还债段）**。2026-08-16 风格审查（见 §6）发现子仓 Vulkan 录制路径残留每帧堆分配
   与指针追逐（P0–P7 曾在 core 侧消灭同构问题，backend 执行侧当时显式排除在范围外），
   主仓也有两处每帧临时 vector 与启动路径 8 处 `throw`。属"修复/还债"，排在小件能力之前。

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
| C1 job system（infra I0–I3：线程池 + MPMC + 任务图） | M7 专用传输线程立项 / 第二个真实并发负载 | `InfrastructureDesign.md` §2/§3 |
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

## 4. 验收欠债登记（已实施、数据未闭环）

- **Sponza 实机数据回填**：`telemetry.frame.counters.{visible,culled,draws}`、`telemetry.load` 分段、
  剔除前后帧时对比（`PerformancePlan.md` §A1/A2 验收口径）——待有 Sponza 资产的机器。
- **M2 多 arena GPU 实机路径**：>256MB 真实资产加载（现仅 benchmark 逻辑级覆盖 arena_count=3）。

## 5. 推进纪律

- 每阶段：实现 → 构建（vcvars64 环境）→ **子仓 + 主仓 ctest 全绿**（主仓构建需保持
  `CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS=ON`，默认随 `BUILD_TESTING`，2026-08-16 已修正缓存）
  → 独立提交（子仓 `[refactor]`/`[feature]`，主仓 `[feature]`/`[refactor]`/`[test]`/`[docs]`）
  → 回填本文 ✅ + commit。
- GPU smoke（Triangle/GltfSponza/Shadow/Culling 默认 + `--debug-view` shadow/depth 共 7 组，
  `--validation`）在行为变化点复验；benchmark/measure 数据记录，以数据为准。
- `ArchitectureContract.cmake` 与子仓 `BackendOwnershipContract`/`CoreLayoutContract` 全程绿；
  新风格规则先落契约检查再实施。

## 6. 不做（当前范围外）

- deferred/G-Buffer（若立项，查看器复用 M3 机制）、GI 本体（M9 只做 IBL）、PCSS 软阴影、透明物投影；
- 完整阶段纯化（阶段间 in/out 表化）——D3 已评估，成本高收益存疑；
- 后端执行侧以外的既有形态：bindless 默认资源内 default normal map（PBR 语义下沉）、
  `vk_graph_executor`/`vk_runtime` 双资源表中心、retirement 命名——记录在 `Architecture.md` 还债清单，
  待触碰相邻代码时顺手收敛，不单独排期。

## 7. 阶段与提交记录

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
| M9 | IBL | 待实施 |
| M10 | CSM | 待实施 |

## 8. 资产加载性能登记（2026-08-18，立项意向：dcl 结构与性能优化）

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
  与 §4 Sponza 回填项一并处理。
- smoke 契约 `expected_indirect_groups_per_frame=3`（`gltf_sponza_sample.cpp:69`）按小资产
  "每 pass 一组"校准，Sponza 实测 5 组/帧——大资产跑 smoke 双配置都会失败，契约口径问题。
- 启动资产加载主线程空转等待（`engine_runtime.cpp:111-125`），窗口假死约 80s
  （控制平面 IO 线程不受影响）；大资产 Debug 迭代建议日常用 RelWithDebInfo。

**立项意向（下一个 feature 分支）**：dcl 仓结构与性能优化，按实测收益排序：

1. **纹理解码管线**（80-90%）：73 张图天然独立——worker 内并行 stb decode（预期数倍）；
   进一步做纹理延迟/异步解码（几何先行注册，配合 M7 流式口径，"可见"时间分钟级→秒级）；
   资产侧建议离线压缩（KTX2/分级），2.6GB 属异常形态。
2. **dcl RGBA 归一化**（8-17%）：4 通道直 memcpy、3→4 通道紧凑循环（去 lambda）。
3. **dcl accessor double 中间态**（1-2%）：类型特化直读 + 紧排整块拷贝，顺手可改。
4. **tinygltf 整体自写替换：评估结论为不做**。瓶颈在纹理解码而非 JSON/转换，自写解决
   错误的问题；兼容性长尾（sparse/Draco/meshopt/KTX2/data URI/扩展机制）全要自扛。
   可选折中：仅当"JSON 极大+纹理少"资产成为常态时，自写 JSON+accessor 聚焦层
   （保 `gltf_loader.h` 接口，tinygltf 作异态回退）。大资产优先 .glb。

## 9. UI 重构立项意向（2026-08-18，单独 feature 分支）

B2/B3 落地后的实机反馈（三角/Shadow/Sponza 目检）：

1. **视觉**：单一灰蓝配色 + 原生控件观感简陋；缺设计令牌（色板/间距/圆角/字体层级），
   按钮/输入/表格/滚动条样式未统一，信息密度与层次不清。
2. **默认布局失衡**：左侧「帧控制/调试视图/帧计数」tab 组内容单薄却占约 1/3 屏宽，
   数据密集面板（帧时曲线/RG 图/阶段耗时/控制台）反被压缩；默认布局应按
   "控制窄栏 + 数据主区"重新配比，或评估响应式默认布局。

**立项意向（单独 feature 分支）**：ui/ 视觉与布局整体重构——设计令牌 + 控件样式统一 +
默认布局重排；dockview 布局缓存版本（现 v2）随重构再 bump。范围仅前端，协议/引擎零改动。
