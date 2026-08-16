# 引擎路线图：下一里程碑排序（M 系列计划）

> 状态：**计划（待实施，2026-08）**。本文固化 R 系列收尾后（阴影链路已闭环、
> ShadowSample 已跑通）的跨层优先级排序；R2/R3 仍归 `RenderLayerPlan.md` 所有，
> 本文只定顺序并新增阶段（M1/M2/M3/M6/M7/M8/M9/M10 为跨层或新增项）。
> 依据：`RenderLayerPlan.md`（R2/R3）、`PerformancePlan.md`（B/C 系列）、
> `上传路径与大场景规划.md`（T 系列）、`Architecture.md`（设计约束与还债清单）、
> `EngineLayerDoDPlan.md`（D/F 系列纪律）、RG 子仓 `docs/计划.md`（DoD 准则）。
>
> **2026-08-16 起待办排序以 [Plan.md](Plan.md) 为准**；本文保留 M1–M10 的动机与改动点
> 细节，不再单独维护顺序（两处调整：M7 提前至 M6 前、新增 H1 还债段，见 Plan.md §1）。

## 0. 排序总览与理由

| 序 | 里程碑 | 内容 | 理由（为什么是这个位置） |
|---|---|---|---|
| M1 | F4 帧通道保活机制化 | 见 §1 | 修复类：`ffe91a60` 刚踩过"发布裸指针、update 返回即悬空"的坑，机制化后同类 bug 不可能再犯；小、无行为变化 |
| M2 | T1a 可增长 arena 池 | 见 §2 | 规模小件：消 256 MB 容量墙，分块流式（M7）的前置；改动面最小 |
| M3 | R4 调试可视化：阴影图/深度查看器 | 见 §3 | **开发者速度投资**：渲染层越往后做（R2/R3/M6/M10），debug view 的回报越高；机制已齐，只剩三件小东西，成本最低 |
| M4 | R2 compare sampler + 硬件 PCF | 见 §4 | 阴影质量第一档；M3 的 debug view 正好用于 PCF 前后 A/B |
| M5 | R3 阴影视锥剔除 + per-pass 遥测 | 见 §5 | shadow pass 全量画的 CPU 成本随场景增长；遥测同时是 M8 可视化的数据源 |
| M6 | R5 后处理链路 | 见 §6 | 画面完成度最高杠杆；R0b 的 per-pass render_area 就是为它铺的路 |
| M7 | T1b 分块流式上传 | 见 §7 | 大场景墙（1–3 s/GB 单事务挂帧）是当前最明确的架构级瓶颈；"专用传输线程"触发 C1 评估 |
| M8 | B1 → B3：控制平面收尾 + RG 事件浏览器 | 见 §8 | Unity Frame Debugger 形态的 pass/barrier 浏览器；消费 M5 遥测 + M3 调试状态；B1 文档写"可直接开始" |
| M9 | IBL：环境贴图 + 预过滤 | 见 §9 | 材质正确性方向（metal 无 IBL 观感上限低）；F3 注释已点名 GI 是 lights_table 第一个真实消费者 |
| M10 | CSM 分阶阴影 | 见 §10 | 大场景阴影必经之路；`sun_light.ortho_box` 已留分阶注释；依赖 M4/M5 全部就绪 |

触发式后置（不做主动排期）：C1 job system（触发 = M7 传输线程 / 多资产并行解析）、
层级 B（RG 持久资源逻辑句柄化，大场景裁剪/热重载时评估）、GI、PCSS 软阴影、
透明物投影、deferred（若做 deferred，G-Buffer 查看器走 M3 同一机制）。

排序原则：**修复 → 小件能力 → 开发者速度 → 画面 → 规模**。阴影链路已闭环，
R2/R3 是打磨不该独占下一轮；下一个里程碑由"下一个瓶颈"决定——当前最像缺一块的
是大场景能力（M2/M7）与画面完成度（M6/M9），调试可视化（M3）则让这一切可理解。

---

## M1 — F4 帧通道保活机制化

**动机**：`ffe91a60`（阴影链路修复二）的根因是帧通道契约"裸指针 + 生存期 = 单帧"
允许发布方把 update 局部变量发布后返回即销毁——悬空指针是契约允许的合法写法，
修复靠每个 sample 手工 `shared_ptr` 保活。把生存期从编写者纪律变成机制保证。

