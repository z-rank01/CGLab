# 性能与 A 步骤计划：测量 / 视锥剔除 / 加载路径

> 状态：**A0/A1/A2 已实施（2026-08，提交 `da42119e` → `7d2d26ec` → `942f9fdc` → `6e012c4e` →
> `dee0a36e` → `2953d3bf` → `2b0d23d9`，主仓分支 feature/vulkan_sample_dod）**。
> B/C 步骤仍只记录路线与触发条件，**暂不实施**，实施前另行评审。
>
> 依据：`Architecture.md`（架构与还债清单）、`InfrastructureDesign.md`（基础设施 I0–I3 与触发条款）、
> `InterfaceRedesign.md`（I1–I5 路线）、`RenderGraphAndRHI.md`（资源与内存模型）。
>
> **2026-08-16 起待办排序以 [Plan.md](Plan.md) 为准**；本文保留 A 系列设计稿与 B/C 路线记录，
> 不再单独维护顺序（B1–B3 由 Plan.md M8 承接，C 系列触发条款见 Plan.md §3）。
>
> 本文所有 A 步骤遵守仓库既有准则：DoD（SoA / CSR / 存在性代替布尔 / 两遍法）、
> Component + Manager 数据模型、函数式阶段（副作用集中在帧边界出口）、单向依赖。

## 0. 背景与现状盘点

| 痛点 | 现状根因 | 归属 |
|---|---|---|
| 大 glTF 加载慢 | (1) worker 侧 `dcl::load_gltf` 全单线程（解析+转换+图片解码）；(2) 主线程合并把共享 blob 逐 primitive 回拷为 AoS `geometry_asset`（`Architecture.md` 还债项）；(3) 每 mesh 一次 `apply_resource_changes` 事务（Sponza 数百次独立事务） | A2 |
| 无 frustum culling | 全仓无剔除；recipe `build_frame` 把全部 instance × primitive 转成 draw 命令。`scene_registry`/mesh 行已有 AABB，只是没人消费 | A1 |
| 窗口移动慢 / 帧时间无数据 | RG backend 硬编码 FIFO（vsync）；每帧全量重建 draw 表；telemetry 只有 fps/帧时与 RG 统计，无阶段耗时、draw 数、加载分段 | A0（先测量，再定 present/CPU 优化归属） |
| 单窗口（webUI 合并） | 文档路线 I1 → I2 → I4(B1) 已定义，不返工协议层 | B（仅记录） |

**VRA 现状（供 A2 参考）**：旧 VRA 类与 `src/renderer/` 已退出构建（历史在 `archive/legacy_vulkan/`），
但其目标——所有 vertex/index 合并进一个大 device-local buffer、以 offset 引用——被保留并正规化：
glTF recipe 使用单个 256MB geometry arena（triangle 4MB），primitive 是 arena slice，
indirect draw 以 `vertex_offset/first_index` 引用；分配/同步/生命周期/回收由 RG 行表事务统一管理
（`RenderGraphAndRHI.md:46-54`）。**GPU 侧大 buffer 已存在，加载瓶颈在 CPU 侧**：
合并时 SoA→AoS 回拷 + 逐 mesh 事务。A2 只动 CPU 侧，GPU 侧形态不变。

## A 步骤总览

| 阶段 | 内容 | 落点 | 状态 |
|---|---|---|---|
| **A0** | 测量设施：阶段计时、计数器、样本环、分位数聚合；telemetry 增量字段；加载分段报告 | `src/measure/`（leaf）+ `engine_runtime` + 协议 | ✅ `da42119e` + `7d2d26ec` |
| **A1** | 视锥剔除：相机可选组件 + 管理器；纯函数 frustum/AABB；CSR 两遍法压实；CullingSample | `_interface/culling.h`、`engine/culling_manager.h`、`extract_render_packet`、控制平面 | ✅ `942f9fdc` + `6e012c4e` + `dee0a36e` |
| **A2** | 加载路径：零拷贝 blob span 上传 + 整资产单事务；删 SoA→AoS 回拷 | `render_backend.h` 契约、`geometry_upload_plan.h`、recipe、`merge_asset_database` | ✅ `2953d3bf` + `2b0d23d9` |

