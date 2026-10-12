#pragma once

#include <cstdint>
#include <string>

#include "_interface/window.h"

namespace engine
{
    struct control_config
    {
        bool enabled = true;
        std::uint16_t port = 17381;
        bool open_browser = false;
    };

    struct runtime_config
    {
        interface::window_config window{};
        std::string working_directory;
        std::uint8_t frames_in_flight = 3;
        bool validation = false;
        control_config control_plane{};
        // A3-1：最小化跳过帧的容忍上限。跳过路径每次 sleep 8ms，默认 240 帧 ≈ 2s：
        // 有界帧数运行（--frames N）在窗口持续最小化时据此报错退出，而不是空转；
        // 交互式运行（无 frame_limit）不受影响，一直等待恢复。
        std::uint64_t max_skipped_minimized_frames = 240;
    };
} // namespace engine
