#pragma once

// measure::metrics_ring —— A0 测量设施的叶子数据模型（src/measure/，只依赖 std）。
//
// - 固定容量样本环（默认 4096 帧 ≈ 68s @60fps），列式存储：同列样本连续，
//   无逐样本堆分配，环满后覆盖最旧样本。
// - 列索引约定：0 = frame_us，1..phase_count = 各阶段耗时，之后为计数器槽。
// - push/summarize 均为纯函数；聚合（分位数）在遥测频率（10Hz）下拷贝排序，
//   稳态帧开销仅 push 的一次写列。
//
// 摆放注意：metrics_ring 约 885KB（4096 × (8 + 10×8 + 16×8) 字节），只能作为
// engine_runtime 的堆上成员（或堆分配持有），禁止栈上实例化。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace measure
{
    // 阶段槽与 engine_runtime 的固定 phase 表一一对应（编译期契约）。
    inline constexpr std::uint32_t phase_count = 10;
    inline constexpr std::uint32_t counter_slot_count = 16;
    inline constexpr std::uint32_t ring_capacity = 4096;

    struct metrics_ring
    {
        std::uint32_t head = 0;  // 下一个写入位置（环游标）
        std::uint32_t count = 0; // 已积累样本数（≤ capacity）
        std::array<std::uint64_t, ring_capacity> frame_us{};
        std::array<std::array<std::uint64_t, ring_capacity>, phase_count> phase_us{};
        std::array<std::array<std::uint64_t, ring_capacity>, counter_slot_count> counters{};
    };

    inline void push(metrics_ring& ring,
                     std::uint64_t frame_us,
                     const std::array<std::uint64_t, phase_count>& phase,
                     const std::array<std::uint64_t, counter_slot_count>& counters)
    {
        ring.frame_us[ring.head] = frame_us;
        for (std::uint32_t p = 0; p < phase_count; ++p)
        {
            ring.phase_us[p][ring.head] = phase[p];
        }
        for (std::uint32_t c = 0; c < counter_slot_count; ++c)
        {
            ring.counters[c][ring.head] = counters[c];
        }
        ring.head = (ring.head + 1) % ring_capacity;
        if (ring.count < ring_capacity)
        {
            ++ring.count;
        }
    }

    struct quantiles
    {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    // 对环内某列求分位数（最近秩法：sorted[ceil(p*n)-1]）。
    // column 0 = frame_us，1..phase_count = 阶段，其后为计数器槽；
    // 空环或越界列返回全 0。
    [[nodiscard]] inline quantiles summarize(const metrics_ring& ring, std::uint32_t column)
    {
        quantiles result;
        if (ring.count == 0 || column > phase_count + counter_slot_count)
        {
            return result;
        }

        const std::uint64_t* source = nullptr;
        if (column == 0)
        {
            source = ring.frame_us.data();
        }
        else if (column <= phase_count)
        {
            source = ring.phase_us[column - 1].data();
        }
        else
        {
            source = ring.counters[column - 1 - phase_count].data();
        }

        // 环内样本物理上连续吗？未满时 [0, count) 连续；已满时从 head 起绕回。
        std::array<std::uint64_t, ring_capacity> sorted{};
        if (ring.count < ring_capacity)
        {
            std::copy_n(source, ring.count, sorted.begin());
        }
        else
        {
            std::copy_n(source + ring.head, ring_capacity - ring.head, sorted.begin());
            std::copy_n(source, ring.head, sorted.begin() + (ring_capacity - ring.head));
        }
        std::sort(sorted.begin(), sorted.begin() + ring.count);

        const auto percentile = [&sorted](double p, std::uint32_t n) -> double
        {
            const std::uint32_t index = static_cast<std::uint32_t>(std::ceil(p * static_cast<double>(n))) - 1;
            return static_cast<double>(sorted.data()[index]);
        };
        result.p50 = percentile(0.50, ring.count);
        result.p95 = percentile(0.95, ring.count);
        result.p99 = percentile(0.99, ring.count);
        return result;
    }
} // namespace measure
