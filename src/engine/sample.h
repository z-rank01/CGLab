#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "_interface/camera_component.h"
#include "engine/asset_service.h"
#include "engine/frame_channels.h"
#include "scene/scene_registry.h"

namespace engine
{
    // 调试视图覆盖（M8/B2）：control plane 的 debug.set_view 命令写入，
    // 引擎每帧以持久成员裸指针发布进 channels；sample 读取并翻译为自身的
    // debug_view_request 发布（引擎只搬运 mode 数值，模式语义归 apps 层，
    // 取值序与 apps::debug_view_mode 一致：0=off…4=resolved）。
    // active=false = 无 web 覆盖，sample 回退 CLI 的 --debug-view 选项。
    struct debug_view_override
    {
        std::uint32_t mode = 0;
        bool active = false;
    };

    struct runtime_services
    {
        scene::scene_registry& scene;
        interface::camera_container& cameras;
        std::size_t active_camera = 0;
        // 帧通道行表（引擎提供秩序机制给系统回调：帧首已 clear，系统在此发布
        // 组件表通道，extract/submit 阶段后续消费；每通道单写者）。
        engine::frame_channels& channels;
        std::function<engine::result<asset_request_id>(std::filesystem::path)> request_asset;
        std::function<void(std::string_view)> post_message;
    };

    struct sample
    {
        std::string name;
        std::optional<engine::asset_database> startup_geometry;
        std::optional<std::filesystem::path> required_startup_asset;
        std::function<void(runtime_services&, float)> update;
    };
} // namespace engine
