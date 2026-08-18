#include "infra/job_system.h"

#include <algorithm>

namespace infra
{
    job_system::job_system(std::uint32_t worker_count, std::uint32_t queue_capacity)
        : capacity_(queue_capacity == 0 ? 1 : queue_capacity)
    {
        const std::uint32_t count = worker_count == 0 ? default_worker_count() : worker_count;
        workers_.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            workers_.emplace_back(&job_system::worker_loop, this);
        }
    }

    job_system::~job_system()
    {
        stop();
    }

    std::uint32_t job_system::default_worker_count() noexcept
    {
        const std::uint32_t hardware = std::thread::hardware_concurrency();
        return hardware > 1 ? hardware - 1 : 1;
    }

    std::uint32_t job_system::worker_count() const noexcept
    {
        return static_cast<std::uint32_t>(workers_.size());
    }

    void job_system::stop() noexcept
    {
        {
            std::lock_guard lock(queue_mutex_);
            if (stopped_)
            {
                return; // 幂等：重复 stop 只生效一次（析构与显式 stop 可叠加）
            }
            stopped_ = true;
        }
        // 注意只唤醒消费者：生产者仍可能阻塞在 not_full_ 上，但 worker 会继续
        // drain 队列（退出条件是"stopped_ 且队列空"），pop 时的 not_full_ 通知
        // 会逐一放行；被放行的生产者看到 stopped_ 后走异常 future 路径退出。
        not_empty_.notify_all();
        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers_.clear();
    }

    void job_system::worker_loop() noexcept
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock lock(queue_mutex_);
                not_empty_.wait(lock, [this] { return !queue_.empty() || stopped_; });
                if (queue_.empty())
                {
                    return; // stopped_ 且队列已 drain
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            not_full_.notify_one();
            // packaged_task 自身把任务异常捕获进 future，这里不会抛出。
            task();
        }
    }

    void job_system::post_main_callback(std::function<void()> fn)
    {
        {
            std::lock_guard lock(callback_mutex_);
            main_callbacks_.push_back(std::move(fn));
        }
    }

    std::size_t job_system::drain_main_callbacks(std::size_t max_count)
    {
        std::size_t executed = 0;
        while (executed < max_count)
        {
            std::function<void()> callback;
            {
                std::lock_guard lock(callback_mutex_);
                if (main_callbacks_.empty())
                {
                    break;
                }
                callback = std::move(main_callbacks_.front());
                main_callbacks_.pop_front();
            }
            callback();
            ++executed;
        }
        return executed;
    }
} // namespace infra
