#include "asset/asset_service.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{
    asset::completed_request wait_for(asset::asset_service& service, asset::request_id id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
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
    asset::asset_service service;
    service.start(CGLAB_SOURCE_DIR);
    const auto valid = service.request("assets/triangle.gltf");
    if (!valid)
    {
        return EXIT_FAILURE;
    }
    const auto loaded = wait_for(service, valid.value);
    const auto missing = service.request("assets/missing.gltf");
    if (!missing)
    {
        return EXIT_FAILURE;
    }
    const auto failed = wait_for(service, missing.value);
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
