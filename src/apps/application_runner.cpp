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
                    statistics.draw_pass_executions != *request.frame_limit ||
                    statistics.presented_frames != *request.frame_limit ||
                    statistics.steady_frame_descriptor_updates != 0 ||
                    statistics.pipeline_creations != request.expected_pipeline_creations ||
                    statistics.indirect_groups != *request.frame_limit * request.expected_indirect_groups_per_frame)
                {
                    Logger::LogError("GPU smoke counters did not match the requested frame contract");
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