前置依赖：A0 先行（A1/A2 的收益度量依赖 A0）；A1 与 A2 互相独立，可并行。

---

## A0 测量设施

### 目标与非目标

- **目标**：以数据驱动 A1/A2/后续优化（benchmark 驱动原则，`InfrastructureDesign.md`）；
  数据同时服务于未来 UI（B2 的图示/历史曲线），因此**协议先行、历史保留**。
- **非目标**：不做火焰图/采样 profiler；不做 GPU 侧 timestamp 查询（归 B3 RG 可视化）；不引入第三方。

### 依赖方向（单向）

```text
src/measure/ (leaf，只依赖 std)
      ^
engine_core / engine_runtime（生产样本）
      ^
control_plane（只消费 JSON，不依赖 measure 类型）
      ^
UI（B2：订阅 telemetry + 后续 metrics.history RPC）
```

渲染侧 recipe 通过 `render_frame_packet` 里的计数器指针回填（engine → recipe 单向，主线程单写者）。

### 数据模型（DoD：SoA 列 + 环 + 存在性）

```cpp
// src/measure/frame_metrics.h —— 叶子头，仅 <array><cstdint><algorithm>
namespace measure
{
    // 阶段槽与 engine_runtime 的固定 phase 表一一对应（编译期契约）
    inline constexpr uint32_t phase_count = 10;
    inline constexpr uint32_t counter_slot_count = 16;

    // 固定容量样本环（列式：同列样本连续，无逐样本堆分配）
    struct metrics_ring
    {
        static constexpr uint32_t capacity = 4096;   // ≈68s @60fps
        uint32_t head = 0;
        uint32_t count = 0;
        std::array<uint64_t, capacity> frame_us{};
        std::array<std::array<uint64_t, capacity>, phase_count> phase_us{};   // 列式
        std::array<std::array<uint64_t, capacity>, counter_slot_count> counters{};
    };

    void push(metrics_ring&, uint64_t frame_us,
              const std::array<uint64_t, phase_count>& phase_us,
              const std::array<uint64_t, counter_slot_count>& counters);

    struct quantiles { double p50 = 0.0; double p95 = 0.0; double p99 = 0.0; };
    [[nodiscard]] quantiles summarize(const metrics_ring&, uint32_t column); // 0=frame, 1..10=phase, 11..=counter
}
```

> 摆放注意：`metrics_ring` 约 885KB（4096 × (8 + 10×8 + 16×8) 字节），只能作为
> `engine_runtime` 的堆上成员（或堆分配持有），禁止栈上实例化。

引擎侧计数槽（`engine_runtime` 内私有映射，名字进 telemetry JSON）：

```text
slot 0  instance_rows    slot 1  visible_rows     slot 2  culled_rows
slot 3  draw_commands    slot 4  buffer_upload_rows  slot 5  image_upload_rows
```

### 引擎侧计数回填（单向）

`engine/render_backend.h` 增量（向后兼容，可空）：

```cpp
struct frame_counters
{
    std::uint64_t instance_rows = 0;   // engine：extract 填
    std::uint64_t visible_rows = 0;    // engine：剔除后填（A1）
    std::uint64_t culled_rows = 0;     // engine：剔除填（A1）
    std::uint64_t draw_commands = 0;   // recipe：build_frame 填
    std::uint64_t buffer_upload_rows = 0;
    std::uint64_t image_upload_rows = 0;
};
// render_frame_packet 增加：
//   frame_counters* counters = nullptr;   // 可空；recipe 只写自己的字段
```

### 加载分段报告（值类型，worker 合规）

