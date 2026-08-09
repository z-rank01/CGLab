#pragma once

#include <filesystem>

#include "engine/geometry.h"
#include "engine/render_backend.h"

namespace asset
{
    [[nodiscard]] engine::result<engine::geometry_asset> load_gltf(const std::filesystem::path& path);
}
