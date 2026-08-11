#pragma once

#include "engine/render_backend.h"
#include "render_graph_vulkan/render_program.h"

namespace engine::vulkan
{
    [[nodiscard]] render_driver create_backend(render_program program = {});
}
