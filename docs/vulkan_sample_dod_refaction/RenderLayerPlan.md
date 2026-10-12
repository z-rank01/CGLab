# 渲染层 R 系列计划：阴影 pass 与 render-graph 能力扩展

> 状态：**R0a–R0c + R1a–R1e + R2 + R3 + R5 已实施完成（2026-08-16，提交见 §阶段记录）；
> R6 + M9 + M10 已立项待实施（2026-10-12）**。
> 依据：`Architecture.md`（四层模型、还债清单、帧通道术语表）、
> `EngineLayerDoDPlan.md`（D/F 系列推进纪律与文档风格）、render-graph 子仓
> `docs/ArchitectureAndInternals.md`（pass 模型、同步两遍法）与 `docs/计划.md`。
>
> **2026-08-16 起待办排序以 [Plan.md](Plan.md) 为准**（R2/R3 由 Plan.md M4/M5 承接；
> R6/M9/M10 为 Plan.md §5/§6 第一、第二梯队）；
> 本文保留 R 系列设计稿与阶段记录。
>
> 背景：引擎层 D/F 系列已收尾（帧通道插件化 + F3 lights_table 端到端验证），
> glTF/GLB 静态 Core 2.0 PBR 可加载。渲染层目前**单 pass、无阴影、无后处理**，
> 是四层中与"像引擎"差距最大的一层。R 系列第一个真实消费者 = 阴影 pass，
> 同时给 RG 子仓补齐 depth-only pipeline、per-pass render area、设备实测 capabilities
> 三块能力（R2 的 compare sampler 后置）。

## 0. 背景与现状盘点

| # | 现状 | 位置 | 归属 |
|---|---|---|---|
| 1 | gltf recipe 单 pass（"GltfSponzaPass"），无深度写出的第二视图 | `src/apps/gltf_render_recipe.cpp` | R1 |
| 2 | shader 单光源（点光/硬编码方向光），无阴影采样 | `src/shader/gltf.frag` | R1 |
| 3 | RG 子仓：多 pass 图、depth 附件（D32）、pass 间自动 barrier、per-pass push constant/indirect draw **均已具备** | `render-graph/src/core/system.cpp`、`vk_barrier_lowering.h` | — |
| 4 | **缺口 A**：depth-only pipeline 被校验拒绝（硬性要求 ≥1 color format） | `render-graph/src/backend/vulkan/vk_pipeline_store.cpp:73-74` | R0a |
| 5 | **缺口 E**：bindless sampled image view 写死 COLOR aspect，depth 图无法采样 | `render-graph/src/backend/vulkan/vk_bindless.cpp:394-400` | R0a |
| 6 | **缺口 F**：frame_plan 无 per-pass render_area 输入（全帧 = swapchain extent） | `render-graph/src/core/system.cpp:559-565`、`include/render_graph/render_device.h:364-379` | R0b |
| 7 | **缺口 G**：viewport/scissor 硬编码全窗，不跟随 pass area | `render-graph/src/backend/vulkan/vk_scene_recorder.cpp:66-71`、`vulkan_device.cpp:761-768` | R0b |
| 8 | **缺口 H**：`capabilities()` 硬编码空默认值，无 depth format/max dim 实测 | `render-graph/src/backend/vulkan/vk_backend.h:162`、`resource_types.h:194-208` | R0c |
| 9 | F3 lights_table 模式可复用（sample 发布组件表通道 → recipe `find_state` 消费，引擎零改动） | `src/apps/lights_table.h`、`gltf_sponza_sample.cpp:59-66` | R1a |
| 10 | `camera_row` 通道单写者约束：阴影光源视图必须走**新类型通道** | `engine/frame_channels.h`、`engine_runtime.cpp:920` | R1a |
| 11 | smoke 契约：`expected_pipeline_creations=4`、`expected_indirect_groups_per_frame=1` 需同步 | `src/apps/application_runner.h:28-29` | R1d |

**RG 子仓已具备的能力（阴影不需动）**：frame_plan 扁平行 + access 事件自动推导
DAG 调度（`src/core/graph.cpp`）；depth attachment 的 load/store/clear 全套
（`include/render_graph/raster.h`）；depth-only pass 在编译器层面合法
（`system.cpp:691-693` 只拒绝"无 color 且无 depth"）；深度写 → shader 读的自动
barrier 与布局跟踪（`vk_barrier_lowering.h`：DEPTH_STENCIL_ATTACHMENT →
SHADER_READ_ONLY）；pipeline 的 cull/front_face/depth_test/depth_write 状态与
attachment format 进 pipeline key；per-pass push constant 切片与独立 indirect draw
批次。transient 附件惰性分配 + aliasing + plan cache 已生效。

