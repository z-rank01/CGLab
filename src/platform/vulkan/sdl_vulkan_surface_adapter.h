#pragma once

#include "render_graph/backend/vulkan/runtime.h"

namespace interface
{
    class window;
}

namespace platform::vulkan
{
    [[nodiscard]] render_graph::vk_surface_provider make_sdl_surface_provider(interface::window& window);
}
