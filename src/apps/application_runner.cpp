#include "apps/application_runner.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "_interface/sdl_window.h"
#include "engine/engine_runtime.h"
#include "utility/logger.h"

namespace apps
{
    int run_application(application_run_request request)
    {
        try
        {
            if (!request.window)
            {
                request.window = std::make_unique<interface::sdl_window>();
            }
            engine::engine_runtime runtime(std::move(request.runtime), std::move(request.renderer), std::move(request.window));
            runtime.configure_sample(std::move(request.sample));
            runtime.initialize();
            if (request.culling_enabled)
            {
                runtime.set_camera_culling(true);
            }
            const bool succeeded = runtime.tick(request.frame_limit);
            runtime.shutdown();
            if (!succeeded)
            {
                return EXIT_FAILURE;
            }
            if (request.require_validation_clean && runtime.validation_error_count() != 0)
            {
                Logger::LogError("Vulkan validation reported " + std::to_string(runtime.validation_error_count()) + " error(s)");
                return EXIT_FAILURE;
            }
            if (request.enforce_smoke_contract)
            {
                const engine::render_statistics statistics = runtime.statistics();
                if (!request.frame_limit || statistics.upload_pass_executions != 1 ||
                    statistics.draw_pass_executions != *request.frame_limit * request.expected_draw_passes_per_frame ||
                    statistics.presented_frames != *request.frame_limit ||
                    statistics.steady_frame_descriptor_updates != 0 ||
                    statistics.pipeline_creations != request.expected_pipeline_creations ||
                    statistics.indirect_groups != *request.frame_limit * request.expected_indirect_groups_per_frame)
                {
                    Logger::LogError("GPU smoke counters did not match the requested frame contract: "
                                     "uploads=" + std::to_string(statistics.upload_pass_executions) +
                                     " draw_passes=" + std::to_string(statistics.draw_pass_executions) +
                                     " presented=" + std::to_string(statistics.presented_frames) +
                                     " steady_desc_updates=" + std::to_string(statistics.steady_frame_descriptor_updates) +
                                     " pipelines=" + std::to_string(statistics.pipeline_creations) +
                                     " indirect_groups=" + std::to_string(statistics.indirect_groups) +
                                     " (expected " + std::to_string(request.expected_draw_passes_per_frame) +
                                     "/" + std::to_string(request.expected_pipeline_creations) +
                                     "/" + std::to_string(request.expected_indirect_groups_per_frame) + " per frame)");
                    return EXIT_FAILURE;
                }
                // R3/M5 per-pass draw 契约（末帧 frame_counters）：
                // 主 pass = 全量 draw（debug quad 归属 debug pass）；
                // 光视图剔除后阴影 pass 不可能多于主 pass；
                // debug pass 仅在 debug 模式画 quad（1 条）。
                const auto& counters = runtime.last_frame_counters();
                const std::uint64_t expected_debug_draws = request.expected_draw_passes_per_frame == 3 ? 1u : 0u;
                if (counters.main_draw_count + counters.debug_draw_count != counters.draw_commands ||
                    counters.shadow_draw_count > counters.main_draw_count ||
                    counters.debug_draw_count != expected_debug_draws)
                {
                    Logger::LogError("GPU smoke per-pass counters did not match: "
                                     "shadow_draws=" + std::to_string(counters.shadow_draw_count) +
                                     " main_draws=" + std::to_string(counters.main_draw_count) +
                                     " debug_draws=" + std::to_string(counters.debug_draw_count) +
                                     " draw_commands=" + std::to_string(counters.draw_commands) +
                                     " (expected debug_draws=" + std::to_string(expected_debug_draws) + ")");
                    return EXIT_FAILURE;
                }
            }
            return EXIT_SUCCESS;
        }
        catch (const std::exception& error)
        {
            Logger::LogError("Application failed: " + std::string(error.what()));
            return EXIT_FAILURE;
        }
    }

    int run_application(int argc,
                        char** argv,
                        std::string_view executable_name,
                        const application_setup& setup,
                        application_cli cli)
    {
        const std::span arguments(argv + 1, static_cast<std::size_t>(argc - 1));
        std::vector<std::string_view> views;
        views.reserve(arguments.size());
        for (const char* argument : arguments)
        {
            views.emplace_back(argument);
        }

        const application_options_result parsed = parse_application_options(views, cli);
        if (parsed.status == application_options_status::help)
        {
            std::cout << application_usage(argc > 0 ? argv[0] : executable_name, cli);
            return EXIT_SUCCESS;
        }
        if (!parsed.succeeded())
        {
            std::cerr << parsed.message << '\n' << application_usage(argc > 0 ? argv[0] : executable_name, cli);
            return EXIT_FAILURE;
        }

        try
        {
            application_setup_result configured = setup(parsed.options);
            if (!configured.request)
            {
                Logger::LogError(configured.error.empty() ? "Application setup failed" : configured.error);
                return EXIT_FAILURE;
            }
            return run_application(std::move(*configured.request));
        }
        catch (const std::exception& error)
        {
            Logger::LogError("Application setup failed: " + std::string(error.what()));
            return EXIT_FAILURE;
        }
    }
}