**技术决策（2026-08-15 已定）**：
- 阴影图分辨率 = **固定 2048²**（独立于 swapchain），故 R0b（per-pass render_area）
  必做；per-pass area 同时是 R3 后处理（半分辨率 pass）的前置能力。
- PCF 路线 = **先手动 PCF**（shader 内 3×3 比较，普通 nearest sampler），
  compare sampler（缺口 C）作为 R2 后置独立段。
- 阴影图 = **persistent image**（DEPTH|SAMPLED，D32），一次性 bindless 发布进
  sampled_images 表拿 slot；不做 transient → bindless 的槽位协议（缺口 D 后置，
  暂不需要）。

## R0 — RG 子仓能力扩展（先行，主仓零改动，子仓内独立提交）

### R0a — depth-only pipeline + depth 采样视图
- `vk_pipeline_store.cpp:73-74`：放宽校验——`color_formats.empty()` 时若
  `depth_format != VK_FORMAT_UNDEFINED` 则允许创建（depth-only raster pipeline，
  无 fragment shader；`VkPipelineRenderingCreateInfo.colorAttachmentCount=0` +
  `VkPipelineColorBlendStateCreateInfo.attachmentCount=0` 均合法）。pipeline key
  无需改动（空 color_formats 循环自然为空）。
- `vk_bindless.cpp:394-400`：`allocate_sampled_image(image_handle, format, ...)`
  的 view 创建按格式选 aspect——`format == VK_FORMAT_D32_SFLOAT` 用
  `VK_IMAGE_ASPECT_DEPTH_BIT`，否则 COLOR。depth 图即可被下一 pass 采样。
- 验收：子仓 ctest 全绿；`vulkan_sample_graph_test`（若有 GPU 路径）与
  `render_device_contract_test` 通过；三后端 lowering 契约测试不受影响（本段无 desc
  改动）。GPU 层端到端验证推迟到 R1d。

### R0b — per-pass render_area + viewport 跟随
- `render_device.h` `frame_pass_row` 末尾加 `render_area area{};`
  （0×0 = 回退 environment.extent，向后兼容全部现有 recipe 与测试）。
- `system.cpp:559-565`：`push_pass_row` 的 frame_area 改为逐 pass 取
  `row.area` 有效则用之、否则回退全帧 extent。
- `vk_scene_recorder.cpp:66-71` 与 `vulkan_device.cpp:761-768`：
  `record_indexed_indirect` 的 viewport/scissor 改由传入的 pass area 推导
  （不再是 swapchain extent）；`record_graph` 循环按 `passes.areas[pass]` 传入。
- 验收：子仓 ctest 全绿；`compiler_contract_test` 增加"per-pass area 进 compiled
  areas 列 + 0×0 回退"断言；主仓 recipe 不改也绿（回退路径）。

### R0c — capabilities 设备实测
- `vk_backend.h` `capabilities()` 去 static（用成员 `physical_device`）：
  `vkGetPhysicalDeviceFormatProperties` 查 D32_SFLOAT 的 SAMPLED/深度附件支持 →
  `backend_capabilities` 新增 `supports_depth_sampled` 字段（默认 true）；
  `vkGetPhysicalDeviceProperties` 填 `max_image_dimension`（limits.maxImageDimension2D）
  与 `max_samples`（framebufferColorSampleCounts 推导）。physical_device 为空时回退
  当前默认值。
- `vulkan_device.cpp:602`：`vk_graph_executor::capabilities()` 改为实例调用。
- `validate_image_desc` 消费 `supports_depth_sampled`：DEPTH|SAMPLED 且不支持时拒绝。
- 验收：子仓 ctest 全绿；DX12 stub 保持同构默认值（不实现查询）。

## R1 — 主仓阴影端到端

### R1a — 光源通道（sun_light）
- 新增 `src/apps/sun_light.h`：`{direction, color, intensity, view_proj(正交),
  ortho_box 参数}` 纯 SoA/单例组件表（参照 lights_table 形态）。
- `gltf_sponza_sample.cpp` update：构造 sun_light（斜向平行光 + 由场景包围球推
  正交视锥）并 `services.channels.publish_state<apps::sun_light>()`。
  **引擎零改动**（新类型通道，避开 camera_row 单写者冲突）。
- 验收：frame_channels 发布/消费语义不变；单测不新增（通道机制已测），
  端到端在 R1d。

