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

        dispatch_result invalid_params(const nlohmann::json& id, const std::string& message)
        {
            return immediate(make_error(id, error_invalid_params, message));
        }

        // 读取可选数值参数：存在时必须为 [min, max] 内的数字。返回 false 表示校验失败。
        bool read_optional_number(const nlohmann::json& params, const char* name, double min, double max,
                                  double& out_value, bool& present)
        {
            present = false;
            const auto it = params.find(name);
            if (it == params.end())
            {
                return true;
            }
            if (!it->is_number())
            {
                return false;
            }
            const double value = it->get<double>();
            if (value < min || value > max)
            {
                return false;
            }
            out_value = value;
            present   = true;
            return true;
        }

        // 读取 0..7 的 slot 参数（必填）
        bool read_slot(const nlohmann::json& params, std::uint32_t& slot)
        {
            const auto it = params.find("slot");
            if (it == params.end() || !it->is_number_unsigned())
            {
                return false;
            }
            const std::uint32_t value = it->get<std::uint32_t>();
            if (value > 7)
            {
                return false;
            }
            slot = value;
            return true;
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
                                           {"telemetry.frame", "debug.echo", "frame.pause", "frame.resume", "frame.step",
                                            "camera.set_mode", "camera.set_params", "camera.get_state",
                                            "camera.bookmark.save", "camera.bookmark.goto"}}}));
        }

        if (method == "debug.echo")
        {
            const auto message_it = request.params.find("message");
            if (message_it == request.params.end() || !message_it->is_string())
            {
                return invalid_params(request.id, "debug.echo requires string \"message\"");
            }
            engine_command command = base_command(client_id, request, command_kind::echo);
            command.params         = {{"message", message_it->get<std::string>()}};
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
                    return invalid_params(request.id, "frame.step \"count\" must be an unsigned integer in [1, 64]");
                }
                count = count_it->get<std::uint32_t>();
            }
            engine_command command = base_command(client_id, request, command_kind::frame_step);
            command.params         = {{"count", count}};
            return queued(std::move(command));
        }

        if (method == "camera.set_mode")
        {
            const auto mode_it = request.params.find("mode");
            if (mode_it == request.params.end() || !mode_it->is_string())
            {
                return invalid_params(request.id, "camera.set_mode requires string \"mode\" (\"fly\" | \"orbit\")");
            }
            const std::string mode = mode_it->get<std::string>();
            if (mode != "fly" && mode != "orbit")
            {
                return invalid_params(request.id, "camera.set_mode \"mode\" must be \"fly\" or \"orbit\"");
            }
            engine_command command = base_command(client_id, request, command_kind::camera_set_mode);
            command.params         = {{"mode", mode}};
            return queued(std::move(command));
        }

        if (method == "camera.set_params")
        {
            // 所有字段可选但至少提供一个；存在时按值域校验
            struct field_rule
            {
                const char* name;
                double min;
                double max;
            };
            static constexpr field_rule rules[] = {
                {"fov", 1.0, 175.0},
                {"movement_speed", 0.01, 1000.0},
                {"mouse_sensitivity", 0.001, 10.0},
                {"zoom_speed", 0.01, 100.0},
                {"orbit_distance", 0.1, 10000.0},
                {"near_plane", 0.0001, 1000.0},
                {"far_plane", 1.0, 1000000.0},
            };

            nlohmann::json validated = nlohmann::json::object();
            bool any                 = false;
            for (const field_rule& rule : rules)
            {
                double value = 0.0;
                bool present = false;
                if (!read_optional_number(request.params, rule.name, rule.min, rule.max, value, present))
                {
                    return invalid_params(request.id,
                                          std::string("camera.set_params \"") + rule.name + "\" out of range or not a number");
                }
                if (present)
                {
                    validated[rule.name] = value;
                    any                  = true;
                }
            }
            if (!any)
            {
                return invalid_params(request.id, "camera.set_params requires at least one known parameter");
            }
            engine_command command = base_command(client_id, request, command_kind::camera_set_params);
            command.params         = std::move(validated);
            return queued(std::move(command));
        }

        if (method == "camera.get_state")
        {
            return queued(base_command(client_id, request, command_kind::camera_get_state));
        }

        if (method == "camera.bookmark.save" || method == "camera.bookmark.goto")
        {
            std::uint32_t slot = 0;
            if (!read_slot(request.params, slot))
            {
                return invalid_params(request.id, method + " requires unsigned integer \"slot\" in [0, 7]");
            }
            engine_command command = base_command(
                client_id, request,
                method == "camera.bookmark.save" ? command_kind::camera_bookmark_save : command_kind::camera_bookmark_goto);
            command.params = {{"slot", slot}};
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
