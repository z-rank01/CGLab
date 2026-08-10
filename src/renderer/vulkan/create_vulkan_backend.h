#pragma once

#include <memory>

#include "engine/render_backend.h"
#include "renderer/vulkan/render_program.h"

namespace engine::vulkan
{
    [[nodiscard]] std::unique_ptr<render_backend> create_backend(render_program program = {});
}