### R1b — recipe shadow pass
- `initialize` 增建：
  - persistent shadow image（2048²，D32_SFLOAT，usage `DEPTH_STENCIL_ATTACHMENT |
    SAMPLED`，device_local，aliasing forbidden，persistent）→ bindless 发布进
    sampled_images 表 → `shadow_map_slot`；
  - shadow sampler（nearest + clamp_to_edge，手动 PCF 用）→ `shadow_sampler_slot`；
  - 每帧 in-flight 一个 light uniform UBO（`{mat4 view_proj; vec4 direction;
    vec4 color; float intensity;}`）→ uniform_buffers 表 → `light_uniform_slots[i]`；
  - depth-only pipeline：复用 `gltf.vert.spv` 的 vertex layout，**无 fragment
    shader**，color_formats 空、depth_format D32、cull front（防 peter-panning）、
    depth_write on，push constant = vertex stage 小结构
    `{uint light_uniform_slot; uint transform_slot; uint pad;}`。
- `build_frame`：
  - 消费 `sun_light` 通道，上传 light uniform（view_proj 正交矩阵）；
  - frame_plan 增补：ShadowPass（area 2048²，attachment = shadow image depth
    [clear 1.0 / store store]，draw = 不透明两组 [group 0/1] 的深度绘制）+ 主 pass
    加 shadow image 的 `image_access{usage=SAMPLED, read}` 行 + push constant 扩为
    8 uint（新增 shadow_map_slot / shadow_sampler_slot / light_uniform_slot）；
  - `recipe_state` 数组扩容：frame_resources 5→6、frame_attachments 2→3、
    frame_passes 1→2、draws 4→5、frame_buffer_accesses 3→4（+light uniform 读）；
    `cache_key` 保持 0x474c544650425200。
- 验收：全部 cglab ctest 绿；`cglab.architecture_contract` 绿（新代码无嵌套
  vector / vector<bool>）。

### R1c — shader
- 新增 `src/shader/gltf_shadow.vert`：读 light uniform（`view_proj`）+ transform
  表（复用 binding 4 slot 索引），输出 `gl_Position`；depth-only（无 frag）。
- `gltf.frag`：push constant 加 shadow_map_slot / shadow_sampler_slot /
  light_uniform_slot；平行光（sun）作为主光，`world_position` 经 light view_proj
  变换 → NDC → uv/depth，3×3 手动 PCF + slope-scaled bias（`dFdx/dFdy` 深度导数）；
  点光保留为局部补充。light_count==0 回退路径保留。
- `src/shader/compile.bat` 增 `gltf_shadow.vert` 编译行。
- 验收：`glslc` 编译通过；GPU smoke 无验证错误。

### R1d — 契约与验证
- `application_runner.h`：`expected_pipeline_creations` 4→5、
  `expected_indirect_groups_per_frame` 1→2。
- smoke 增补断言：`draw_pass_executions == frame_limit`（每帧两 raster pass）保持
  原语义即可（计数器按 pass 计）。
- 验收：全部 cglab ctest 绿 + Triangle/GltfSponza GPU smoke 各 6 帧
  （`--validation --no-ui --frames 6`）通过；交互模式 `--asset` 外部场景人工目检
  （阴影方向/软硬边/无漏光穿帮）。

### R1e — ShadowSample：小物件阴影展示（追加，2026-08-15）
- 动机：GltfSponzaSample 的太阳近乎垂直入射（63° 俯角）+ 环境光仅 0.03，
  小物件（helmet 等）朝相机的面基本背光，且影子垂直落在物件正下方被自身遮挡
  ——观感"整体在阴影里"。需要一个桌面级展示 sample。
- 新增 `src/apps/shadow_sample.cpp`（复用 gltf recipe，引擎零改动）：
  - 内置 6×6 接影地面（`startup_geometry`，白色默认材质）+ `--asset` 装载物件
    （`required_startup_asset`，两者可共存，`engine_runtime.cpp:114-133` 顺序 merge）；
  - 斜射太阳光（右上前方入射，受光面朝默认相机，影子向左后铺在平面上）；
    光正交视锥收紧到 ±4（Sponza 版 ±40，小物件下阴影图有效分辨率提升 ~100 倍）；
  - 一盏冷色补光（无阴影）lift 背光面。
- 验收：smoke 6 帧（--validation）通过，阴影契约（5/2/2）保持成立。

## R2 — compare sampler + 硬件 PCF（✅ 已实施 2026-08-16，提交见 §阶段记录）