```cpp
// engine/render_backend.h
struct load_report
{
    std::string path;
    std::uint64_t load_us = 0;   // worker：dcl::load_gltf 全程（解析+转换+解码）
    std::uint64_t merge_us = 0;  // 主线程：merge_asset_database
    std::uint64_t upload_us = 0; // 主线程：apply_resource_changes
    std::uint64_t vertex_bytes = 0;
    std::uint64_t index_bytes = 0;
    std::uint32_t image_count = 0;
    // 可选细分：仅当 DCL 提供计时装点后填写，否则保持 0
    std::uint64_t parse_us = 0;  // worker：tinygltf 解析
    std::uint64_t convert_us = 0; // worker：DCL 行表转换
    std::uint64_t decode_us = 0; // worker：图片解码
};
```

worker 只填 load_us（值类型，符合"worker 只写私有结果"横切准则）；
主线程在 merge 边界补 merge/upload。每资产完成发一次 `telemetry.load` 通知。

> **口径取舍**：tinygltf 在 `LoadASCIIFromFile` 内部即完成图片解码，`dcl::load_gltf`
> 对 worker 是单次黑盒调用——parse/convert/decode 三段不切 DCL 无法分别测量。
> A0 默认 **DCL 零改动**、只报 `load_us` 总量；三段细分依赖 DCL 侧可选计时装点
> （如自定义 image loader 回调），列为后续可选项，不阻塞 A0 验收。

### 协议扩展（增量，不破坏现有字段）

| 通知 | 新增字段 | 语义 |
|---|---|---|
| `telemetry.frame` | `phase_us: {poll_events, consume_control_commands, ..., publish_telemetry}` | 各阶段近 10Hz 均值（µs） |
| `telemetry.frame` | `quantiles: {frame_p50_ms, frame_p95_ms, frame_p99_ms}` | 环上帧时分位数 |
| `telemetry.frame` | `counters: {instances, visible, culled, draws, buffer_uploads, image_uploads}` | 帧计数 |
| `telemetry.load`（新） | `{path, load_us, merge_us, upload_us, vertex_bytes, index_bytes, images, geometry_arena: {count, created, reserved_bytes, used_bytes, allocation_us, plan_us, transfer_us}}`（+ 可选 `parse_us/convert_us/decode_us`） | 每次加载完成；arena 大小为池快照，耗时为本次事务 |

> 新增通知/方法须同步登记 `control_plane/json_rpc.cpp` 的方法清单（A0 为 `telemetry.load`；
> A1 为 `camera.set_culling` / `camera.get_state` 扩展），否则客户端无法发现。

### A0 验收

1. `cglab.measure_*` CTest：push/summarize 正确性（含空环、环翻转、分位数边界）；
2. Debug/Release 下 `telemetry.frame` 含全部新增字段，数值与手测一致（smoke 契约观察点）；
3. 稳态帧开销：环 push + 聚合 < 10µs（无逐帧堆分配，benchmark 记录）。

---

## A1 视锥剔除（相机可选组件）

### 设计原则（按用户约束）

1. **剔除是相机的可选组件**：创建相机时可以不带（无组件行 = 无剔除能力，直通），也可以带；
   运行时可用控制平面挂载/摘除/开关。
2. **剔除在应用层完成**：作用于 engine 的 `render_frame_packet` 输入侧（instance 集合），
   **recipe 与 RG 零改动**——剔除后 packet 的 `instance_rows` 变短，语义不变（transform 索引保留）。
3. **与 glTF 解耦**：资产侧只提供"mesh 级 bounds + instance/transform 行"接口（现状已满足），
   剔除系统不感知 glTF/PBR/recipe。
4. **独立 sample**：新增 `CullingSample`（复用 gltf recipe + sponza 资产），演示带/不带组件的差异，
   并可通过控制平面切换。
5. **DoD**：SoA 列、存在性代替布尔、CSR 两遍法（计数 → 前缀和 → 散布）、纯函数、帧边界副作用。

### 组件与管理器（Component + Manager，SoA）

