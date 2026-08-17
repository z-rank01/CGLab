#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "engine/render_backend.h"

namespace engine
{
    using asset_request_id = std::uint64_t;

    struct completed_asset_request
    {
        asset_request_id id = 0;
        engine::result<engine::asset_database> result;
        // 加载报告：worker 填 load_us/字节数/图片数，主线程补 merge/upload。
        engine::load_report report;
    };

    class asset_service
    {
    public:
        virtual ~asset_service() = default;
        virtual void start(std::filesystem::path working_directory) = 0;
        [[nodiscard]] virtual engine::result<asset_request_id> request(std::filesystem::path path) = 0;
        [[nodiscard]] virtual std::vector<completed_asset_request> drain_completed() = 0;
        virtual void shutdown() noexcept = 0;
    };
} // namespace engine