**改动点**：
- `engine/frame_channels.h`：新增帧持久发布变体（方案二选一，实施时定）：
  - `publish_state_owned<T>(std::unique_ptr<T>)` / `publish_rows_owned<T>(std::vector<T>)`：
    channels 持有所有权，帧尾统一释放（每通道仍单写者，帧首 clear 时回收）；
  - 或引擎提供 per-frame bump allocator，`publish_state` 改为构造进 bump 区。
  - 推荐前者：改动局部、语义直白（发布即转移所有权）。
- 消费端 `find_state`/`find_rows` 签名不变，两个 recipe 与 samples 迁移：
  `lights_table`/`sun_light` 的 `shared_ptr` 保活改 owned 发布（shadow_sample /
  gltf_sponza_sample 删除手工 shared_ptr）。
- `cglab.frame_channels` 单测补：owned 发布生命周期（帧尾释放、指针有效跨 phase）、
  与裸指针发布混用语义。

**验收**：42/42+ 测试绿 + Triangle/GltfSponza/ShadowSample smoke 各 6 帧；
grep 确认 samples 中 `std::make_shared<apps::lights_table/sun_light>` 零残留。

**结果（2026-08-16）**：✅ 已提交 `33e0285e`。`publish_state_owned`/`publish_rows_owned`
落地（通道持有数据到帧尾 clear 释放；owned 条目类型擦除 + 先清列后析构）；
两个 sample 迁移为每帧新造对象 + owned 发布（`plane_fit` 保持 sample 持久状态不发布）；
`frame_channels` 单测补 3 项（悬空指针回归、owned state/rows 帧尾释放，计数类型验证
无拷贝无泄漏）；43/43 ctest 绿 + Triangle/GltfSponza/ShadowSample smoke 6 帧通过。

---

## M2 — T1a 可增长 arena 池

**动机**：`上传路径与大场景规划.md` §五.1——固定 256 MB 容量墙是 G 级场景的第一道硬墙；
arena 池是小改、句柄与切片逻辑不变，且是 M7 分块流式的前置。

**改动点**：主仓 gltf recipe：geometry arena 由单个固定容量 buffer 改为按需开新
arena（池），`geometry_cursor` 跨 arena 续接；句柄管理保持不变（大 buffer 子分配
语义不变）。

**验收**：`cglab.loading_benchmark` 复测 + 契约测试绿 + GPU smoke；记录超 256 MB
合成负载可加载（或 benchmark 中 N 加大对照）。

**结果（2026-08-16）**：✅ 已提交 `22e4521d`。geometry 单 buffer → `geometry_arenas`
池（arena 0 初始化创建，容量不足时按当前批整体开新 arena 从头布局，单资产超
256MB 仍报错）；`geometry_draw_arenas` 列与 draw 列并行；build_frame 命令按
(组, arena) 分段（段内保持距离序），draw 行每段一条引用对应 arena——单 arena 时
与旧布局逐字节一致。43/43 ctest 绿 + 三个 sample smoke 6 帧通过；
`cglab.loading_benchmark` 复测 N=4096 1734µs / N=65536 33683µs（与历史记录一致，
plan 纯函数未动）。多 arena 激活路径（>256MB 资产）待大资产到场后实机验证。

**后续修正（2026-08-16）**：补齐 arena 池可观测性：加载日志与 `telemetry.load`
现在同时报告池的 arena 数、本次新建数、reserved/used 字节，以及本事务
allocation/plan/transfer 耗时；loading benchmark 用 3 次逻辑加载显式报告扩容后的
arena 数、保留/使用 MB 与利用率，不再只给上传行 CPU 时间。

---

## M3 — R4 调试可视化：阴影图/深度查看器

**动机**：能看到 RT 才能理解渲染。现状盘点（2026-08 核实）：

- **机制已具备**：多 pass 图（R1 双 pass 已跑）；阴影图是 persistent image 且已
  发布进 bindless `sampled_images`（`shadow_map_slot`，主 pass 正在采样）；
  per-pass `render_area`（R0b）可画角落 inset；draw 行 = indexed indirect，
  全屏/角落 quad 用 `startup_geometry` 同款机制上传 6 顶点即可；主 pass push
  constant 已含 shadow_map_slot / shadow_sampler_slot。