```cpp
// src/_interface/culling.h —— 纯函数层：无引擎类型依赖（仅 glm/array/span）
namespace interface::culling
{
    // 世界空间 6 平面（外向法线，Gribb-Hartmann 提取）
    struct frustum { std::array<glm::vec4, 6> planes{}; };

    [[nodiscard]] frustum make_frustum(const glm::mat4& view_projection);
    [[nodiscard]] bool test_aabb(const frustum&, glm::vec3 min, glm::vec3 max);
}
```

```cpp
// src/engine/culling_manager.h —— 组件 + 管理器（行按相机索引对齐）
//
// 无独立 component 结构体：manager 的列即唯一真相——present 列表达"能力存在"
// （无行 = 无剔除能力，存在性代替布尔），enabled 列表达运行时开关。
namespace engine
{
    struct culling_manager
    {
        std::vector<uint8_t> present;              // 存在列（0/1）
        std::vector<uint8_t> enabled;              // 开关列
        std::vector<interface::culling::frustum> frustums;  // 每帧缓存（相机 dirty 时重算）
        // 单写者 scratch：帧边界复用，稳态零分配
        std::vector<engine::instance_row> visible_scratch;
        std::vector<uint8_t> flags;                // 可见性掩码列（第一遍产物）
        // 上帧统计（telemetry 消费）
        std::uint64_t last_visible = 0;
        std::uint64_t last_culled = 0;
    };

    // 组件生命周期（能力挂载/摘除；无行 = 无能力）
    bool attach_culling(culling_manager&, std::size_t camera_index, bool enabled);
    void detach_culling(culling_manager&, std::size_t camera_index);
    void set_culling_enabled(culling_manager&, std::size_t camera_index, bool enabled);

    // 每帧剔除 pass（CSR 两遍法，零分配）：
    //   1) 逐 instance 计算世界 AABB（mesh bounds × transform，复用 scene::transform_bounds 思路）
    //      写入 flags 列；2) 前缀和 → 3) 散布压实进 visible_scratch。
    [[nodiscard]] std::span<const engine::instance_row> cull_instances(
        culling_manager&, std::size_t camera_index,
        const glm::mat4& view_projection,
        std::span<const engine::instance_row> instances,
        std::span<const glm::mat4> transforms,
        std::span<const glm::vec3> mesh_bounds_min,      // 按 geometry_handle 索引（A1 新增引擎表）
        std::span<const glm::vec3> mesh_bounds_max,
        std::uint64_t* out_culled = nullptr);
}
```

**网格 bounds 表**：`engine_runtime` 在 `merge_asset_database` 时维护
`mesh_bounds_min/max`（按返回的 `geometry_handle` 索引，来自 dcl mesh 行既有 bounds）——
这是资产侧提供的唯一新接口，glTF 无感。

### 引擎接线（副作用集中在帧边界出口）

`extract_render_packet` 阶段内（不新增 phase，phase 表契约不变）：

```text
构造 render_frame_packet（现状不变）
  --> 若 present[camera] && enabled[camera]：
        frustum = make_frustum(projection * view)   // GLM 约定：projection * view
        packet.instance_rows = cull_instances(...)
        计数写 frame_counters（visible/culled）
  --> 否则：直通（instance_rows 原样）
```

剔除与场景显隐（`scene_registry` visible）互不干扰：显隐在 extract 之前过滤，剔除在其后。

### 控制平面与 Sample

- 新方法 `camera.set_culling {enabled: bool}`（不存在组件时自动挂载）；
  `camera.get_state` 增加 `culling: bool`（两者均须登记 `json_rpc.cpp` 方法清单，见 A0 协议注记）。
- 新 Sample：`CullingSample`（`src/apps/culling_sample.cpp`，复用 `application_runner` + gltf recipe +
  sponza 资产）；启动相机**带**剔除组件；dev console 可切换并观察
  `counters.visible/culled/draws` 与 fps 变化。TriangleSample/GltfSponzaSample 默认不带（行为不变），
  可选 CLI `--culling`。

