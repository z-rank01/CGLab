#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <vector>

#include <asset_loader.h>

#include "engine/asset_service.h"
#include "infra/job_system.h"

namespace asset
{
    using request_id = engine::asset_request_id;
    using completed_request = engine::completed_asset_request;

    // C1/I1 迁移：专用 worker 线程 + 双队列 → infra::job_system（I1 验收负载，
    // request ID/result 行为与控制平面协议保持不变）。
    // 线程模型：request/drain_completed/shutdown 均为主线程调用；任务体（解析）
    // 在池 worker 上执行且只写局部结果；完成经池的主线程回调队列投递——
    // 主线程 drain 回调后结果才进入 completed（drain 责任在调用方：引擎在
    // merge_asset_results 帧边界先 drain 回调再 drain_completed）。
    //
    // 在途节流（dcl 并行解码的死锁规避）：解析任务在 worker 内会嵌套提交
    // 数十个解码子任务并阻塞等待；非窃取有界池下，若不限制解析并发，
    // 全部 worker 都可能堵在等待处、解码任务无人消费（死锁）。故在途解析
    // 任务 ≤ max_in_flight_loads，池中始终有富余 worker 消费解码子任务；
    // 同时约束大资产并发解析的瞬时内存。受理上限 max_pending_loads 语义不变。
    class asset_service final : public engine::asset_service
    {
    public:
        explicit asset_service(infra::job_system& jobs);
        ~asset_service() override;
        asset_service(const asset_service&) = delete;
        asset_service& operator=(const asset_service&) = delete;

        void start(std::filesystem::path working_directory) override;
        [[nodiscard]] engine::result<request_id> request(std::filesystem::path path) override;
        [[nodiscard]] std::vector<completed_request> drain_completed() override;
        void shutdown() noexcept override;

    private:
        struct queued_request
        {
            request_id id = 0;
            std::filesystem::path path;
        };

        void submit_load(request_id id, std::filesystem::path path);
        void try_submit_waiting();

        infra::job_system* jobs;
        std::filesystem::path base_directory;
        dcl::load_options decode_options;        // start() 装配并行解码执行器（组合根注入）
        std::deque<completed_request> completed; // 仅主线程回调写入 + 主线程 drain（同线程，免锁）
        std::deque<queued_request> waiting;      // 已受理待提交（在途节流，主线程单写者）
        std::uint32_t in_flight = 0;             // 已提交未完成（主线程单写者）
        request_id next_id = 1;
        bool started = false;
        bool stopping = false;
    };
} // namespace asset
