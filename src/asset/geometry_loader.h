#pragma once

#include <filesystem>

#include <asset_loader.h>

#include "engine/geometry.h"
#include "engine/render_backend.h"

namespace asset
{
    // 几何资产加载入口：格式分发收敛在 dcl::load_asset（经 gltf_adapter 转调），
    // options 原样下传（含图像解码执行器缝），report 非空时回填三段计时。
    [[nodiscard]] engine::result<engine::asset_database> load_geometry(const std::filesystem::path& path,
                                                                       engine::load_report* report = nullptr,
                                                                       const dcl::load_options& options = {});
} // namespace asset