### 与 glTF/RG 的解耦边界（契约检查）

- `_interface/culling.h`：零引擎依赖（单测友好）；
- `engine/culling_manager.h`：只依赖 engine 行类型 + scene AABB 工具，不出现 recipe/RG 类型；
- recipe、RG backend、DCL **零改动**（A1 完全不触碰渲染侧）；
- 粒度：A1 = instance 级（每 node/mesh 一个 AABB 测试）；primitive/draw 级与 GPU-driven 剔除归 C2。

### A1 验收

1. `cglab.culling_*` CTest：make_frustum 平面正确性（视锥内/外/跨越点）、AABB 保守性
   （box 与平面相切必须判可见）、压实结果与朴素扫描一致、无组件相机直通语义；
2. Sponza 场景（带组件）：`telemetry.frame.counters.culled > 0` 且 `draws` 显著下降
   （在场景内部视角下记录前后 draw 数对比数据）；
3. 开关切换不改变帧语义（剔除开/关画面一致，仅 draw 数变化）；GPU smoke 保持绿；
4. 稳态帧：剔除 pass 无堆分配（benchmark 记录）。

---

## A2 加载路径优化

### 现状与目标

GPU 侧大 buffer + offset 已存在（见 §0 VRA 现状）。瓶颈在 CPU 侧：

1. `merge_asset_database` 把共享 `vertex_blob/index_blob` 逐 primitive **回拷**为
   `geometry_asset`（SoA→AoS 回退点，`Architecture.md` 还债项）——大场景数百 MB 数据复制 + 数千次小 vector 分配；
2. **每 mesh 一次** `apply_resource_changes` 事务——Sponza 数百次独立
   validate → staging memcpy → publish 循环。

A2 = 零拷贝上传 + 整资产单事务。GPU 侧、RG 后端、DCL 零改动。

### 契约变更（`engine/render_backend.h`，主仓内一并改消费点）

```cpp
// 旧：geometry_upload_row { const geometry_asset* asset; }          （AoS，删除）
// 新：批量行直接引用共享 blob（dcl 行模型，engine 已 using 别名）
struct geometry_upload_row
{
    const asset_database* asset = nullptr;   // 整库批量行
    std::uint32_t first_mesh = 0;
    std::uint32_t mesh_count = 0;            // ≥1，覆盖的 mesh 行数
    std::uint32_t material_base = 0;         // 材质上传返回的 base（recipe 需要）
};
```

- `apply_resource_changes` 返回 `geometry_handles`：按 mesh 顺序每 mesh 一个句柄（语义不变）；
- 一资产 = **一次**事务（geometry + materials + images 各自成批，事务数从 O(meshes) 降到 O(1)）；
- `geometry_asset` AoS 从合并路径删除（仅保留 pick/显隐所需的 bounds 与注册信息）。

旧行删除的牵连点（主仓内一并迁移，不留 legacy 双契约）：

| 牵连点 | 处置 |
|---|---|
| `triangle_render_recipe.cpp`（消费旧行类型） | 改用新批量行 |
| `triangle_sample.cpp`（`make_triangle()` 产出 `geometry_asset`） | 改为产出单 mesh 的 `asset_database` |
| `sample_definition::startup_geometry` / `set_initial_geometry`（`engine_runtime` initial_geometry 路径） | 参数类型 `geometry_asset` → `asset_database` |
| `engine_runtime_test`（fake backend + `set_initial_geometry(triangle())` 用例） | 跟随新行类型更新 |

### recipe `apply_changes` 重写（零拷贝）

```text
for row in batch.geometry_uploads:
    for mesh in [first_mesh, first_mesh + mesh_count):
        for primitive in mesh 行段:
            upload 行 = { state.geometry, vertex_offset(按 sizeof(vertex) 对齐后),
                          as_bytes(std::span(asset->vertex_blob)
                              .subspan(primitive.vertex_offset, primitive.vertex_count)) }  // 元素语义
                        + index 行同理（index_blob.subspan(primitive.index_offset, primitive.index_count)）
        geometry_handle = 一次 apply 后按序返回
```