RG 子仓 `sampler_desc` 加 compare 字段（min/mag/compare op；`vk_runtime.h` 的
`vk_sampler_desc`、`vk_bindless.cpp create_sampler`、`vulkan_device.cpp` lowering、
DX12/Metal 契约测试同步）→ 主仓 shadow sampler 换 comparison sampler，shader 删
手动比较。

**实施细节（2026-08-16）**：

- 子仓（`6cf61c5`）：`sampler_compare_op` 枚举（never = 普通采样，存在性代替
  布尔）+ `sampler_desc.compare_op`；`vk_sampler_desc.compare_op` →
  `create_sampler` 按 `compare_op != NEVER` 设 `compareEnable`/`compareOp`
  （linear 滤波 + dref 采样 = 硬件 2×2 PCF）；vulkan lowering 经可测的
  `lower_vk_compare_op`；DX12 `lower/normalize_dx12_sampler_desc`
  （`D3D12_SAMPLER_DESC` + `COMPARISON_FUNC` 往返）、Metal `metal_sampler_lowering`
  契约同步；`resource_description_lowering_test` 增三后端断言（默认 never 不被
  误判为 comparison sampler）。
- 主仓：shadow sampler 改 comparison sampler（`LESS_OR_EQUAL` + linear 滤波），
  `gltf.frag` 的 3×3 手动比较循环删为单次 `sampler2DShadow` dref 采样
  （`OpImageSampleDrefImplicitLod`，slope-scaled bias 进 reference 深度）；
  `light_uniform` 删 `shadow_texel_size`（std140 布局不变，三 pad 补齐）。
- **调试视图适配**：comparison sampler 禁止非比较读取（validation VUID），
  debug shader 读原始深度必须用独立普通采样器——新增 debug raw sampler
  （nearest + clamp，无比较）进 bindless，`debug_push.sampler_slot` 改指它；
  M3 的 `--debug-view shadow|depth` 语义与输出逐字不变。
- reversed-Z 评审结论：**不做**（见 §不做）。

## R3 — 阴影视锥剔除 + per-pass draw 遥测（✅ 已实施 2026-08-16，提交见 §阶段记录）

culling_manager 多视图输出（每视图掩码列或双 scratch——注意契约禁嵌套 vector）；
frame_counters 加 per-pass draw 计数（measure 槽 6–15 空闲，协议不动）。

**实施细节（2026-08-16）**：

- **形态取舍**：原设计"culling_manager 多视图输出"按架构现状收敛为——
  engine `culling_manager` 保持相机单视图（引擎 API 中立，不感知光视图）；
  **光视图剔除在 recipe 侧完成**（光视图是 apps 层概念：`sun_light` 通道 →
  recipe 消费，与 R1a 同款边界）。双视图共用 `_interface/culling.h` 纯函数
  （`make_frustum`/`test_aabb`/新增 `transform_aabb`），各自单写者掩码，
  无嵌套 vector。
- **recipe 侧光视图剔除**：`recipe_state` 新增每 geometry handle 的 mesh 局部
  bounds 列（上传时随句柄维护）；`build_frame` 用 `sun_light.view_proj` 建光
  视锥（sun 缺失 = 无平行光 = 保守全画），逐实例世界 AABB × 光视锥得
  `light_visible` 掩码（mesh 越界保守判可见；光近侧取 -w<=z<=w 松约束，
  宁多画不丢影）；阴影 pass 只画光视锥内 group 0 候选。
- **独立阴影命令区**：光剔除后阴影 pass 无法引用主命令区的连续 span——
  `shadow_commands` 只含可见候选（draw_index 与 transform 行同序），上传到
  indirect buffer 主命令区之后；阴影 draw 行按 (arena) 分段引用该区。
  单 arena 全可见时与旧布局逐字节一致（契约不变）。
- **per-pass 遥测**：`frame_counters` 新增 `shadow_draw_count` /
  `main_draw_count` / `debug_draw_count`（recipe 填；主 pass = 全量候选、
  阴影 = 光内 group 0 候选、调试 = quad 1 条）；telemetry JSON
  `counters.{shadow_draws,main_draws,debug_draws}`（M8 B3 pass 瀑布数据源，
  协议即 pass 维度扩展）；measure 槽 6–8（槽 6–15 预留区间内）。
- **smoke 契约**：新增 per-pass 不变式——主 pass + 调试 pass = 全量 draw、
  阴影 ≤ 主（光剔除只减不增）、调试 pass 仅调试模式画 quad。
- `_interface/culling.h` 新增 `transform_aabb`（闭式解，`scene::transform_bounds`
  委托同一实现）；`culling_test` 增正交光视锥边界（光盒中心可见 / 侧向剔除 /
  far 外剔除 / 近侧保守保留）与 `transform_aabb` 平移缩放旋转用例。
