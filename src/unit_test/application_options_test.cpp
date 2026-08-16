#include "apps/application_options.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
    void check(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

int main()
{
    {
        const std::array<std::string_view, 0> arguments{};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(!result.options.asset_path.has_value());
        CHECK(!result.options.frame_limit.has_value());
    }
    {
        const std::array arguments{
            std::string_view{"--frames"}, std::string_view{"3"},
            std::string_view{"--validation"}, std::string_view{"--smoke-test"},
        };
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.frame_limit == 3);
        CHECK(result.options.validation);
        CHECK(result.options.smoke_test);
    }
    {
        const std::array arguments{std::string_view{"--asset"}, std::string_view{"scene.gltf"}};
        const auto result = apps::parse_application_options(arguments, {.accepts_asset = true});
        CHECK(result.succeeded());
        CHECK(result.options.asset_path == std::filesystem::path("scene.gltf"));
    }
    {
        const std::array arguments{std::string_view{"--asset"}, std::string_view{"scene.gltf"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.status == apps::application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--frames"}, std::string_view{"0"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.status == apps::application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--asset"}};
        const auto result = apps::parse_application_options(arguments, {.accepts_asset = true});
        CHECK(result.status == apps::application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--help"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.status == apps::application_options_status::help);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"shadow"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.debug_view == apps::debug_view_mode::shadow);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"depth"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.debug_view == apps::debug_view_mode::depth);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"off"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.debug_view == apps::debug_view_mode::off);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"hdr"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.debug_view == apps::debug_view_mode::hdr);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"resolved"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.debug_view == apps::debug_view_mode::resolved);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}, std::string_view{"bogus"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.status == apps::application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--debug-view"}};
        const auto result = apps::parse_application_options(arguments);
        CHECK(result.status == apps::application_options_status::error);
    }
    return EXIT_SUCCESS;
}