上传行引用的 span 在 `apply_resource_changes` 同步调用期间有效（事务同步执行），
blob 生命周期由 merge 持有到调用结束——无悬垂。arena 游标/容量检查逻辑不变。

### `merge_asset_database` 重写

```text
material upload（现状，一次事务）
  --> 构造单条 geometry_upload_row{asset, 0, asset.meshes.size(), material_base}
  --> 一次 apply_resource_changes（全部 mesh 的全部 primitive 行）
  --> 按序收 geometry_handles → mesh_handles[]
  --> mesh_bounds 表维护（A1 消费）+ 节点 world 解析 + scene 注册（现状不变）
```

单事务后失败语义简化为"整资产全成或全败"：merge 内现有的逐 mesh 失败回滚循环
（逐枚已上传句柄 retire）整段删除，不再需要。

启动资产（`read_only`，启动即载）走同一路径，同样受益。

### 可选 A2.3（标记为 stretch，不阻塞验收）

**worker 侧 upload 规划**：offset 计算 + 行清单（值类型 `upload_plan`，SoA：offsets/sizes/源区间）
移到 worker；主线程只做"计划 → 行 → apply"。收益：merge 阶段进一步缩短；
约束：worker 只产值类型（架构横切准则），RG 句柄仍由主线程持有。若 A2 数据已达标可暂缓。

### A2 验收

1. `cglab.loading_*` CTest：批量行降级正确性（句柄数量与顺序、offset 对齐、越界报错、容量耗尽回滚）；
2. 主仓 GPU smoke（Triangle/GltfSponza 各 6 帧）保持绿，画面与基准一致；
3. 用 A0 的 `telemetry.load` 记录 Sponza：`merge_us`、`upload_us`、总时长前后对比
   （目标：merge_us 下降一个数量级——消除复制与逐 mesh 事务；以数据为准）；
4. 场景注册/拾取/显隐行为不变（复用既有 cglab 单测）。

---

## B 步骤（仅记录，暂不实施）

| # | 内容 | 依据 | 前置 |
|---|---|---|---|
| B1 | I1 协议收尾：方法/telemetry JSON Schema 固化进 `docs/`；控制平面 HTTP 静态托管；`--ui-open-browser` 真正生效 | `InterfaceRedesign.md` §3 I1 | 无（文档写"可直接开始"） |
| B2 | I2 正式 Web UI（dock 布局）；**消费 A0 数据**：帧时曲线、阶段直方图、计数面板、加载瀑布 | `InterfaceRedesign.md` I2 | B1；A0 协议字段 |
| B3 | I3 RG 可视化：`debug_dump()` JSON + React Flow DAG + pass 时序瀑布（GPU timestamp 查询在此引入） | `InterfaceRedesign.md` I3 | B2 |
| B4 | I4 单窗口 B1：webview 壳 + SDL HWND 视口子区嵌入；焦点规则；`--no-ui`/浏览器模式仍可用 | `InterfaceRedesign.md` I4 | B2（依赖链 I4→I2） |
| B5 | 加载后续：DCL 并行化评估（多资产并行解析 → 触发 infra 触发条款评估）；纹理流送评估；present 模式配置化（mailbox 选项，由 A0 数据决定是否值得） | `InfrastructureDesign.md` §2 | A0/A2 数据 |

## C 步骤（仅记录，暂不实施）

| # | 内容 | 触发/依据 |
|---|---|---|
| C1 | infra job system I0–I3（固定线程池 + MPMC + 主线程回调队列；任务图；协程层仅数据证明需要时） | `InfrastructureDesign.md` §2/§3：**触发条件 = 第二个真实并发负载**（多 glTF 并行解析 > shader 编译 > 纹理流送）或 worker 模式被复制粘贴 |
| C2 | primitive/draw 级剔除与 GPU-driven culling（间接剔除 / meshlet） | 由 A1 的 instance 级收益数据决定是否值得 |
| C3 | 场景层级：`parent_index` + dirty transform 批量重算（还债 `update_scene_transforms` 空 phase） | `Architecture.md` 还债清单；InterfaceRedesign §5 |
| C4 | 渲染还债：`backend_capabilities()` 设备实测、executor/runtime 双资源表收敛等 | `Architecture.md` 还债清单 |

