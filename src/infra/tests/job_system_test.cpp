// infra::job_system 单元测试（I1 验收口径：future 取值、异常传递不吞、
// 多生产者 MPMC、主线程回调在执行线程上运行、stop 排空后拒收、幂等）。
#include "infra/job_system.h"

#include <atomic>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::uint32_t failures = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << message << '\n';
        }
    }

    void test_submit_returns_values()
    {
        infra::job_system jobs(2);
        auto number = jobs.submit([] { return 42; });
        check(number.get() == 42, "submit returns int");
        auto text = jobs.submit([] { return std::string("ok"); });
        check(text.get() == "ok", "submit returns string");
        auto nothing = jobs.submit([] { return; });
        nothing.get();
        check(true, "submit void completes");
    }

    void test_exception_lands_in_future_and_worker_survives()
    {
        infra::job_system jobs(2);
        auto bad = jobs.submit([]() -> int { throw std::runtime_error("boom"); });
        bool caught = false;
        try
        {
            (void)bad.get();
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        check(caught, "task exception captured into future");
        auto after = jobs.submit([] { return 7; });
        check(after.get() == 7, "worker survives task exception");
    }

    void test_multi_producer_all_complete()
    {
        infra::job_system jobs(0);
        std::atomic<std::uint64_t> count{0};
        constexpr std::uint64_t per_producer = 2500;
        std::vector<std::thread> producers;
        std::vector<std::future<void>> futures;
        std::mutex futures_mutex;
        for (std::uint32_t producer = 0; producer < 4; ++producer)
        {
            producers.emplace_back([&]
            {
                std::vector<std::future<void>> local;
                local.reserve(per_producer);
                for (std::uint64_t index = 0; index < per_producer; ++index)
                {
                    local.push_back(jobs.submit([&] { count.fetch_add(1, std::memory_order_relaxed); }));
                }
                std::lock_guard lock(futures_mutex);
                futures.insert(futures.end(), std::make_move_iterator(local.begin()), std::make_move_iterator(local.end()));
            });
        }
        for (auto& producer : producers) producer.join();
        for (auto& future : futures) future.get();
        check(count.load() == 4 * per_producer, "multi-producer MPMC: all tasks executed");
    }

    void test_main_callback_runs_on_draining_thread()
    {
        infra::job_system jobs(2);
        const std::thread::id main_id = std::this_thread::get_id();
        std::atomic<bool> ran{false};
        std::atomic<bool> on_main{false};
        jobs.submit([&]
        {
            jobs.post_main_callback([&]
            {
                on_main = std::this_thread::get_id() == main_id;
                ran = true;
            });
        }).get();
        check(jobs.drain_main_callbacks(16) == 1, "drain executes one callback");
        check(ran.load(), "callback ran");
        check(on_main.load(), "callback ran on draining (main) thread");
        check(jobs.drain_main_callbacks(16) == 0, "queue empty after drain");
    }

    void test_stop_drains_then_rejects_and_is_idempotent()
    {
        infra::job_system jobs(1, 8);
        std::atomic<std::uint64_t> done{0};
        constexpr std::uint64_t task_count = 100;
        std::vector<std::future<void>> futures;
        futures.reserve(task_count);
        for (std::uint64_t index = 0; index < task_count; ++index)
        {
            futures.push_back(jobs.submit([&] { done.fetch_add(1, std::memory_order_relaxed); }));
        }
        jobs.stop();
        for (auto& future : futures) future.get();
        check(done.load() == task_count, "stop drains queued tasks before join");

        auto late = jobs.submit([] { return 1; });
        bool rejected = false;
        try
        {
            (void)late.get();
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        check(rejected, "submit after stop returns exceptional future");
        jobs.stop(); // 幂等：二次 stop 无副作用
        check(true, "stop is idempotent");
    }

    void test_default_worker_count()
    {
        infra::job_system jobs;
        check(jobs.worker_count() == infra::job_system::default_worker_count(), "default pool size");
        check(infra::job_system::default_worker_count() >= 1, "pool size >= 1");
    }
} // namespace

int main()
{
    test_submit_returns_values();
    test_exception_lands_in_future_and_worker_survives();
    test_multi_producer_all_complete();
    test_main_callback_runs_on_draining_thread();
    test_stop_drains_then_rejects_and_is_idempotent();
    test_default_worker_count();

    if (failures != 0)
    {
        std::cerr << failures << " infra job_system test(s) failed\n";
        return 1;
    }
    std::cout << "All infra job_system tests passed\n";
    return 0;
}
