#pragma once

#include "render_graph/backend/vulkan/surface_provider.h"

namespace interface
{
    class window;
}

namespace platform::vulkan
{
    [[nodiscard]] render_graph::vulkan::surface_provider make_sdl_surface_provider(interface::window& window);
}
