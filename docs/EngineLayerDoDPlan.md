# 引擎层 DoD 还债（D 系列）+ 插件式帧通道（F 系列）计划

> 状态：**规划完成，待实施（2026-08-14）**。D 系列 = 引擎层 DoD 化还债（对应
> `Architecture.md` 还债清单）；F 系列 = `render_frame_packet` 插件化（类型键控帧通道）。
> 全程 **render-graph 子仓零改动**；沿用 A0–A2/P0–P7 推进纪律：每阶段独立提交、全部单测绿才进下一段。
>
> 依据：`Architecture.md`（架构与还债清单、行表命名约定）、`PerformancePlan.md`（A0 测量设施）、
> RG 子仓 `docs/计划.md`（P0–P7 DoD 重构，本计划的风格参照）。
>
> 术语表（拟并入 `Architecture.md` 行表命名约定一节）：
> - **component（组件）**：挂在实体上的那份数据的类型（光源组件、probe 组件、相机组件）。与 EnTT/flecs/Bevy 的 component 语义一致。
> - **component table（组件表）**：某类组件的全量 SoA 表，即 `*_table`（未来 `lights_table`、`probe_table`）。packet 传递的从来不是单个 component，而是整张表。
> - **frame channel（帧通道）**：packet 内的一次发布槽位——类型键控的 `{id, 行指针, 行数}` 行。命名避开 Bevy `Resource`（与 GPU 资源撞名）与 EnTT `ctx`（与 `frame_phase_context` 撞名）。
> - **state（状态通道）**：非实体的帧级单例数据（view-projection、时间、配置），复用通道机制，不单独造词。

## 0. 背景与现状盘点

引擎层 2026-08 的三步（A0 测量 / A1 剔除 / A2 加载路径）是认真 DoD 的新代码
（`measure::metrics_ring` 列式环、`culling_manager` 两遍法 + 存在性列、`render_frame_packet` 行表输入），
但 P2 时期遗留的老代码仍是 AoS/上帝对象，且**没有机械契约**把风格变成构建失败（子仓有 `core_layout`，
主仓只有 `ArchitectureContract.cmake` 管 Vulkan 边界，不管 DoD 风格）。评审结论：

| # | 问题 | 位置 | 归属 |
|---|---|---|---|
| 1 | `scene_registry` 胖 AoS 行（`std::string`+`std::vector`+3 bool）+ `alive` 为 `std::vector<bool>` + `find`/`unload` O(n) 线性扫描 + `objects()` 每帧新建指针 vector | `src/scene/scene_registry.h` | D1 |
| 2 | `extract_render_packet` 每帧走 `objects()` 指针追逐 + 逐对象 `model_matrix`（glm 旋转链）；`update_scene_transforms` 空 phase 名存实亡 | `engine_runtime.cpp` | D2 |
| 3 | `poll_events` 两处提前副作用（resize 直接 `request_resize`、拾取命中即时发布遥测），违反"副作用集中在帧边界出口" | `engine_runtime.cpp` | D2 |
| 4 | `frame_phase_context` 4 bool 传控制流；`engine_runtime` 为约 40 个可变成员的上帝对象，阶段纯度无强制 | `engine_runtime.h` | D3 |
| 5 | `render_frame_packet` 固定五类 span 结构体——加一类数据就要改公共头 + engine + 全部 recipe 重编译；非插件式 | `engine/render_backend.h` | F 系列 |
| 6 | `transform_bounds` 8 角点三元分支选角点，阻碍自动向量化 | `scene/scene_registry.h` | D2 |

## 1. 插件式帧通道设计（F 系列方案）

### 1.1 职责边界
- **引擎保证秩序**：帧首清零、发布窗口 = 阶段表顺序、每通道单写者、数据生存期 = 单帧。
- **编写者保证语义**：recipe 找不到通道（空 span / nullptr）或内容不对，引擎不校验——这是编写者责任。

### 1.2 机制（纯 std，叶子里面的叶子）
```cpp
// engine/frame_channels.h
struct frame_channels {
    struct channel_row { std::uint32_t id; const void* data; std::uint32_t count; };
    std::vector<channel_row> entries;   // 帧首 clear，reserve 后稳态零分配
    template <typename T> static std::uint32_t channel_id();  // 静态原子计数器，不依赖 RTTI
    template <typename T> void publish_rows(std::span<const T>);   // 行表通道（span + count）
    template <typename T> void publish_state(const T*);            // 单例/SoA 表通道（count=1）
    template <typename T> std::span<const T> find_rows() const;    // 缺失 → 空 span
    template <typename T> const T* find_state() const;             // 缺失 → nullptr
};
```
- 静态局部原子计数器生成 id（全二进制唯一；主仓单可执行成立，DLL 化时需评审）。
- 查找为小表线性扫（通道数 ~10，纳秒级）。

