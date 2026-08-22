#include "asset/asset_service.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{
    // C1/I1：完成经池的主线程回调队列投递——等待循环必须同时 drain 回调与结果。
    asset::completed_request wait_for(asset::asset_service& service, infra::job_system& jobs, asset::request_id id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            jobs.drain_main_callbacks(64);
            for (asset::completed_request& result : service.drain_completed())
            {
                if (result.id == id)
                {
                    return std::move(result);
                }
            }
            std::this_thread::yield();
        }
        return {.id = id, .result = {.error = "Timed out waiting for asset service"}};
    }
}

int main()
{
    infra::job_system jobs(2);
    asset::asset_service service(jobs);
    service.start(CGLAB_SOURCE_DIR);
    const auto valid = service.request("assets/triangle.gltf");
    if (!valid)
    {
        return EXIT_FAILURE;
    }
    const auto loaded = wait_for(service, jobs, valid.value);
    const auto missing = service.request("assets/missing.gltf");
    if (!missing)
    {
        return EXIT_FAILURE;
    }
    const auto failed = wait_for(service, jobs, missing.value);
    service.shutdown();
    service.shutdown();
    if (!loaded.result || failed.result || loaded.result.value.primitives.size() != 1 ||
        loaded.result.value.nodes.size() != 1)
    {
        std::cerr << "Unexpected asset service result\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
