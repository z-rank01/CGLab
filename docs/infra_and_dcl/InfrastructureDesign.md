# 基础设施层设计：Job System、异步框架与 Benchmark

> 地位：`Architecture.md` 四层模型中的**基础设施层**（最下层）。
> 与渲染/引擎完全解耦，未来可抽为独立仓库/submodule（与 render-graph、DCL 同级）。
> 本文与 `InterfaceRedesign.md` 分离：UI/交互不关心本层实现，只通过业务层间接受益。
>
> **2026-08-16 起待办排序以 [Plan.md](../Plan.md) 为准**（I0–I3 为触发式后置，见 Plan.md §3 C1）。
> **2026-08-18：C1 触发成立（dcl 纹理并行解码 = 第二个真实并发负载），本文自
> `docs/vulkan_sample_dod_refaction/` 迁出，成为 `feature/infra-and-dcl` 的生效设计稿。**

## 1. 目标与非目标

**目标**：
- 为业务层提供"提交任务 → 拿结果"的最小并发能力。
  第一个真实用户已存在：glTF 异步解析（P2 loader worker）。
  第二个候选用户：shader 编译、多 glTF 并行解析、纹理流送、RG 编译。
- **benchmark 驱动**：每个阶段必须用数据证明收益，不许凭感觉引入复杂度。
- 零第三方依赖、可独立 CTest、可整体搬出本仓库。

**非目标（明确不做，防止膨胀）**：
- fiber / 有栈协程调度器
- GPU 任务调度、驱动级并发
- 网络/磁盘异步 IO 框架
- 通用 reactive / event-bus 框架
- 跨进程任务分发

## 2. 反 yak-shaving 条款（启动条件）

**不在只有一个并发负载时抽象。** 当前 P2 loader worker（专用线程 + 双队列 + CV）已经够用，保持原样。

触发 I1 的条件（满足其一）：
1. 出现第二个真实并发负载（候选按概率：多 glTF 并行解析 > shader 编译 > 纹理流送）；
2. loader worker 模式在第二处代码被复制粘贴（框架层抽取时若发生，即触发）。

## 3. 分阶段路线

| 阶段 | 内容 | 验收 |
|---|---|---|
| **I0 benchmark 设施** | 微基准 harness（计时/分位数/线程扩展曲线），纳入 CTest 独立 target | 能跑出"空任务 std::thread-per-task vs 队列"的基线数据 |
| **I1 最小 job system** | 固定线程池（`hardware_concurrency - 1`）；MPMC 有界队列 + CV；task = move-only `function<void()>`；future 取结果；**主线程回调队列**（帧边界合并，对接 Architecture 横切准则） | 迁移 1 个真实负载（loader）；benchmark 达标（§5）；异常经 future 传递不吞 |
| **I2 任务图** | fork-join 依赖：`wait_all` / `then` 延续，原子计数器实现；仍无协程 | 用一个真实多阶段负载验收（如：并行解析 → 合并 → 主线程 staging） |
| **I3 async-await 层（仅当数据证明需要）** | C++20 coroutine + scheduler awaitable，包在 I1/I2 之上；业务层接口不变 | I2 的 benchmark 显示续体开销为瓶颈，且 I3 能消掉它 |

每个阶段的附加验收：现有全部 CTest / smoke / 端到端验证保持绿。

**明确禁止的跳级**：不许直接写 I3（协程优先是过度设计）；不许在 I1 引入 work-stealing 复杂队列（先用 MPMC + CV，stealing 等 benchmark 指出热点再换）。

## 4. 设计约束（与 Architecture 横切准则对齐）

- **DoD**：任务集中存储、批量派发；worker 热路径避免逐任务堆分配（任务池/块式分配后置优化，先正确）。
- **所有权规则**：任务只写自己的私有结果区；component 数组永远是主线程单写者；结果经主线程回调队列在**帧边界合并**（P2 loader 即此模式的原型）。
- **错误处理**：任务异常捕获进 future/result，调用方显式处理；worker 不允许因任务异常退出。
- **惰性 + 副作用最后**：任务的"纯计算"与"结果应用"分离——job 产出值，主线程决定何时生效。
- **API 形态**：头文件/静态库，命名空间 `infra`；不出现任何 Vulkan/SDL/引擎类型。

## 5. Benchmark 设计

**微基准**（I0 建立，每阶段复跑）：
- 空任务吞吐（tasks/s）、提交→完成延迟分布（p50/p99）；
- 线程数扩展曲线（1/2/4/N workers）；
- 对照组：`std::thread`-per-task、串行执行。

**集成基准**（I1 验收用真实负载）：
- 并行解析 N 个 glTF vs 串行解析的总耗时比；
- 引擎帧时间影响：加载期间主线程帧耗时的 p99 不应劣化（对照无加载基线）。

**参考门槛**（4 核以上机器，未达标则该阶段不算完成）：
- 8 个 CPU 密集任务并行总耗时 ≤ 串行的 40%；
- 空任务平均开销 < 10 µs；
- 加载期间主线程 p99 帧耗时劣化 < 5%。

**I0 基线（2026-08-18，`f3590553`，Debug，hardware_concurrency=20）**：
serial 0.0015µs/task；thread-per-task 165µs/task（p50 62.5 / p99 374.7）；
有界队列吞吐 2.07M→3.57M→1.30M→0.69M tasks/s（1/2/4/19 workers）——
thread-per-task 与队列差两个数量级；空任务下队列吞吐拐点在 2 workers
（单 mutex 竞争主导）。I1 复跑同表对照。

## 6. 目录与迁移

- 落地位置：`src/infra/`（新），CMake 独立 target `infra` + `cglab.infra_*` CTest。
- 稳定后（I2 完成且有两个以上真实用户）抽为独立子模块仓库，与 render-graph / DCL 同级。
- loader 迁移路径：当前 `cglab_asset_runtime` 的专用 worker → I1 job system 的一个真实负载，request ID/result 行为与控制平面协议保持不变。
