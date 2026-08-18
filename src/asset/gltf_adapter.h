#pragma once

#include <filesystem>

#include "engine/geometry.h"
#include "engine/render_backend.h"

namespace asset
{
    // dcl::load_asset 的引擎侧转调：格式分发已收敛进 DCL（gltf/fbx/obj 同签名入口），
    // 本函数只做 dcl::result/load_report 到引擎类型的映射。report 非空时回填
    // parse/convert/decode 三段计时（telemetry.load 数据源）。
    [[nodiscard]] engine::result<engine::asset_database> load_asset(const std::filesystem::path& path,
                                                                    engine::load_report* report = nullptr);
} // namespace asset
