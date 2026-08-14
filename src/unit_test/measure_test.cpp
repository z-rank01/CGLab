// measure::metrics_ring 单元测试（A0）
// 覆盖：push/summarize 正确性、空环、环翻转、分位数边界、阶段/计数器列隔离。

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "measure/frame_metrics.h"

namespace
{
    int failures = 0;

    void check(bool condition, const char* name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    void check_close(double actual, double expected, const char* name)
    {
        if (std::abs(actual - expected) > 1e-9)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << ": expected " << expected << ", got " << actual << '\n';
        }
    }

    std::array<std::uint64_t, measure::phase_count> zero_phase()
    {
        return {};
    }

    std::array<std::uint64_t, measure::counter_slot_count> zero_counters()
    {
        return {};
    }

    void test_empty_ring()
    {
        const measure::metrics_ring ring;
        const auto q = measure::summarize(ring, 0);
        check(q.p50 == 0.0 && q.p95 == 0.0 && q.p99 == 0.0, "empty ring summarize is zero");
    }

    void test_single_sample()
    {
        measure::metrics_ring ring;
        auto phase = zero_phase();
        phase[0] = 123;
        auto counters = zero_counters();
        counters[3] = 7;
        measure::push(ring, 1000, phase, counters);
        check(ring.count == 1, "single sample count");
        check(ring.head == 1, "single sample head");
        const auto q = measure::summarize(ring, 0);
        check_close(q.p50, 1000.0, "single sample frame p50");
        check_close(q.p99, 1000.0, "single sample frame p99");
        const auto phase_q = measure::summarize(ring, 1);
        check_close(phase_q.p50, 123.0, "single sample phase p50");
        const auto counter_q = measure::summarize(ring, 1 + measure::phase_count + 3);
        check_close(counter_q.p50, 7.0, "single sample counter p50");
    }

    void test_percentile_known_values()
    {
        measure::metrics_ring ring;
        auto phase = zero_phase();
        auto counters = zero_counters();
        for (std::uint64_t value = 0; value < 100; ++value)
        {
            measure::push(ring, value, phase, counters);
        }
        check(ring.count == 100, "hundred samples count");
        const auto q = measure::summarize(ring, 0);
        check_close(q.p50, 49.0, "nearest-rank p50 of 0..99");
        check_close(q.p95, 94.0, "nearest-rank p95 of 0..99");
        check_close(q.p99, 98.0, "nearest-rank p99 of 0..99");
    }

    void test_ring_wrap_around()
    {
        measure::metrics_ring ring;
        auto phase = zero_phase();
        auto counters = zero_counters();
        const std::uint32_t total = measure::ring_capacity + 10;
        for (std::uint32_t sample = 0; sample < total; ++sample)
        {
            measure::push(ring, sample, phase, counters);
        }
        check(ring.count == measure::ring_capacity, "full ring count");
        check(ring.head == 10, "full ring head after wrap");
        // 环内只保留最后 capacity 个样本：最新样本写在 head-1（模容量）处
        const std::uint32_t newest = (ring.head + measure::ring_capacity - 1) % measure::ring_capacity;
        check(ring.frame_us[newest] == total - 1, "ring keeps newest sample");
        // 最旧样本（0..9）被逐出：summarize 只统计保留窗 10..4105
        const auto q = measure::summarize(ring, 0);
        check(q.p99 > 4000.0, "ring wrap evicts oldest samples");
        check(q.p50 > 2048.0, "ring wrap median stays in retained window");
    }

    void test_column_isolation()
    {
        measure::metrics_ring ring;
        auto phase = zero_phase();
        phase[9] = 555;
        auto counters = zero_counters();
        counters[0] = 42;
        measure::push(ring, 111, phase, counters);
        const auto frame_q = measure::summarize(ring, 0);
        check_close(frame_q.p50, 111.0, "frame column isolated");
        const auto last_phase_q = measure::summarize(ring, measure::phase_count);
        check_close(last_phase_q.p50, 555.0, "phase column isolated");
        const auto first_counter_q = measure::summarize(ring, 1 + measure::phase_count);
        check_close(first_counter_q.p50, 42.0, "counter column isolated");
        // 越界列按空处理
        const auto out_of_range = measure::summarize(ring, 1 + measure::phase_count + measure::counter_slot_count + 1);
        check(out_of_range.p50 == 0.0, "out-of-range column is zero");
    }

    void test_push_keeps_phase_columns()
    {
        measure::metrics_ring ring;
        auto phase = zero_phase();
        auto counters = zero_counters();
        for (std::uint32_t p = 0; p < measure::phase_count; ++p)
        {
            phase[p] = p * 10;
        }
        measure::push(ring, 100, phase, counters);
        for (std::uint32_t p = 0; p < measure::phase_count; ++p)
        {
            const auto q = measure::summarize(ring, 1 + p);
            check_close(q.p50, static_cast<double>(p * 10), "phase column p stored");
        }
    }
} // namespace

int main()
{
    test_empty_ring();
    test_single_sample();
    test_percentile_known_values();
    test_ring_wrap_around();
    test_column_isolation();
    test_push_keeps_phase_columns();

    if (failures != 0)
    {
        std::cerr << failures << " measure test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All measure tests passed\n";
    return EXIT_SUCCESS;
}
