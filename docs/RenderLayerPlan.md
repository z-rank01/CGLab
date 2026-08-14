# 渲染层 R 系列计划：阴影 pass 与 render-graph 能力扩展

> 状态：**R0a–R0c + R1a–R1d 已实施完成（2026-08-15，提交见 §阶段记录）；R2/R3 后置待立项**。
> 依据：`Architecture.md`（四层模型、还债清单、帧通道术语表）、
> `EngineLayerDoDPlan.md`（D/F 系列推进纪律与文档风格）、render-graph 子仓
> `docs/ArchitectureAndInternals.md`（pass 模型、同步两遍法）与 `docs/计划.md`。
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

## R2 — compare sampler + 硬件 PCF（后置独立段，本轮不做）

RG 子仓 `sampler_desc` 加 compare 字段（min/mag/compare op；`vk_runtime.h` 的
`vk_sampler_desc`、`vk_bindless.cpp create_sampler`、`vulkan_device.cpp` lowering、
DX12/Metal 契约测试同步）→ 主仓 shadow sampler 换 comparison sampler，shader 删
手动比较。触发：R1 落地后作为独立提交评审。

## R3 — 可选后置：阴影视锥剔除 + per-pass draw 遥测

culling_manager 多视图输出（每视图掩码列或双 scratch——注意契约禁嵌套 vector）；
frame_counters 加 per-pass draw 计数（measure 槽 6–15 空闲，协议不动）。触发：
R1 后评估阴影 pass 的 CPU 成本再立项。

## 推进纪律

- 每阶段：实现 → 构建（vcvars64 环境）→ 子仓 ctest / 主仓 cglab ctest 全绿 →
  独立提交（子仓 `[refactor]`/`[feature]`，主仓 `[feature]`/`[refactor]`/`[test]`）
  → 回填本笔记（✅ + commit）。
- GPU smoke（Triangle/GltfSponza 各 6 帧）在 R1d 复验。
- 主仓提交与 render-graph 子仓提交分开（子仓是 submodule，主仓同步指针时一并提交）。

## 不做（本轮范围外）

- cascaded shadow map（CSM 分阶投影后续再说）、PCSS/软阴影。
- 透明物投影（shadow pass 只画不透明两组；透明物不投影是常见简化）。
- reversed-Z（需 depthCompareOp 扩展，正向 Z + bias 够用；R2 评审时再议）。
- compare sampler（R2）、transient→bindless 槽位协议（缺口 D）、后处理
  （per-pass area 已铺路）、GI、IBL。
- 大场景上传路径 / job system / DI 风格统一：维持各自触发条款，不在本轮。

## 阶段与提交记录

| 阶段 | 内容 | 提交 |
|------|------|------|
| 规划 | 本笔记 | — |
| R0a | depth-only pipeline 放宽 + depth 采样视图（✅ 2026-08-15：pipeline 校验允许"无 color 有 depth"；bindless sampled view 按格式选 DEPTH aspect；子仓 25/25 绿） | 待提交 |
| R0b | per-pass render_area + viewport 跟随（✅ 2026-08-15：`frame_pass_row.area`（0×0 回退帧 extent）；`vk_indexed_scene_record`/`vk_indexed_indirect_record` 的 extent → render_area；recorder 按 pass area 设 viewport/scissor；`compiler_contract_test` 增 `raster_pass` 的 area 断言） | 待提交 |
| R0c | capabilities 设备实测（✅ 2026-08-15：`capabilities()` 改实例方法查 `vkGetPhysicalDeviceProperties`/`FormatProperties`；`backend_capabilities` 加 `supports_depth_sampled`；`validate_image_desc` 消费） | 待提交 |
| R1a | sun_light 通道（✅ 2026-08-15：`apps::sun_light` 状态通道（direction/intensity/color/view_proj/ortho_box），sample 发布，引擎零改动） | 待提交 |
| R1b | recipe shadow pass（✅ 2026-08-15：persistent 2048² shadow image（DEPTH\|SAMPLED）+ nearest/clamp sampler + per-frame light UBO 进 bindless；depth-only pipeline（复用 vertex layout 仅 location 0，front cull，shadow_push 8 字节）；`build_frame` 双 pass（ShadowPass area 2048² + 主 pass SAMPLED depth-aspect 读）+ push constant 扩 8 uint；shadow 只画不透明单面组） | 待提交 |
| R1c | gltf_shadow.vert + frag 阴影采样（✅ 2026-08-15：glslc 编译通过；frag 平行光主光 + 3×3 手动 PCF + slope-scaled bias；点光保留无阴影） | 待提交 |
| R1d | smoke 契约 + 全量验证（✅ 2026-08-15：契约默认值 4→5 / 1→2 且 `expected_draw_passes_per_frame=2`（triangle 覆盖 1/1/1）；43/43 ctest 绿 + Triangle/GltfSponza GPU smoke 6 帧（--validation）通过零警告） | 待提交 |
| R2 | compare sampler + 硬件 PCF | 后置 |
| R3 | 阴影视锥剔除 + per-pass 遥测 | 可选后置 |
