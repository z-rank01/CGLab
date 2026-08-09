#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "_interface/camera_component.h"
#include "framework/asset_service.h"
#include "scene/scene_registry.h"

namespace framework
{
    struct runtime_services
    {
        scene::scene_registry& scene;
        interface::camera_container& cameras;
        std::size_t active_camera = 0;
        std::function<engine::result<asset_request_id>(std::filesystem::path)> request_asset;
        std::function<void(std::string_view)> post_message;
    };

    struct sample
    {
        std::string name;
        std::optional<engine::geometry_asset> startup_geometry;
        std::optional<std::filesystem::path> required_startup_asset;
        std::function<void(runtime_services&, float)> update;
    };
} // namespace framework