### 1.3 packet 迁移形态
- `render_frame_packet` 瘦身为 `{ frame_serial, const frame_channels*, frame_counters* }`。
- 现有五类 span（camera/instance/transform/material/mesh）→ extract 发布的**前五个内置通道**。
- `render_driver_api` 函数表签名不变（主仓内部），两个 recipe 与 mock driver 改 `find_rows<T>()` 消费。

## D 系列：引擎层 DoD 还债

### D0 — 契约与基线
- 扩 `cmake/ArchitectureContract.cmake`（构建时检查，违反即 FATAL_ERROR）：
  - `src/engine`、`src/scene` 禁 `std::vector<bool>`（位打包伪容器反模式）；
  - 禁 `std::vector<std::vector`（嵌套 vector 指针追逐）。
- 新增 extract 微基准（仿子仓 `compile_benchmark`）：合成 N=4096 对象，统计
  `objects() → model_matrix → render_instances` 全链耗时，基线记入 §5 表。

### D1 — scene_registry 拆列 + id→slot 索引
- `scene_object` AoS → SoA 列：`names`（冷）、`transforms`、`matrices`、`bounds_min/max`、
  `flags`（uint8 位打包 visible/read_only/use_matrix，消灭 3 bool）、`geometries`；
  draws 改 CSR（`draw_begins/draw_counts` + 扁平 `draw_ranges`）；`alive` → `uint8_t` 列。
- id→slot 直接寻址数组（id 单调不复用，slot 回收），`find/unload` O(n) → O(1)；
  维护紧凑 `active_slots` 索引列（仅注册/卸载时维护），消灭 `objects()` 每帧指针 vector 分配。
- 公共 API 签名不变（测试锁定 10 个方法：register/find/objects/pick/revision/selected/
  set_selected/set_transform/set_visibility/unload）。
- 验收：`scene_registry_test` 绿 + 契约绿 + D0 基准复测。

### D2 — extract 列式重写 + 空 phase 做实 + 副作用归并
- `update_scene_transforms` 做实：`set_transform/register` 置脏（uint8 脏列），该 phase 批量重算
  `matrices` 列（均匀循环无逐元素 guard）；extract 不再逐对象 `model_matrix`，直读列。
- `extract_render_packet` 沿 `active_slots` 走列 → `render_instances/render_transforms`
  （reserve 后稳态零分配）。
- `poll_events` 两处早退副作用改请求行：`resize_requests`（计数）、`pick_requests`（x,y 行），
  分别归并到 submit 前边界与 `publish_telemetry` 出口。
- `transform_bounds` 8 角点三元分支 → 闭式解（`abs(M)·half_extent`，标准 AABB 变换技巧），
  culling 第一遍同享收益。
- 验收：全部 cglab 测试绿 + **GPU smoke 复验**（pick 移帧尾是可见行为变化：命中高亮晚一帧）+ 基准记录。

### D3 — context 收敛（轻量）
- `frame_phase_context` 4 bool → `stop_reason` 枚举 + `rendered`；`render_this_frame` 降为
  submit 局部变量。
- `engine_runtime` 成员按职责分组（异步加载簇/遥测簇/提取簇）成子 struct——组织性改动，无行为变化。
- **明确不做**：完整阶段纯化（阶段间 in/out 表化），成本高收益存疑，写入不做清单。

## F 系列：插件式帧通道

### F1 — frame_channels 核心
- 上述机制 + 单测 `cglab.frame_channels`：发布/查找/缺失/重复发布 assert/多通道顺序/帧首清零。
- 文档：`Architecture.md` 新增「帧通道」节 + 术语表并入命名约定一节。

### F2 — packet 迁移
- `render_frame_packet` 瘦身；extract 发布五内置通道；两个 recipe + `engine_runtime_test` mock
  改 `find_rows<T>()` 消费；`render_driver_api` 签名不变。
- 验收：全部测试绿 + GPU smoke 复验；grep 确认 packet 直字段零残留。

