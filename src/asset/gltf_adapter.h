#pragma once

#include <filesystem>

#include <asset_loader.h>

#include "engine/geometry.h"
#include "engine/render_backend.h"
#include "infra/job_system.h"

namespace asset
{
    // dcl::load_asset 的引擎侧转调：options 原样下传（含图像解码执行器缝），
    // report 非空时回填 parse/convert/decode 三段计时（telemetry.load 数据源）。
    [[nodiscard]] engine::result<engine::asset_database> load_asset(const std::filesystem::path& path,
                                                                    engine::load_report* report = nullptr,
                                                                    const dcl::load_options& options = {});

    // C1/dcl 性能：job-system 并行图像解码执行器（组合根注入 dcl 函数表缝）。
    // 每图一个任务提交到池，调用线程等待全部完成；嵌套提交安全性依赖
    // asset_service 的在途节流（≤4 个解析任务，死锁分析见 .cpp）。
    [[nodiscard]] dcl::image_decode_executor make_parallel_image_decode_executor(infra::job_system& jobs) noexcept;
} // namespace asset