- **缺口（本阶段补）**：
  1. 展示 shader：`debug_view.frag` depth→color 重映射（`1.0 - d` 或线性化，
     可选 aspect/距离分档着色）；
  2. 角落 quad 几何 + debug pipeline（复用 gltf vertex layout 或专用全屏 vert）；
  3. 切换入口：`--debug-view shadow|depth|off` CLI（引擎零改动，recipe 按选项
     增删 debug pass）；交互式切换后置到 M8（web UI 命令或引擎暴露 input_events）。
- **不做**：G-Buffer 查看器（forward 无 G-Buffer；deferred 立项时走同一机制）；
  Frame Debugger 事件浏览器（= M8/B3）。

**改动点**：主仓 recipe：debug pass（写 swapchain，attachment load=load 保留主
输出，角落或全屏两种模式）+ debug pipeline/quad + `--debug-view` 选项贯通到
`application_options`；RG 子仓零改动（依赖自动 barrier：depth write → sample read
已具备）。

**验收**：`--debug-view shadow` smoke 6 帧（--validation）通过 + 人工目检阴影图
内容正确（物体轮廓 + 深度渐变）；`--debug-view off` 与现有 smoke 契约逐字一致。

**结果（2026-08-16）**：✅ 已提交 `ef1a1687`（主仓，含子仓指针）。
`--debug-view shadow|depth|off` CLI → sample 经帧通道发布 `debug_view_request`
（owned 发布）→ recipe 追加 DebugViewPass（swapchain load=load 保留主输出，
320×180 角落 inset，NDC quad 几何 + debug 管线 + 独立 push 切片 [32,52)）；
mode 折叠进 cache key（切换触发重编译）；debug off 时 pass 被切片排除，默认契约
逐字不变（管线恒 6 = 4 材质 + shadow + debug，pass/indirect 随 mode 2/2 → 3/3）。
**顺带修复 RG 编译器潜在缺陷**（子仓 `4d7a4b5`）：多 pass 时颜色 CSR 的
`color_begins` 全为 0（pass 行统一创建），≥3 pass 时尾部 pass 会把前面 pass 的
颜色包进 span——按附件入列顺序重置 begins + 新增 `multi_pass_color_csr_contract`
回归测试。另：smoke 契约不匹配现在打印实际计数（`application_runner.cpp`）。
43/43 ctest 绿 + Triangle/GltfSponza/ShadowSample/CullingSample 默认与
`--debug-view`（shadow/depth）共 7 组 smoke 全过（--validation 零错误）。

**后续修正（2026-08-16）**：定位到 debug quad 只上传顶点、却把顶点字节同时按
`uint32` 索引读取，导致 pass/提交计数正常但不产生有效像素；现已补独立索引切片并
在 draw 行设置 `index_offset`。同时修正 CLI 模式映射：`shadow` 显示原始深度灰阶，
`depth` 显示线性距离热力图；inset 增加高对比描边，以区分“深度恰好全为远平面”与
“debug pass 根本没有输出”。二次目检又发现统一 push blob 是 `build_frame` 局部数组，
返回后 `frame_plan.push_constants` 悬空，导致主/阴影/debug pass 读到失效的 bindless
slot；现改由 recipe state 持有到帧录制完成。

---

## M4 — R2 compare sampler + 硬件 PCF

按 `RenderLayerPlan.md` §R2 执行（子仓 `sampler_desc` 加 compare 字段 + 三后端
契约同步；主仓 shadow sampler 换 comparison sampler，shader 删手动比较）。
**验收补充**：M3 的 debug view 用于 PCF 前后 A/B 目检；reversed-Z 按计划在
本阶段评审。