## 通用 DoD 检查单（A 步骤验收共查）

- [x] SoA 列优先，无 AoS 热路径；无逐帧/逐元素堆分配（scratch/环固定容量）；
- [x] CSR：剔除压实、样本环、upload 计划均"计数 → 前缀和 → 散布"两遍法；
- [x] 存在性代替布尔（组件行存在 = 能力存在；无 `has_culling` 式状态查询）；
- [x] 组件 + Manager：数据行 + 帧边界系统函数，主线程单写者；
- [x] 纯函数无副作用（frustum/AABB/聚合）；副作用集中在 phase 出口；
- [x] 单向依赖：`measure` ← engine ← runtime ← 协议 → UI；剔除不依赖 recipe/RG/glTF；
- [x] 每步 CTest 绿 + 主仓 GPU smoke 绿 + benchmark 数据记录（不凭感觉）。

---

## 实施记录（as-built，2026-08）

### 落地差异（与设计稿的偏差，均为刻意决策）

1. **`load_report` 口径**：按设计稿只报 `load_us` 总量（worker 黑盒），`parse/convert/decode`
   细分字段保留但恒 0，等 DCL 侧可选计时装点（§A0 口径取舍）。
2. **帧计数回填语义**：`buffer_uploads/image_uploads` 为"本帧 build_frame 内 apply 的
   staging 上传行数 + 加载帧累计行数"（加载在帧边界 apply，故加载帧可见批量行数），
   稳态帧为每帧 3 行（uniform/transform/indirect 表）。
3. **`initial_geometry`（Triangle 启动几何）**：`geometry_asset` AoS 全仓删除后，
   `set_initial_geometry` 参数改为 `asset_database`，与 glTF 加载路径共用 `merge_asset_database`
   单事务路径（Triangle 的启动几何同样受益于批量行契约）。
4. **triangle recipe 顶点对齐**：旧代码按 `alignof(vertex)`（4 字节）对齐，规划器统一为
   顶点步长（72 字节）对齐——与 glTF recipe 旧行为一致，draw 换算不受影响。
5. **`telemetry.load` 仅运行时加载发布**：启动资产（`--asset`）只写日志不推通知
   （启动时无客户端连接，避免无效推送）。

### 已测量数据（本机 Debug，triangle.gltf，A2 后）

```text
TriangleSample    smoke: 启动几何 merge 334us / upload 245us
GltfSponzaSample  smoke: 启动资产 load 2500us / merge 312us / upload 196us（228 bytes）
CullingSample     smoke: 启动资产 load 2993us / merge 195us / upload 154us（228 bytes，剔除开启）
```

> Sponza 场景数据（`telemetry.frame.counters.{visible,culled,draws}`、`telemetry.load` 分段、
> 剔除前后帧时对比）需在有 Sponza 资产的机器上记录，填入本文（§A1/A2 验收）。

### 验收核对

- `cglab.measure` / `cglab.culling` / `cglab.loading_plan` / 既有 11 个 CTest：全绿；
- GPU smoke：TriangleSample / GltfSponzaSample / CullingSample 各 6 帧契约通过（Debug）；
- 新增协议：`telemetry.load`、`camera.set_culling` 已登记 `session.init` capabilities 与
  `control_plane_protocol_test`；`camera.get_state` 新增 `culling` 字段；
- 新 Sample：`CullingSample`（`--asset` 可选，剔除恒开）；`--culling` 可选开关接入
  TriangleSample / GltfSponzaSample（默认关，行为不变）。
