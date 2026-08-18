#pragma once

// infra::job_system —— C1/I1 最小 job system（docs/infra_and_dcl/InfrastructureDesign.md §3 口径）。
//
// 四件套：固定线程池（默认 hardware_concurrency - 1）+ MPMC 有界队列 + CV 背压 +
// move-only 任务 + future 取结果；另有主线程回调队列（帧边界合并，对接 Architecture
// "副作用归帧边界出口"横切准则）。
//
// 纪律（§4）：
// - 任务异常经 packaged_task 捕获进 future，worker 永不因此退出；调用方显式 get() 处理。
// - 提交即排队开销含一次堆分配（packaged_task 共享态）；任务池/块式分配后置优化，先正确。
// - 零第三方依赖、不引用任何引擎/Vulkan/SDL 类型——本目录整体可抽仓。

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace infra
{
    class job_system
    {
    public:
        // 构造即启动 worker；worker_count = 0 时取 default_worker_count()。
        explicit job_system(std::uint32_t worker_count = 0, std::uint32_t queue_capacity = 1024);
        // 等价于 stop()：跑完已入队任务后关闭并 join 全部 worker。
        ~job_system();

        job_system(const job_system&) = delete;
        job_system& operator=(const job_system&) = delete;

        // I1 池口径：hardware_concurrency - 1（硬件不报数时退化为 1）。
        [[nodiscard]] static std::uint32_t default_worker_count() noexcept;
        [[nodiscard]] std::uint32_t worker_count() const noexcept;

        // 优雅停止：拒收新任务，跑完已入队的，join 全部 worker。幂等。
        void stop() noexcept;

        // 提交任务，future 取结果。队列满则 CV 等待（有界背压）。
        // 停止后提交返回内含 std::runtime_error 的 future（不丢静默失败）。
        template <typename F>
        [[nodiscard]] auto submit(F&& fn) -> std::future<std::invoke_result_t<F>>
        {
            using R = std::invoke_result_t<F>;
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
            std::future<R> future = task->get_future();
            {
                std::unique_lock lock(queue_mutex_);
                not_full_.wait(lock, [this] { return queue_.size() < capacity_ || stopped_; });
                if (stopped_)
                {
                    std::promise<R> promise;
                    promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("job_system is stopped")));
                    return promise.get_future();
                }
                queue_.push_back([task]() mutable { (*task)(); });
            }
            not_empty_.notify_one();
            return future;
        }

        // 主线程回调队列：任意线程投递（任务内亦可），主线程 drain 时执行。
        void post_main_callback(std::function<void()> fn);
        // 主线程调用：执行至多 max_count 个已投递回调，返回实际执行个数。
        std::size_t drain_main_callbacks(std::size_t max_count);

    private:
        void worker_loop() noexcept;

        const std::uint32_t capacity_;
        std::vector<std::thread> workers_;

        std::mutex queue_mutex_;
        std::condition_variable not_full_;
        std::condition_variable not_empty_;
        std::deque<std::function<void()>> queue_;
        bool stopped_ = false;

        std::mutex callback_mutex_;
        std::deque<std::function<void()>> main_callbacks_;
    };
} // namespace infra