- **剔除生效端到端验证**：`assets/two_triangles.gltf`（第二三角置于光盒外
  x≈6 但相机可见）→ ShadowSample 遥测 `shadow_draws=2 / main_draws=3`
  （光外实例只进主 pass）；smoke（--validation）零错误。

## R5 — 半分辨率后处理链路（✅ 已实施 2026-08-16，提交见 §阶段记录）

**动机**：画面完成度最高杠杆；R0b 的 per-pass render_area 就是为它铺的路。

**实施细节（2026-08-16）**：

- **半分辨率 RT**：persistent `R8G8B8A8_UNORM` image（COLOR_ATTACHMENT|SAMPLED、
  aliasing forbidden），首帧或 swapchain 缩放时在 `build_frame` 内重建
  （`apply_resource_changes` image_creates + bindless_publishes），旧 image 与
  bindless 槽经 `resource_retire_row` 延迟销毁（RG 事务三阶段 + deferred
  destroy 保证 in-flight 安全，无泄漏）；主 pass 深度改半分辨率 transient
  `D32_SFLOAT`（store=dont_care）。主材质管线 `color_formats` 改
  `R8G8B8A8_UNORM`（半分辨率 RT 匹配——UNDEFINED 会与 swapchain
  B8G8R8A8 不匹配导致管线创建失败）。
- **resolve pass**：全屏 quad（复用 debug_quad 几何 + `debug_view.vert`）+ 新增
  linear/clamp 普通采样器（无比较）采样半分辨率 RT，`resolve.frag` 做 ACES
  tonemap（Narkowicz 2015）+ vignette 写 swapchain；swapchain 附件
  load=dont_care（quad 覆盖整帧）。
- **统一 push blob 三段切片**：主 push `[0,32)` / debug_push `[32,52)` /
  resolve_push `[52,64)`；raster 录制器恒在 layout offset 0 push，pass 行的
  `push_constant_offset` 只负责从 blob 选切片（管线 push range 必须无 offset——
  教训：resolve 管线最初配 offset 52 的 push range 导致录制器把主 push 切片
  当 resolve push 用，画面全黑，改无 offset 后正常）。
- **`--debug-view` 扩 `hdr|resolved`**（mode 3/4，复用 M3 机制看半分辨率 RT）：
  debug push `image_slot` 按 mode≥3 选半分辨率 RT 槽、`sampler_slot` 选 resolve
  采样器；hdr=raw RT、resolved=ACES tonemap 后（`debug_view.frag` 复制一份
  ACES）。mode 折叠进 plan cache key（切换触发重编译，同模式帧间命中）。
- **per-pass 遥测**：`frame_counters` 新增 `resolve_draw_count`（恒 1，quad）；
  telemetry JSON `counters.resolve_draws`；measure 槽 9。smoke 契约改为
  `main+resolve+debug == draw_commands`、`resolve_draw_count == (passes>=3 ? 1 : 0)`、
  `debug == (passes==4 ? 1 : 0)`；默认路径（debug off）每帧 3 个 raster pass
  （shadow/main/resolve）。
- **steady descriptor 基线修复**：`render_graph_driver` 的 steady 基线改为首帧
  渲染**后**捕获——半分辨率 RT 首帧 bindless 发布原本计入稳态
  （steady_desc_updates=1 误报），修复后稳态为 0（bindless 复用成立）。
- **验证方法论教训**：`PrintWindow(PW_RENDERFULLCONTENT)` 对 Vulkan 交换链窗口
  返回 DWM 快照不可靠（画面全黑假象，曾误判为渲染回归，二分到 H1/M7 排除）；
  改用置顶 + 延时 + `CopyFromScreen`（真实屏幕）后全部视图正常。
- **验证**：42/42 ctest + 10 组 smoke（--validation 零错误）+ DamagedHelmet
  屏幕捕获——resolve 上屏（mean 12.7 / bright 4678）、hdr inset
  （mean 35.8 / nonblack 14375）、resolved inset（35.4 / 14369）、shadow 视图
  （25.6 / 3156）、triangle 场景（11.9 / 3115）；hdr-vs-resolved inset 逐像素
  diff（mean 6.62 / max 109）确认 tonemap 生效。

## R6 — RG 后端能力：FP16 色彩格式 + compute 路径首验（2026-10-12 立项）

