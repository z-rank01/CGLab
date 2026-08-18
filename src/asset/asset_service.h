#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <vector>

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
        infra::job_system* jobs;
        std::filesystem::path base_directory;
        std::deque<completed_request> completed; // 仅主线程回调写入 + 主线程 drain（同线程，免锁）
        std::uint32_t pending = 0;               // 已提交未合并（背压计数，主线程单写者）
        request_id next_id = 1;
        bool started = false;
        bool stopping = false;
    };
} // namespace asset
