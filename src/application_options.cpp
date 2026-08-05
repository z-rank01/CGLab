#include "application_options.h"

#include <charconv>
#include <utility>

namespace
{
    application_options_result error_result(std::string message)
    {
        return application_options_result{
            .status = application_options_status::error,
            .message = std::move(message),
        };
    }
}

application_options_result parse_application_options(std::span<const std::string_view> arguments)
{
    application_options_result result;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h")
        {
            result.status = application_options_status::help;
            return result;
        }
        if (argument == "--validation")
        {
            result.options.validation = true;
            continue;
        }
        if (argument == "--smoke-test")
        {
            result.options.smoke_test = true;
            continue;
        }

        const auto consume_value = [&](std::string_view option) -> std::optional<std::string_view>
        {
            if (index + 1 >= arguments.size())
            {
                result = error_result("Missing value for " + std::string(option));
                return std::nullopt;
            }
            ++index;
            return arguments[index];
        };

        if (argument == "--config")
        {
            const auto value = consume_value(argument);
            if (!value)
            {
                return result;
            }
            result.options.config_path = std::filesystem::path(*value);
            continue;
        }
        if (argument == "--asset")
        {
            const auto value = consume_value(argument);
            if (!value)
            {
                return result;
            }
            result.options.asset_path = std::filesystem::path(*value);
            continue;
        }
        if (argument == "--frames")
        {
            const auto value = consume_value(argument);
            if (!value)
            {
                return result;
            }

            std::uint64_t frame_limit = 0;
            const char* begin = value->data();
            const char* end = begin + value->size();
            const auto [parsed_end, error] = std::from_chars(begin, end, frame_limit);
            if (error != std::errc{} || parsed_end != end || frame_limit == 0)
            {
                return error_result("--frames requires a positive integer");
            }
            result.options.frame_limit = frame_limit;
            continue;
        }

        return error_result("Unknown argument: " + std::string(argument));
    }
    return result;
}

std::string application_usage(std::string_view executable_name)
{
    return "Usage: " + std::string(executable_name) +
           " [--config <path>] [--asset <path>] [--frames <count>] [--validation] [--smoke-test]\n";
}

std::filesystem::path resolve_asset_path(
    const application_options& options,
    const std::filesystem::path& configured_asset_path)
{
    if (options.asset_path)
    {
        return options.asset_path->lexically_normal();
    }
    if (configured_asset_path.empty())
    {
        return {};
    }
    if (configured_asset_path.is_relative() && options.config_path)
    {
        return (options.config_path->parent_path() / configured_asset_path).lexically_normal();
    }
    return configured_asset_path.lexically_normal();
}
