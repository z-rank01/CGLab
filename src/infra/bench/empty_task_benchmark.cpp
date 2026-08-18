// infra I0 benchmark：空任务基线（InfrastructureDesign.md §5 口径）。
// 产出三组对照 + 线程扩展曲线，数据是 I1 验收（同表复跑对比）的基线：
//   1. serial          —— 主线程内联执行（下界）；
//   2. thread-per-task —— 每任务 spawn+join 一个 std::thread（朴素对照）；
//   3. bounded queue   —— 有界环队列 + mutex/CV + 固定 worker 池（I1 设计形态，
//                          本文件内置参考实现；I1 落地后以库实现复跑本表）。
// 任务载荷刻意平凡（写结果槽）：I0 量的是派发/排队开销本身，不是任务体。
// 结果槽按 DoD 口径预分配——worker 只写自己的槽位，主线程汇总。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
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

    // --- 3. 有界环队列 + CV（I1 形态参考实现；SPMC 负载：主线程单生产者）---
    struct queue_task
    {
        std::uint64_t submit_ns = 0;
        std::uint64_t result_index = 0;
    };

    class bounded_task_queue
    {
    public:
        explicit bounded_task_queue(std::uint32_t capacity) : slots_(capacity) {}

        void push(const queue_task& task)
        {
            std::unique_lock lock(mutex_);
            not_full_.wait(lock, [&] { return count_ < slots_.size(); });
            slots_[tail_] = task;
            tail_ = (tail_ + 1) % slots_.size();
            count_++;
            lock.unlock();
            not_empty_.notify_one();
        }

        // 返回 false 表示队列已关闭且无剩余任务。
        bool pop(queue_task& task)
        {
            std::unique_lock lock(mutex_);
            not_empty_.wait(lock, [&] { return count_ > 0 || closed_; });
            if (count_ == 0)
            {
                return false;
            }
            task = slots_[head_];
            head_ = (head_ + 1) % slots_.size();
            count_--;
            lock.unlock();
            not_full_.notify_one();
            return true;
        }

        void close()
        {
            {
                std::lock_guard lock(mutex_);
                closed_ = true;
            }
            not_empty_.notify_all();
        }

    private:
        std::vector<queue_task> slots_;
        std::size_t head_ = 0;
        std::size_t tail_ = 0;
        std::size_t count_ = 0;
        bool closed_ = false;
        std::mutex mutex_;
        std::condition_variable not_full_;
        std::condition_variable not_empty_;
    };

    void bench_queue(std::uint64_t task_count, std::uint32_t worker_count)
    {
        result_slots.assign(task_count, 0);
        std::vector<std::uint64_t> completion_ns(task_count, 0);
        std::vector<std::uint64_t> submit_ns(task_count, 0);
        bounded_task_queue queue(1024);
        std::atomic<std::uint64_t> completed{0};

        const auto worker_body = [&]
        {
            queue_task task;
            while (queue.pop(task))
            {
                result_slots[task.result_index] = task.result_index + 1;
                completion_ns[task.result_index] = now_ns();
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        const std::uint64_t begin = now_ns();
        for (std::uint32_t index = 0; index < worker_count; index++)
        {
            workers.emplace_back(worker_body);
        }
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            submit_ns[index] = now_ns();
            queue.push({.submit_ns = submit_ns[index], .result_index = index});
        }
        // 等待全部完成后再关队列（关早会丢任务，关晚 worker 空转）。
        while (completed.load(std::memory_order_relaxed) < task_count)
        {
            std::this_thread::yield();
        }
        const std::uint64_t end = now_ns();
        queue.close();
        for (auto& worker : workers) worker.join();

        std::vector<std::uint64_t> latencies(task_count);
        for (std::uint64_t index = 0; index < task_count; index++)
        {
            latencies[index] = completion_ns[index] - submit_ns[index];
        }
        print_row("bounded queue", worker_count, task_count, static_cast<double>(end - begin) / 1.0e6,
                  summarize(std::move(latencies)));
    }
} // namespace

int main()
{
    std::printf("infra I0 empty-task baseline (unit: wall/ms, latency/us)\n");

    bench_serial(1000000);
    bench_thread_per_task(2000);

    // 线程扩展曲线：1/2/4/N workers（N = hardware_concurrency - 1，与 I1 池同口径）。
    const std::uint32_t hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::uint32_t full_pool = std::max(1u, hardware - 1);
    std::printf("  hardware_concurrency=%u, pool=%u\n", hardware, full_pool);
    for (const std::uint32_t workers : {1u, 2u, 4u, full_pool})
    {
        bench_queue(200000, workers);
    }

    std::printf("done.\n");
    return 0;
}
