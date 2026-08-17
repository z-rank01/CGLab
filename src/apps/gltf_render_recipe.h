#pragma once

#include "engine/render_backend.h"

namespace apps
{
    [[nodiscard]] engine::render_driver create_gltf_render_driver();
}
