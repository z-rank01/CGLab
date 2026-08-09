#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "framework/asset_service.h"

namespace asset
{
    using request_id = framework::asset_request_id;
    using completed_request = framework::completed_asset_request;

    class asset_service final : public framework::asset_service
    {
    public:
        asset_service() = default;
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

        void worker_loop() noexcept;

        std::filesystem::path base_directory;
        std::thread worker;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<queued_request> queue;
        std::deque<completed_request> completed;
        request_id next_id = 1;
        bool stopping = false;
    };
} // namespace asset
