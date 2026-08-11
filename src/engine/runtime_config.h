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
    };
} // namespace engine