### F3 — 端到端插件验证：光源表 demo
- `lights_table`（SoA：positions/colors/intensities）由 sample 系统发布、gltf recipe 在
  `build_frame` 上传进每帧 storage buffer、shader 消费——**engine 全程零改动**。
- 这是插件故事的最小可运行证明；GI 是它的第一个真实消费者（届时另立计划）。

## 推进纪律
- 每阶段：实现 → 构建（vcvars64 环境）→ ctest 37/37 全绿 → 主仓独立提交（`[refactor]`/`[test]`/`[chore]`）
  → 更新本笔记（✅ + commit）。
- GPU smoke（Triangle/GltfSponza 各 6 帧）在 D2/F2 两个行为变化点复验。

## 不做（本轮范围外）
- render-graph 子仓任何改动。
- GI sample 本体（F3 仅用 lights 验证机制）。
- DI 风格统一（`render_driver` opaque+function table vs `asset_service`/`window` 虚接口，还债清单第 3 条）。
- 多相机 / shadow view 的通道语义（机制使之可能，不代为提供）。
- 完整阶段纯化（阶段间 in/out 表化）。

## 微基准表（extract 全链，Debug，N=4096 对象）

| 阶段 | best (µs) | median (µs) | 相对基线 |
|------|-----------|-------------|---------|
| D0 基线 | 11975 | 12711 | — |
| D1 拆列后 | 11749 | 13397 | ≈持平（extract 仍走对象路径，提速在 D2） |
| D2 列式重写后 | 265 | 271 | **-97.8%**（列直读 + 静态场景零矩阵重算） |
| D3 context 收敛 | — | — | 无行为变化 |

## 阶段与提交记录

| 阶段 | 内容 | 提交 |
|------|------|------|
| 规划 | 本笔记 | — |
| D0 | 契约 + 基线（✅ 2026-08-14：`ArchitectureContract.cmake` 增 DoD 检查，`scene_registry` `vector<bool>`→`uint8_t`，新增 `cglab.extract_benchmark`；41/41 绿，基线 11975/12711µs） | `7e8e4844` |
| D1 | scene_registry 索引化（✅ 2026-08-14：id→slot 直接寻址 O(1) 查找、active_slots 紧凑索引、draws 扁平列 + draw_begin/count 切片、scene_object 保留为冷路径记录；公共 API 不变；41/41 绿；基准 ≈持平） | `ad20b09d` |
| D2 | extract 列式化（✅ 2026-08-14：热列 visible/matrices/dirty/geometries + refresh_matrices 增量重算、update_scene_transforms 做实、poll_events resize/拾取副作用归并到帧边界、transform_bounds 闭式解；41/41 绿 + Triangle/GltfSponza GPU smoke 6 帧通过；基准 265/271µs，**-97.8%**。注：pick 反馈延后到 telemetry 出口，命中高亮晚一帧——还债清单钦点方向） | `ef595a96` |
| D3 | context 收敛 + 成员分簇（✅ 2026-08-14：frame_phase_context 4 bool → frame_stop_reason 枚举 + rendered（stop 语义互斥）；render_this_frame 降为 submit 局部变量；成员按职责分簇 frame_loads/frame_extract/frame_telemetry（组织性，无行为变化）；41/41 绿） | `c66e1d3a` |
| F1 | frame_channels 核心（✅ 2026-08-14：`engine/frame_channels.h` 类型键控通道（静态原子 id + publish_rows/publish_state/find_rows/find_state + 帧首 clear + 每通道单写者）；`cglab.frame_channels` 单测 7 项；`Architecture.md` 术语表并入命名约定 + 新增「帧通道」节 + 还债清单勾销 D1/D2 三条；42/42 绿） | — |
| F2 | packet 瘦身迁移（✅ 2026-08-14：`render_frame_packet` 瘦身为 {frame_serial, channels, counters}；extract 发布 camera/instance/transform 三内置通道（原 material_handles/mesh_handles 为死字段一并移除）；两个 recipe + mock driver 改 `find_rows<T>()` 消费；`render_driver_api` 签名不变；42/42 绿 + Triangle/GltfSponza GPU smoke 6 帧通过；grep 验收直字段零残留） | — |
| D1 | scene_registry 拆列 + id→slot | — |
| D2 | extract 列式 + phase 做实 + 归并 | — |
| D3 | context 收敛 | — |
| F1 | frame_channels 核心 | — |
| F2 | packet 迁移 | — |
| F3 | lights_table 插件验证 | — |
