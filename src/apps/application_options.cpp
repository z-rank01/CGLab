#include "apps/application_options.h"

#include <charconv>
#include <utility>

namespace apps
{
    namespace
    {
        application_options_result error_result(std::string message)
        {
            return application_options_result{
                .status = application_options_status::error,
                .message = std::move(message),
            };
        }
    } // namespace

    application_options_result parse_application_options(std::span<const std::string_view> arguments,
                                                          application_cli cli)
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

            if (argument == "--asset")
            {
                if (!cli.accepts_asset)
                {
                    return error_result("--asset is not supported by this application");
                }
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
            if (argument == "--no-ui")
            {
                result.options.no_ui = true;
                continue;
            }
            if (argument == "--culling")
            {
                result.options.culling = true;
                continue;
            }
            if (argument == "--debug-view")
            {
                const auto value = consume_value(argument);
                if (!value)
                {
                    return result;
                }
                if (*value == "shadow")
                {
                    result.options.debug_view = apps::debug_view_mode::shadow;
                }
                else if (*value == "depth")
                {
                    result.options.debug_view = apps::debug_view_mode::depth;
                }
                else if (*value == "off")
                {
                    result.options.debug_view = apps::debug_view_mode::off;
                }
                else
                {
                    return error_result("--debug-view requires one of: shadow, depth, off");
                }
                continue;
            }
            if (argument == "--ui-open-browser")
            {
                result.options.ui_open_browser = true;
                continue;
            }
            if (argument == "--ui-port")
            {
                const auto value = consume_value(argument);
                if (!value)
                {
                    return result;
                }

                std::uint32_t port = 0;
                const char* begin = value->data();
                const char* end = begin + value->size();
                const auto [parsed_end, error] = std::from_chars(begin, end, port);
                if (error != std::errc{} || parsed_end != end || port < 1024 || port > 65535)
                {
                    return error_result("--ui-port requires an integer in [1024, 65535]");
                }
                result.options.ui_port = static_cast<std::uint16_t>(port);
                result.options.ui_port_specified = true;
                continue;
            }

            return error_result("Unknown argument: " + std::string(argument));
        }
        return result;
    }

    std::string application_usage(std::string_view executable_name, application_cli cli)
    {
        std::string usage = "Usage: " + std::string(executable_name) + " [--frames <count>] [--validation] [--smoke-test]";
        if (cli.accepts_asset)
        {
            usage += " [--asset <path>]";
        }
        usage += "\n           [--no-ui] [--ui-port <port>] [--ui-open-browser] [--culling]\n";
        usage += "           [--debug-view <shadow|depth|off>]\n";
        usage += "  --no-ui            Disable the control plane WebSocket server (Web UI backend).\n";
        usage += "  --ui-port <port>   Control plane port in [1024, 65535] (default 17381); explicit port\n";
        usage += "                     also enables the control plane under --smoke-test for protocol tests.\n";
        usage += "  --ui-open-browser  Reserved (P3): open the Web UI in the default browser on start.\n";
        usage += "  --culling          Enable per-instance frustum culling on the active camera (A1).\n";
        usage += "  --debug-view       Draw a debug view of an intermediate render target (shadow map\n";
        usage += "                     raw depth / linearized depth) into a screen-corner inset (R4).\n";
        if (cli.accepts_asset)
        {
            usage += "  --asset <path>     Startup geometry asset (.gltf or .glb).\n";
        }
        return usage;
    }
} // namespace apps
