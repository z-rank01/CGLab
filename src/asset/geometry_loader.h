#pragma once

#include <filesystem>

#include "engine/geometry.h"
#include "engine/render_backend.h"

namespace asset
{
    [[nodiscard]] engine::result<engine::asset_database> load_geometry(const std::filesystem::path& path);
}