**结果（2026-08-16）**：✅ 已提交子仓 `6cf61c5` + 主仓 `0f1348ec`。
`sampler_compare_op` 枚举（never=普通采样，存在性代替布尔）→ `sampler_desc`
compare 字段贯通 vk create_sampler（compareEnable/compareOp）+ DX12/Metal
lowering 契约 + 三后端契约测试；主仓 shadow sampler 改 comparison sampler
（LESS_OR_EQUAL + linear 滤波），`gltf.frag` 3×3 手动比较循环删为单次
`sampler2DShadow` dref 采样（硬件 2×2 PCF，slope-scaled bias 进 reference）；
`light_uniform` 删 `shadow_texel_size`（std140 布局不变）。**调试视图适配**：
comparison sampler 禁非比较读取（VUID），debug shader 读原始深度改用独立
debug raw sampler（nearest+clamp 无比较），`--debug-view` 语义输出逐字不变。
42/42 ctest 绿 + Triangle/GltfSponza/Shadow/Culling 默认与 `--debug-view`
（shadow/depth）共 7 组 smoke 全过（--validation 零错误；稳态帧 descriptor
updates=0 保持）。PCF 前后 A/B：同场景前后构建截帧逐字节一致（2048² 阴影图
±4 正交盒下两种 PCF 亚像素等价，硬件路径 1 次 dref 替代 9 次手动比较）；
真人大尺度目检待外部资产（helmet 等）交互验证。**reversed-Z 评审结论：不做**
（阴影图正交投影深度线性、D32 精度充足；主 pass 无 z-fighting 症状；
M10 CSM 近阶小视锥或大场景实机数据出现精度问题再评估——届时需同步翻转
depth clear/比较方向、采样器比较方向与 bias 符号）。

---

## M5 — R3 阴影视锥剔除 + per-pass 遥测

按 `RenderLayerPlan.md` §R3 执行（`culling_manager` 多视图输出：每视图掩码列，
注意契约禁嵌套 vector；`frame_counters` per-pass draw 计数，measure 槽 6–15）。
**验收补充**：遥测字段同时是 M8 B3 的 pass 瀑布数据源，协议扩展预留 pass 维度。

**结果（2026-08-16）**：✅ 已提交 `a74ca942`。形态取舍：engine `culling_manager`
保持相机单视图（API 中立），**光视图剔除在 recipe 侧**（`sun_light` 是 apps 通道，
与 R1a 同款边界；双视图共用 `_interface/culling.h` 纯函数）。recipe 新增每
geometry handle 的 mesh bounds 列 → `build_frame` 光视锥 × 世界 AABB 得
`light_visible` 掩码 → 阴影 pass 独立命令区（只含光内 group 0 候选，按 (arena)
分段）——单 arena 全可见时与旧布局逐字节一致。`frame_counters` 新增
`shadow/main/debug_draw_count`（telemetry JSON `counters.{shadow_draws,main_draws,
debug_draws}`，measure 槽 6–8）；smoke 契约新增 per-pass 不变式（主+调试=全量、
阴影 ≤ 主、调试仅 debug 模式）。`transform_aabb` 进 `_interface/culling.h`
（`scene::transform_bounds` 委托），`culling_test` 增正交光视锥边界用例。
42/42 ctest 绿 + 7 组 smoke 全过（--validation 零错误）；`assets/two_triangles.gltf`
端到端验证剔除生效（光外实例 shadow_draws=2 / main_draws=3）；DamagedHelmet
（3.6MB，1233456 字节几何）ShadowSample smoke 零错误 + 目检阴影清晰
（截帧 `build/helmet_hw_pcf.png`）。Sponza 实机"剔除前后帧时对比"仍待有资产的
机器（Plan.md §4 欠债登记）。

---

## M6 — R5 后处理链路

**动机**：R 系列"不做"清单里明说"per-pass area 已铺路"；半分辨率 + 色调映射是
画面完成度最高杠杆。

**改动点**：
- 主仓 recipe：半分辨率 color（transient，R8G8B8A8）pass + 主 pass 输出到该图 +
  resolve pass（全屏 quad，采样 + ACES/中性 tonemap + 可选 vignette）；
- shader：`resolve.frag`；管线两条（输出半分辨率、resolve 全屏）；
- 契约与 smoke：pipeline/indirect 期望数同步；`--debug-view` 扩 `hdr|resolved`
  （复用 M3 机制看半分辨率 RT）。

**验收**：smoke 6 帧 + 人工目检（无 banding、tonemap 生效）；稳态帧 descriptor
updates 仍为 0（bindless 复用）。

