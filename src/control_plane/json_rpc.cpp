#include "json_rpc.h"

namespace control_plane
{
    nlohmann::json make_result(const nlohmann::json& id, const nlohmann::json& result)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result},
        };
    }

    nlohmann::json make_error(const nlohmann::json& id, int code, std::string_view message)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", message}}},
        };
    }

    nlohmann::json make_notification(std::string_view method, const nlohmann::json& params)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params},
        };
    }

    parse_result parse_request(std::string_view text)
    {
        parse_result result;
        const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
        if (document.is_discarded())
        {
            result.status         = parse_status::parse_error;
            result.error_response = make_error(nullptr, error_parse_error, "Parse error: not valid JSON");
            return result;
        }
        if (!document.is_object() || !document.contains("method") || !document["method"].is_string())
        {
            result.status         = parse_status::invalid_request;
            result.error_response = make_error(nullptr, error_invalid_request, "Invalid Request: missing string member \"method\"");
            return result;
        }

        result.request.method = document["method"].get<std::string>();
        result.request.id     = document.value("id", nlohmann::json(nullptr));
        result.request.is_notification = !document.contains("id");
        result.request.params = document.value("params", nlohmann::json::object());
        if (!result.request.params.is_object())
        {
            result.status         = parse_status::invalid_request;
            result.error_response = make_error(result.request.id, error_invalid_request, "Invalid Request: \"params\" must be an object");
            return result;
        }
        return result;
    }

    namespace
    {
        dispatch_result immediate(nlohmann::json response)
        {
            dispatch_result result;
            result.outcome  = dispatch_outcome::immediate_response;
            result.response = std::move(response);
            return result;
        }

        dispatch_result queued(engine_command command)
        {
            dispatch_result result;
            result.outcome = dispatch_outcome::queue_command;
            result.command = std::move(command);
            return result;
        }

        engine_command base_command(std::string_view client_id, const rpc_request& request, command_kind kind)
        {
            engine_command command;
            command.kind      = kind;
            command.client_id = std::string(client_id);
            command.id        = request.id;
            return command;
        }
    } // namespace

    dispatch_result dispatch_request(std::string_view client_id, const rpc_request& request)
    {
        const std::string& method = request.method;

        if (method == "session.init")
        {
            const auto version_it = request.params.find("protocol_version");
            if (version_it == request.params.end() || !version_it->is_number_unsigned())
            {
                return immediate(make_error(request.id, error_invalid_params,
                                            "session.init requires unsigned integer \"protocol_version\""));
            }
            if (version_it->get<std::uint32_t>() != protocol_version)
            {
                return immediate(make_error(request.id, error_invalid_params,
                                            "Unsupported protocol_version; server speaks " +
                                                std::to_string(protocol_version)));
            }
            return immediate(make_result(request.id,
                                         {{"protocol_version", protocol_version},
                                          {"server", server_name},
                                          {"capabilities",
                                           {"telemetry.frame", "debug.echo", "frame.pause", "frame.resume", "frame.step"}}}));
        }

        if (method == "debug.echo")
        {
            const auto message_it = request.params.find("message");
            if (message_it == request.params.end() || !message_it->is_string())
            {
                return immediate(make_error(request.id, error_invalid_params,
                                            "debug.echo requires string \"message\""));
            }
            engine_command command = base_command(client_id, request, command_kind::echo);
            command.message        = message_it->get<std::string>();
            return queued(std::move(command));
        }

        if (method == "frame.pause")
        {
            return queued(base_command(client_id, request, command_kind::frame_pause));
        }

        if (method == "frame.resume")
        {
            return queued(base_command(client_id, request, command_kind::frame_resume));
        }

        if (method == "frame.step")
        {
            std::uint32_t count = 1;
            if (const auto count_it = request.params.find("count"); count_it != request.params.end())
            {
                if (!count_it->is_number_unsigned() || count_it->get<std::uint32_t>() < 1 ||
                    count_it->get<std::uint32_t>() > 64)
                {
                    return immediate(make_error(request.id, error_invalid_params,
                                                "frame.step \"count\" must be an unsigned integer in [1, 64]"));
                }
                count = count_it->get<std::uint32_t>();
            }
            engine_command command = base_command(client_id, request, command_kind::frame_step);
            command.step_count     = count;
            return queued(std::move(command));
        }

        return immediate(make_error(request.id, error_method_not_found, "Method not found: " + method));
    }

    nlohmann::json make_hello_notification()
    {
        return make_notification("session.hello",
                                 {{"protocol_version", protocol_version}, {"server", server_name}});
    }

} // namespace control_plane
