#include "application_options.h"

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
        const auto result = parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(!result.options.config_path.has_value());
        CHECK(!result.options.frame_limit.has_value());
    }
    {
        const std::array arguments{
            std::string_view{"--config"}, std::string_view{"config.json"},
            std::string_view{"--asset"}, std::string_view{"scene.gltf"},
            std::string_view{"--frames"}, std::string_view{"3"},
            std::string_view{"--validation"}, std::string_view{"--smoke-test"},
        };
        const auto result = parse_application_options(arguments);
        CHECK(result.succeeded());
        CHECK(result.options.config_path == std::filesystem::path("config.json"));
        CHECK(result.options.asset_path == std::filesystem::path("scene.gltf"));
        CHECK(result.options.frame_limit == 3);
        CHECK(result.options.validation);
        CHECK(result.options.smoke_test);
    }
    {
        const std::array arguments{std::string_view{"--frames"}, std::string_view{"0"}};
        const auto result = parse_application_options(arguments);
        CHECK(result.status == application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--config"}};
        const auto result = parse_application_options(arguments);
        CHECK(result.status == application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--unknown"}};
        const auto result = parse_application_options(arguments);
        CHECK(result.status == application_options_status::error);
    }
    {
        const std::array arguments{std::string_view{"--help"}};
        const auto result = parse_application_options(arguments);
        CHECK(result.status == application_options_status::help);
    }
    {
        application_options options;
        options.config_path = std::filesystem::path("configs") / "app.json";
        CHECK(resolve_asset_path(options, std::filesystem::path("models") / "scene.gltf") ==
              std::filesystem::path("configs") / "models" / "scene.gltf");
        options.asset_path = std::filesystem::path("override") / "scene.glb";
        CHECK(resolve_asset_path(options, "ignored.gltf") ==
              std::filesystem::path("override") / "scene.glb");
    }
    return EXIT_SUCCESS;
}
