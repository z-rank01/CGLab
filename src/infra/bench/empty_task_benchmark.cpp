// infra I0/I1 benchmark：空任务基线（InfrastructureDesign.md §5 口径）。
// 产出对照表，I1 起"bounded queue"行换用库实现 infra::job_system（I0 参考
// 实现已退役），另附 I1 验收用的 CPU 密集任务 并行/串行 比值场景：
//   1. serial          —— 主线程内联执行（下界）；
//   2. thread-per-task —— 每任务 spawn+join 一个 std::thread（朴素对照）；
//   3. job_system      —— 库实现（固定池 + 有界队列 + CV），线程扩展 1/2/4/N；
//   4. cpu_dense       —— 8 个 CPU 密集任务并行 vs 串行（§5 门槛：≤40%）。
// 空任务载荷刻意平凡（写结果槽）：量的是派发/排队开销本身，不是任务体。

#include "infra/job_system.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
    using clock_type = std::chrono::steady_clock;

    std::uint64_t now_ns()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count());
    }

    struct latency_stats
    {
        std::uint64_t count = 0;
        double mean_us = 0.0;
        double p50_us = 0.0;
        double p99_us = 0.0;
    };

    // 拷贝排序求分位（聚合不在测量热路径上，与遥测 summarize 同口径）。
    latency_stats summarize(std::vector<std::uint64_t> samples_ns)
    {
        latency_stats stats;
        if (samples_ns.empty())
        {
            return stats;
        }
        std::sort(samples_ns.begin(), samples_ns.end());
        const auto at = [&](double q)
        {
            const auto index = static_cast<std::size_t>(q * static_cast<double>(samples_ns.size() - 1));
            return static_cast<double>(samples_ns[index]) / 1000.0;
        };
        long double sum = 0.0L;
        for (const std::uint64_t sample : samples_ns) sum += static_cast<long double>(sample);
        stats.count = samples_ns.size();
        stats.mean_us = static_cast<double>(sum / static_cast<long double>(samples_ns.size())) / 1000.0;
        stats.p50_us = at(0.50);
        stats.p99_us = at(0.99);
        return stats;
    }

    void print_row(const char* name, std::uint32_t workers, std::uint64_t task_count,
                   double wall_ms, const latency_stats& latency)
    {
        const double per_task_us = wall_ms * 1000.0 / static_cast<double>(task_count);
        const double throughput = static_cast<double>(task_count) / (wall_ms / 1000.0);
        std::printf("  %-18s workers=%-3u tasks=%-7llu wall=%8.2fms | %8.2f us/task | %10.0f tasks/s | p50=%7.2fus p99=%8.2fus\n",
                    name, workers, static_cast<unsigned long long>(task_count), wall_ms,
                    per_task_us, throughput, latency.p50_us, latency.p99_us);
    }

    // 空任务结果槽（防优化抹除 + DoD 私有槽示范）。
    std::vector<std::uint64_t> result_slots;

    // --- 1. serial：主线程内联 ---
    void bench_serial(std::uint64_t task_count)
    {
        result_slots.assign(task_count, 0);
        const std::uint64_t begin = now_ns();
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            result_slots[index] = index + 1;
        }
        const std::uint64_t end = now_ns();
        std::vector<std::uint64_t> latencies;
        latencies.push_back(0); // 串行无排队延迟，分位无意义
        print_row("serial", 1, task_count, static_cast<double>(end - begin) / 1.0e6, summarize(std::move(latencies)));
    }

    // --- 2. thread-per-task：每任务 spawn+join ---
    void bench_thread_per_task(std::uint64_t task_count)
    {
        result_slots.assign(task_count, 0);
        std::vector<std::uint64_t> latencies;
        latencies.reserve(task_count);
        const std::uint64_t begin = now_ns();
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            const std::uint64_t submit = now_ns();
            std::thread worker([index]
            { result_slots[index] = index + 1; });
            worker.join();
            latencies.push_back(now_ns() - submit);
        }
        const std::uint64_t end = now_ns();
        print_row("thread-per-task", 1, task_count, static_cast<double>(end - begin) / 1.0e6,
                  summarize(std::move(latencies)));
    }

    // --- 3. job_system：库实现（固定池 + 有界队列 + CV）---
    void bench_job_system(std::uint64_t task_count, std::uint32_t worker_count)
    {
        infra::job_system jobs(worker_count, 1024);
        result_slots.assign(task_count, 0);
        std::vector<std::uint64_t> completion_ns(task_count, 0);
        std::vector<std::uint64_t> submit_ns(task_count, 0);
        std::atomic<std::uint64_t> completed{0};

        const std::uint64_t begin = now_ns();
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            submit_ns[index] = now_ns();
            // future 即弃（fire-and-forget 口径）：packaged_task 共享态随任务执行回收，
            // 不阻塞、不泄漏；该路径含每次提交一次的堆分配，即"空任务开销"实测对象。
            static_cast<void>(jobs.submit([&, index]
            {
                result_slots[index] = index + 1;
                completion_ns[index] = now_ns();
                completed.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        while (completed.load(std::memory_order_relaxed) < task_count)
        {
            std::this_thread::yield();
        }
        const std::uint64_t end = now_ns();
        jobs.stop();

        std::vector<std::uint64_t> latencies(task_count);
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            latencies[index] = completion_ns[index] - submit_ns[index];
        }
        print_row("job_system", worker_count, task_count, static_cast<double>(end - begin) / 1.0e6,
                  summarize(std::move(latencies)));
    }

    // --- 4. cpu_dense：8 个 CPU 密集任务并行 vs 串行（I1 验收 §5：并行 ≤ 串行 40%）---
    void bench_cpu_dense(std::uint64_t spin_per_task_ns)
    {
        // 忙等自旋防优化抹除；模拟真实 CPU 密集任务（如纹理解码）。
        const auto spin = [spin_per_task_ns]
        {
            const std::uint64_t deadline = now_ns() + spin_per_task_ns;
            std::uint64_t value = result_slots[0];
            while (now_ns() < deadline)
            {
                value = value * 1664525u + 1013904223u;
            }
            result_slots[0] = value;
        };
        result_slots.assign(1, 1);

        const std::uint64_t serial_begin = now_ns();
        for (std::uint32_t index = 0; index < 8; index++) spin();
        const double serial_ms = static_cast<double>(now_ns() - serial_begin) / 1.0e6;

        double parallel_ms = 0.0;
        {
            infra::job_system jobs(8);
            const std::uint64_t parallel_begin = now_ns();
            std::vector<std::future<void>> futures;
            for (std::uint32_t index = 0; index < 8; index++) futures.push_back(jobs.submit(spin));
            for (auto& future : futures) future.get();
            parallel_ms = static_cast<double>(now_ns() - parallel_begin) / 1.0e6;
        }
        const double ratio = parallel_ms / serial_ms;
        std::printf("  cpu_dense          tasks=8       serial=%8.2fms parallel=%8.2fms | ratio=%5.1f%% (gate: <= 40%%) %s\n",
                    serial_ms, parallel_ms, ratio * 100.0, ratio <= 0.40 ? "[OK]" : "[MISS]");
    }
} // namespace

int main()
{
    std::printf("infra I0/I1 empty-task baseline (unit: wall/ms, latency/us)\n");

    bench_serial(1000000);
    bench_thread_per_task(2000);

    // 线程扩展曲线：1/2/4/N workers（N = hardware_concurrency - 1，与 I1 池同口径）。
    const std::uint32_t hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::uint32_t full_pool = std::max(1u, hardware - 1);
    std::printf("  hardware_concurrency=%u, pool=%u\n", hardware, full_pool);
    for (const std::uint32_t workers : {1u, 2u, 4u, full_pool})
    {
        bench_job_system(200000, workers);
    }

    bench_cpu_dense(50000000); // 50ms/task × 8，并行/串行比值（§5 门槛 ≤40%）

    std::printf("done.\n");
    return 0;
}