**动机**：`resource_types.h:17-26` 的 `format` 枚举当前只有 `R8G8B8A8_UNORM/SRGB`、`B8G8R8A8_UNORM/SRGB`、`D32_SFLOAT`——**无浮点色彩格式**，故 M6 的 tonemap 实为 LDR 曲线重映射（输入已被 UNORM 钳到 [0,1]，见 `EngineRoadmapPlan.md` §M6 记录）。FP16 是 M9 IBL 的硬前置。compute 同理：`dispatch_row`/`compute_pipeline_desc`/storage image+buffer bindless 表齐备，但主仓零 compute pass、零 dispatch。

**子步骤（各自独立提交验收）**

| 步 | 归属 | 内容 | 验收 |
|---|---|---|---|
| **R6-1** | 子仓 | `format` 增浮点色彩格式（至少 `R16G16B16A16_SFLOAT`）；`lower_vk_format` 映射；色彩附件/采样/blend 能力校验；资源 lowering 契约测试 | 子仓 ctest 全绿；`core_layout`/`backend_ownership` 契约绿 |
| **R6-2** | 主仓 | compute 最小实机路径：一个 compute pass（storage buffer→storage buffer）经 `dispatch_row` 走通，验证 bindless storage 槽位、barrier 注入、dispatch 录制 | 双仓 ctest 全绿；dispatch 路径 `--validation` 零错误；稳态帧 descriptor updates 仍为 0；图形路径回归逐字节不变 |

**落地后（主仓，随 M9 一并）**：半分辨率 RT 换 `R16G16B16A16_SFLOAT`，resolve 的 ACES 改真 HDR 输入（去 LDR 重映射）；`--debug-view hdr` 语义变为"tonemap 前 HDR"（需确认显示映射）。

### 机制用法（不实现，仅接口事实与踩坑）

**compute pass 接线**（`dispatch_row` 路径已通，主仓首次使用）：

| 环节 | 事实 |
|---|---|
| pass 声明 | `frame_pass_row.kind = pass_kind::compute`；`dispatches` 指向 dispatch 行段；`area` 对 compute 无意义 |
| 兼容矩阵 | compute pass **不得**带 `attachments`/`buffer_copies`/`indexed_indirect_draws`（`vulkan_device.cpp:885` 校验），只允许 `buffer_accesses`/`image_accesses` + `dispatches` |
| 管线创建 | `compute_pipeline_create_row`，shader 行须 `stage == compute` 且 `binary_format == spirv`，否则 `vulkan_device.cpp:257` 直接报错 |
| **barrier 自动注入** | 无需手写 barrier。声明正确的 `frame_buffer_access_row`（`usage = STORAGE_BUFFER`、`access = read/read_write`）即由编译器推导同步 |
| bindless 绑定 | 录制时自动绑 set 0（compute bind point，`vk_scene_recorder.cpp:175`），**每 dispatch 零 descriptor 更新**——storage 表访问走 bindless，不需要按 dispatch 声明 descriptor |
| 数据入口 | `buffer_upload_row{device_buffer_handle, offset, span<const std::byte>}`；以 `bindless_publish_row{table = storage_buffers}` 发布供 shader 索引 |

**已知陷阱**

| # | 陷阱 | 依据 |
|---|---|---|
| 1 | `x/y/z` 任一为 0 → 录制整体失败（不是跳过） | `vk_scene_recorder.cpp:151` |
| 2 | push constant 切片越界 → 录制失败；offset/size 取 `frame_plan.push_constants` 的子区间 | `vk_scene_recorder.cpp:167-172`、`vulkan_device.cpp:1055-1063` |
| 3 | **push constant 总预算**：Vulkan 只保证 `maxPushConstantsSize ≥ 128B`。当前 push blob = main + debug + resolve 三段之和（`gltf_render_recipe.cpp:196`），新增 compute 段前先查设备上限 | Vulkan 规范下限 |
| 4 | compute 的 push range 起点为 0（只负责从 blob 选切片）；若扩到统一范围，**四个图形管线的 push range 必须同步扩大**——recorder 恒从 offset 0 推整个 range | `vk_scene_recorder.cpp:178`、M6 记录 |
| 5 | `queue_availability{compute:false, copy:false}`：所有 pass 仍落 graphics queue（无独立 compute 队列，也无需） | `ArchitectureAndInternals.md:567` |

**FP16 格式落地**（R6-1）：格式枚举 → `lower_vk_format` 映射 → 能力校验与调用点见下。

