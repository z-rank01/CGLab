#include "asset/asset_service.h"

#include "asset/geometry_loader.h"
#include "asset/gltf_adapter.h"

#include <chrono>
#include <iterator>

namespace
{
    constexpr std::uint32_t max_pending_loads = 64;
    // 在途解析任务上限：解析任务在 worker 内会嵌套提交数十个图像解码子任务并
    // 阻塞等待（非窃取有界池）。在途 ≤4 时，池中始终有 ≥15 个 worker（20 核池
    // 为 19）可消费解码子任务——堵死等待的 worker 不可能凑满全池，死锁不成立；
    // 同时把大资产并发解析的瞬时内存约束在个位数 GB。
    constexpr std::uint32_t max_in_flight_loads = 4;
}

namespace asset
{
    asset_service::asset_service(infra::job_system& jobs) : jobs(&jobs) {}

    asset_service::~asset_service()
    {
        shutdown();
    }

    void asset_service::start(std::filesystem::path working_directory)
    {
        if (started)
        {
            return;
        }
        started = true;
        base_directory = std::move(working_directory);
        stopping = false;
        // 组合根装配：图像解码走 job system 并行执行器（注入 dcl 函数表缝）
        decode_options.images = make_parallel_image_decode_executor(*jobs);
    }

    engine::result<request_id> asset_service::request(std::filesystem::path path)
    {
        engine::result<request_id> result;
        if (stopping)
        {
            result.error = "Asset service is stopping";
            return result;
        }
        if (in_flight + waiting.size() >= max_pending_loads)
        {
            result.error = "Load queue is full";
            return result;
        }
        result.value = next_id++;
        if (in_flight < max_in_flight_loads)
        {
            submit_load(result.value, std::move(path));
        }
        else
        {
            waiting.push_back(queued_request{.id = result.value, .path = std::move(path)});
        }
        return result;
    }

    void asset_service::submit_load(request_id id, std::filesystem::path path)
    {
        ++in_flight;
        // 任务体在池 worker 上执行：纯计算（解析 + 报表），只写局部 output；
        // 完成后经池的主线程回调队列交付——主线程 drain 回调时才并入 completed。
        // future 即弃：失败以错误值交付（下方 catch），不依赖 future 传递异常。
        static_cast<void>(jobs->submit([this, id, path = std::move(path)]() mutable
        {
            std::filesystem::path resolved = std::move(path);
            if (resolved.is_relative())
            {
                resolved = base_directory / resolved;
            }
            // A failing load must become a value-type error result; the task
            // never lets an exception escape into the pool worker.
            completed_request output{.id = id};
            const auto load_begin = std::chrono::steady_clock::now();
            try
            {
                output.result = load_geometry(resolved, &output.report, decode_options);
            }
            catch (const std::exception& error)
            {
                output.result.error = std::string("Asset load threw an exception: ") + error.what();
            }
            catch (...)
            {
                output.result.error = "Asset load failed with an unknown exception";
            }
            output.report.path = resolved.string();
            output.report.load_us =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               std::chrono::steady_clock::now() - load_begin)
                                               .count());
            if (output.result)
            {
                const auto& asset = output.result.value;
                output.report.vertex_bytes = asset.vertex_blob.size() * sizeof(engine::vertex);
                output.report.index_bytes = asset.index_blob.size() * sizeof(std::uint32_t);
                output.report.image_count = static_cast<std::uint32_t>(asset.images.size());
            }
            jobs->post_main_callback([this, output = std::move(output)]() mutable
            {
                completed.push_back(std::move(output));
                --in_flight;
                try_submit_waiting();
            });
        }));
    }

    void asset_service::try_submit_waiting()
    {
        while (!waiting.empty() && in_flight < max_in_flight_loads)
        {
            queued_request next = std::move(waiting.front());
            waiting.pop_front();
            submit_load(next.id, std::move(next.path));
        }
    }

    std::vector<completed_request> asset_service::drain_completed()
    {
        std::deque<completed_request> local;
        local.swap(completed);
        return {std::make_move_iterator(local.begin()), std::make_move_iterator(local.end())};
    }

    void asset_service::shutdown() noexcept
    {
        // 只置停止标记：在途任务由池继续跑完（其完成回调投递后无人 drain 时
        // 随池销毁，不触碰本对象）；池的生命周期归引擎（engine_runtime::jobs_）。
        stopping = true;
    }
} // namespace asset