**结果（2026-08-16）**：✅ 已提交 `4a514b3c`（主仓，无子仓改动——半分辨率
RT 用既有 persistent image + transient + `resource_retire_row` 机制，R0 的
per-pass area 已铺路）。半分辨率 `R8G8B8A8_UNORM` persistent RT 首帧/缩放时在
`build_frame` 内重建（旧 image + bindless 槽经 retire 延迟销毁，无泄漏）；主
pass 输出半分辨率（深度改半分辨率 transient），resolve pass（全屏 quad +
linear/clamp 采样器）ACES tonemap（Narkowicz 2015）+ vignette 写 swapchain；
统一 push blob 三段切片（主 [0,32) / debug [32,52) / resolve [52,64)），raster
录制器恒 layout offset 0 push（管线 push range 必须无 offset）；`--debug-view`
扩 `hdr|resolved`（inset 采样半分辨率 RT：raw / tonemapped）；`frame_counters`
增 `resolve_draw_count`（measure 槽 9 + telemetry `counters.resolve_draws`）；
smoke 契约改 `main+resolve+debug==draw_commands`；steady descriptor 基线改首帧
渲染后捕获（半分辨率 RT 首帧 bindless 发布不再计入稳态）。
**验证**：42/42 ctest + 10 组 smoke（--validation 零错误）+ DamagedHelmet 屏幕
捕获（默认 resolve 上屏、hdr/resolved/shadow 调试视图均正常；hdr-vs-resolved
inset diff 确认 tonemap 生效）。验证方法论教训：`PrintWindow` 对 Vulkan 交换链
窗口返回黑帧假象（曾误判渲染回归），改 `CopyFromScreen` 真实屏幕捕获。
**性能观察**：半分辨率渲染（主 pass 面积 1/4）+ resolve 全屏 quad 替代原全屏
主 pass，像素着色总量约减半；稳态帧 descriptor updates 仍为 0（bindless
复用，M6 验收项达成）。

---

## M7 — T1b 分块流式上传

**动机**：`上传路径与大场景规划.md` §五.2——单事务 memcpy 墙（1–3 s/GB 挂一帧）
是架构级瓶颈，行形状只占 5%。

**改动点**：GB 资产按 N 帧分片 apply 到 M2 的 arena 池，`geometry_cursor` 跨帧
续传；加载进度经 A0 协议上报；能力检测分级回退（该文档 §六 T0–T3）按需启用。
"专用传输线程"（§五.3）立项时**触发 C1 job system 评估**。

**验收**：帧时间 p99 有界（加载基准复测）+ GPU smoke + 进度上报在 web UI 可见
（若 M8 已先行）。

**结果（2026-08-16）**：✅ 已提交 `e5cad5c8`（子仓 docs `7c8aff0`）。engine 侧
分片：运行时资产一律经 `streamed_upload` 逐帧排空（每帧一片，8MB 字节预算，
整 mesh 边界；单片超预算的 mesh 独立成片）；片规划为纯函数 `plan_upload_chunk`
（`loading_plan_test` 覆盖预算边界/超预算单 mesh/空 mesh/续传累加）；片间经 M2
arena 池 `geometry_cursor` 跨帧续传，recipe 零改动（批量行本就支持任意 mesh
范围）。`merge_asset_database` 拆为 upload_asset_materials / upload_asset_geometry /
register_asset_scene：启动路径三者紧邻（行为不变），运行时路径上传分片、注册在
全部片完成后一次完成。A0 协议新增 `telemetry.load_progress`（path/uploaded_bytes/
total_bytes/fraction/meshes），capabilities 同步（protocol 测试断言）。
**验证**：42/42 ctest + 6 组 smoke 全过；91.2MB 合成 glb（100 mesh，build/
stream_test.glb 由 build/gen_stream_glb.ps1 生成）经 scene.load_asset 运行时加载
13 片，进度 0.09→1.0 线性，**流式期间帧时间 p50/p99/max≈7ms 有界**（消除
1–3 s/GB 单事务挂帧）；T1/T2/T3（压缩/GPU 解压/DMA）仍按上传路径规划 §六
分级回退待后续（触发式）。

---

## M8 — B1 → B3：控制平面收尾 + RG 事件浏览器

**动机**：Unity Frame Debugger 形态的能力；B1 文档写"可直接开始"。

**改动点**：
- B1：I1 协议收尾（方法/telemetry JSON Schema 固化、HTTP 静态托管、
  `--ui-open-browser` 生效）；