| 环节 | 事实 |
|---|---|
| 能力查询路径 | `backend_capabilities` 是**标量布尔集合**，无 per-format 查询入口；唯一可扩展点是 backend 的 `validate_image_desc`（`vk_backend.h:191`，已是实例方法，内部查 `vkGetPhysicalDeviceFormatProperties`），调用方经 `resource_validation_api.validate_image` 消费 |
| 线性滤波前提 | Vulkan 只**保证** FP16 支持 sampled image 与 color attachment；**线性滤波是可选**（`VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`）。M9 预过滤 mip 采样需线性 → 必须查 `optimalTilingFeatures`（与既有 D32 检查同口径，`vk_backend.h:182-184`） |
| 查询格式必须匹配 tiling | `optimalTilingFeatures` 与 `linearTilingFeatures` 不可混用，device-local 采样图走 optimal |

---

## M9 — IBL：环境贴图 + 预过滤（2026-10-12 补充实施拆分）

> 动机与改动点见 `EngineRoadmapPlan.md` §M9。此处只定拆分与验收口径。

| 步 | 内容 | 验收 |
|---|---|---|
| **M9-1** | compute 首个**真实负载**：运行时生成 BRDF LUT（256² 目标） | LUT 可采样；`--validation` 零错误；稳态帧零额外计算；加载期/启动期一次完成 |
| **M9-2** | skybox + 预过滤 mip chain + irradiance + `gltf.frag` 接入 specular IBL | 金属材质反射环境（人工目检）；smoke 全绿；`sun_light` 阴影行为不变 |

R6-2 是 M9-1 的**能力**前置（路径已验），M9-1 是**真实负载**验收——两步都做，不合并。

---

## M10 — CSM 分阶阴影（2026-10-12 补充设计）

> 动机见 `EngineRoadmapPlan.md` §M10。

**硬前置**：M5 的光视图剔除位于相机可见集上游——视锥外遮光体不投影，这在 CSM 下不可接受（分阶级联需覆盖相机视野外的投影者）。执行序列第一步即**解耦相机/光两路剔除**（Plan.md §3 C3a 已提级为第二梯队项）。

**改动点**

| 层 | 内容 |
|---|---|
| 剔除 | 光视图候选集不再取自相机可见集；相机剔除保持现状（`culling_manager` 单视图不变） |
| recipe | 光正交盒按相机深度分阶收紧（3–4 阶）；shadow pass 逐阶绘制（RG 多 pass 或分层已可表达）；主 pass 按深度选阶采样 |
| 遥测 | 每阶 draw 计数进 `frame_counters`（沿用 R3 的 per-pass 口径，measure 槽扩展） |
| 资产 | `sun_light.ortho_box` 单值 → 分阶列（保留单阶回退语义） |

**验收**：大场景近远影清晰度对比目检（damaged helmet 贴地/大平面）；smoke 契约同步；每阶 draw 计数可见。

**不做**：PCSS 软阴影、透明物投影（沿用本文件 §不做）。

## 推进纪律

- 每阶段：实现 → 构建（vcvars64 环境）→ 子仓 ctest / 主仓 cglab ctest 全绿 →
  独立提交（子仓 `[refactor]`/`[feature]`，主仓 `[feature]`/`[refactor]`/`[test]`）
  → 回填本笔记（✅ + commit）。
- GPU smoke（Triangle/GltfSponza 各 6 帧）在 R1d 复验。
- 主仓提交与 render-graph 子仓提交分开（子仓是 submodule，主仓同步指针时一并提交）。

## 不做（本轮范围外）

> **2026-10-12 修订**：CSM（M10）与 IBL（M9）已移出本清单，立项为 Plan.md §6 第二梯队。

- PCSS/软阴影。
- 透明物投影（shadow pass 只画不透明两组；透明物不投影是常见简化）。
- reversed-Z（**2026-08-16 M4 评审结论：不做**。阴影图是正交投影，深度线性、
  D32 精度充足；主 pass 当前无 z-fighting 症状。若 M10 CSM 近阶小视锥或大场景
  实机数据（Plan.md §8 欠债）出现精度问题再评估——届时翻转需同步改 depth
  clear/比较方向、采样器比较方向与 bias 符号，并重审 `0ef2da49` 的深度约定）。
- transient→bindless 槽位协议（缺口 D）、GI 本体、deferred/G-Buffer。
- 大场景上传路径 / job system / DI 风格统一：维持各自触发条款，不在本轮。

## 阶段与提交记录

