#include "asset/asset_service.h"

#include "asset/geometry_loader.h"

#include <iterator>

namespace
{
    constexpr std::size_t max_pending_loads = 64;
}

namespace asset
{
    asset_service::~asset_service()
    {
        shutdown();
    }

    void asset_service::start(std::filesystem::path working_directory)
    {
        if (worker.joinable())
        {
            return;
        }
        base_directory = std::move(working_directory);
        stopping = false;
        worker = std::thread(&asset_service::worker_loop, this);
    }

    engine::result<request_id> asset_service::request(std::filesystem::path path)
    {
        engine::result<request_id> result;
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                result.error = "Asset service is stopping";
                return result;
            }
            if (queue.size() >= max_pending_loads)
            {
                result.error = "Load queue is full";
                return result;
            }
            result.value = next_id++;
            queue.push_back(queued_request{.id = result.value, .path = std::move(path)});
        }
        condition.notify_one();
        return result;
    }

    std::vector<completed_request> asset_service::drain_completed()
    {
        std::deque<completed_request> local;
        {
            std::lock_guard lock(mutex);
            local.swap(completed);
        }
        return {std::make_move_iterator(local.begin()), std::make_move_iterator(local.end())};
    }

    void asset_service::shutdown() noexcept
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        condition.notify_all();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void asset_service::worker_loop() noexcept
    {
        for (;;)
        {
            queued_request request;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return stopping || !queue.empty(); });
                if (stopping && queue.empty())
                {
                    return;
                }
                request = std::move(queue.front());
                queue.pop_front();
            }
            std::filesystem::path resolved = std::move(request.path);
            if (resolved.is_relative())
            {
                resolved = base_directory / resolved;
            }
            completed_request output{.id = request.id, .result = load_geometry(resolved)};
            {
                std::lock_guard lock(mutex);
                completed.push_back(std::move(output));
            }
        }
    }
} // namespace asset