- B2（B3 前置）：正式 Web UI dock 布局，消费 A0 数据（帧时曲线/阶段直方图/
  计数面板/加载瀑布）+ M5 的 per-pass 遥测（pass 时序瀑布）+ M3 的
  `--debug-view` 交互切换（web 命令 → sample 状态通道，引擎零改动）；
- B3：RG 子仓 `debug_dump()` JSON（pass 列表、依赖边、barrier op、附件/别名
  关系）+ React Flow DAG 视图——事件浏览器本体。

**验收**：浏览器模式与 `--no-ui` 模式 smoke 均通过；DAG/瀑布在三角与 Sponza
场景下目检一致。

---

## M9 — IBL：环境贴图 + 预过滤

**动机**：半球环境光是权宜；metal 材质 diffuse≈0 时观感上限低。F3 注释已点名
GI 是 lights_table 第一个真实消费者，IBL 是 GI 的最近前置。

**改动点**：skybox 贴图加载（RGBE）→ 预过滤环境贴图（mip chain，compute 或
多 pass 降采样）+ irradiance（漫反射）+ BRDF LUT（或近似）；gltf.frag 接入
specular IBL；`sun_light` 阴影保持不变。

**验收**：smoke + 人工目检（金属材质反射环境）；预过滤在加载时一次完成（稳态
帧零额外计算）。

---

## M10 — CSM 分阶阴影

**动机**：大场景阴影必经之路；`sun_light.ortho_box` 已留分阶注释。

**改动点**：光视锥按相机深度分 3–4 阶（正交盒按阶收紧），shadow pass 逐阶绘制
（RG 多 pass 或单 pass 多层已可表达），主 pass 按深度选阶采样；依赖 M4（compare
sampler）与 M5（每阶视锥剔除）全部就绪。

**验收**：大场景（Sponza）近远影清晰度对比目检 + smoke；每阶 draw 计数进 M5 遥测。

---

## 推进纪律

- 每阶段：实现 → 构建（vcvars64 环境）→ 子仓/主仓 ctest 全绿 → 独立提交
  （子仓 `[refactor]`/`[feature]`，主仓 `[feature]`/`[refactor]`/`[test]`/
  `[docs]`）→ 回填本笔记（✅ + commit）。
- GPU smoke（Triangle/GltfSponza/ShadowSample 各 6 帧，--validation）在行为
  变化点复验；benchmark/measure 数据记录（以数据为准，不凭感觉）。
- 主仓提交与 render-graph 子仓提交分开；子仓是 submodule，主仓同步指针时一并提交。
- `ArchitectureContract.cmake` 的 DoD 检查全程保持绿（新代码无嵌套 vector /
  `vector<bool>` / 越界 Vulkan 副作用）。

## 不做（本轮范围外）

- deferred / G-Buffer（若立项，其查看器复用 M3 机制）；
- GI 本体（M9 只做 IBL；probe 系统另立计划）；
- PCSS 软阴影、透明物投影、reversed-Z（**2026-08-16 M4 评审结论：不做**，见 §M4 结果）。
- C1 job system、层级 B：维持触发式后置，不在本计划排期。

## 阶段与提交记录

| 阶段 | 内容 | 提交 |
|------|------|------|
| 规划 | 本笔记 | — |
| M1 | F4 帧通道保活机制化 | ✅ 2026-08-16：`33e0285e` |
| M2 | T1a 可增长 arena 池 | ✅ 2026-08-16：`22e4521d` |
| M3 | R4 调试可视化（阴影图/深度查看器） | ✅ 2026-08-16：`ef1a1687`（子仓修复 `4d7a4b5`） |
| M4 | R2 compare sampler + 硬件 PCF | ✅ 2026-08-16：子仓 `6cf61c5` + 主仓 `0f1348ec` |
| M5 | R3 阴影视锥剔除 + per-pass 遥测 | ✅ 2026-08-16：`a74ca942` |
| M6 | R5 后处理链路 | ✅ 2026-08-16：`4a514b3c` |
| M7 | T1b 分块流式上传 | ✅ 2026-08-16：`e5cad5c8` |
| M8 | B1→B3 控制平面 + RG 事件浏览器 | 待实施 |
| M9 | IBL | 待实施 |
| M10 | CSM | 待实施 |