| 阶段 | 内容 | 提交 |
|------|------|------|
| 规划 | 本笔记 | — |
| R0a | depth-only pipeline 放宽 + depth 采样视图（✅ 2026-08-15：pipeline 校验允许"无 color 有 depth"；bindless sampled view 按格式选 DEPTH aspect；子仓 25/25 绿） | 子仓 `6fbaa97` |
| R0b | per-pass render_area + viewport 跟随（✅ 2026-08-15：`frame_pass_row.area`（0×0 回退帧 extent）；`vk_indexed_scene_record`/`vk_indexed_indirect_record` 的 extent → render_area；recorder 按 pass area 设 viewport/scissor；`compiler_contract_test` 增 `raster_pass` 的 area 断言） | 子仓 `23158a6` |
| R0c | capabilities 设备实测（✅ 2026-08-15：`capabilities()` 改实例方法查 `vkGetPhysicalDeviceProperties`/`FormatProperties`；`backend_capabilities` 加 `supports_depth_sampled`；`validate_image_desc` 消费） | 子仓 `3c994bc` |
| R1a | sun_light 通道（✅ 2026-08-15：`apps::sun_light` 状态通道（direction/intensity/color/view_proj/ortho_box），sample 发布，引擎零改动） | `9baf0335` |
| R1b | recipe shadow pass（✅ 2026-08-15：persistent 2048² shadow image（DEPTH\|SAMPLED）+ nearest/clamp sampler + per-frame light UBO 进 bindless；depth-only pipeline（复用 vertex layout 仅 location 0，front cull，shadow_push 8 字节）；`build_frame` 双 pass（ShadowPass area 2048² + 主 pass SAMPLED depth-aspect 读）+ push constant 扩 8 uint；shadow 只画不透明单面组） | `9baf0335` |
| R1c | gltf_shadow.vert + frag 阴影采样（✅ 2026-08-15：glslc 编译通过；frag 平行光主光 + 3×3 手动 PCF + slope-scaled bias；点光保留无阴影） | `9baf0335` |
| R1d | smoke 契约 + 全量验证（✅ 2026-08-15：契约默认值 4→5 / 1→2 且 `expected_draw_passes_per_frame=2`（triangle 覆盖 1/1/1）；43/43 ctest 绿 + Triangle/GltfSponza GPU smoke 6 帧（--validation）通过零警告） | `b2d1ac72` |
| R1e | ShadowSample 小物件阴影展示（✅ 2026-08-15：接影地面 + 斜射太阳光 + 紧凑正交视锥 + 冷色补光；smoke 6 帧通过） | `f68bed06` |
| R2 | compare sampler + 硬件 PCF（✅ 2026-08-16：子仓 `6cf61c5`（sampler_desc.compare_op + 三后端契约）；主仓 `0f1348ec`（comparison sampler + dref 硬件 PCF + debug raw sampler）；42/42 ctest + 7 组 GPU smoke 全过；reversed-Z 评审结论=不做） | ✅ 2026-08-16：子仓 `6cf61c5`、主仓 `0f1348ec` |
| R3 | 阴影视锥剔除 + per-pass 遥测（✅ 2026-08-16：recipe 侧光视图剔除（光视锥 × 世界 AABB 掩码）+ 独立阴影命令区 + `frame_counters` per-pass draw 计数（measure 槽 6–8）+ smoke per-pass 不变式；`transform_aabb` 进 `_interface/culling.h`；42/42 ctest + 7 组 smoke 全过 + two_triangles 资产端到端验证（shadow 2 / main 3）+ DamagedHelmet 实机目检） | ✅ 2026-08-16：`a74ca942` |
| R5 | 半分辨率后处理链路（✅ 2026-08-16：半分辨率 persistent RT（随 extent 重建 + retire 延迟销毁）+ 主 pass 半分辨率 + resolve pass（ACES tonemap + vignette）+ `--debug-view` 扩 hdr/resolved + `resolve_draw_count` 遥测（槽 9）+ 统一 push blob 三段切片 + steady 基线首帧后捕获；42/42 ctest + 10 组 smoke 全过 + DamagedHelmet 屏幕捕获（CopyFromScreen，PrintWindow 对 Vulkan 交换链不可靠）） | ✅ 2026-08-16：`4a514b3c` |
| R6-1 | FP16 色彩格式（子仓） | 待实施（Plan.md §5 序 4） |
| R6-2 | compute 路径首验（主仓） | 待实施（Plan.md §5 序 4） |
| M9-1 | compute 真实负载：运行时 BRDF LUT | 待实施（Plan.md §6 序 1） |
| M9-2 | IBL 主链路：skybox + 预过滤 + irradiance | 待实施（Plan.md §6 序 1） |
| M10 | CSM 分阶阴影 | 待实施（Plan.md §6 序 2，前置 C3a） |
