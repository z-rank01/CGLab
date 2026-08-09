#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "application_options.h"
#include "engine/render_backend.h"
#include "framework/runtime_config.h"
#include "framework/sample.h"

namespace apps
{
    struct application_run_request
    {
        framework::runtime_config runtime;
        framework::sample sample;
        std::unique_ptr<engine::render_backend> renderer;
        std::optional<std::uint64_t> frame_limit;
        bool require_validation_clean = false;
        bool enforce_smoke_contract = false;
    };

    struct application_setup_result
    {
        std::optional<application_run_request> request;
        std::string error;
    };

    using application_setup = std::function<application_setup_result(const application_options&)>;

    [[nodiscard]] int run_application(application_run_request request);
    [[nodiscard]] int run_application(int argc,
                                      char** argv,
                                      std::string_view executable_name,
                                      const application_setup& setup);
}
