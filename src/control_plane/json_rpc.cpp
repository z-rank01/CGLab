#include "json_rpc.h"

#include <array>
#include <span>

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

        // --- 方法表（I1）：dispatch 校验与协议 schema 的单一事实源 ---
        // 新增方法 = 加一行表项；校验、capabilities、docs schema 全部同源。
        // 列式平铺（CSR 口径）：规则行扁平存放，方法行持 span 切片。

        enum class param_kind : std::uint8_t
        {
            string,           // 任意字符串（可空）
            non_empty_string, // 非空字符串
            unsigned_int,     // 无符号整数，[min_value, max_value]
            number,           // 任意数字，[min_value, max_value]
            boolean,
            vec3,             // 3 个数字的数组
            string_enum,      // choices（'|' 分隔）之一
            uint_or_null,     // 无符号整数或 null
        };

        struct param_rule
        {
            std::string_view name;
            param_kind kind;
            bool required;
            double min_value = 0.0;
            double max_value = 0.0;
            std::string_view choices;     // string_enum：'|' 分隔的合法值
            double default_value = 0.0;   // has_default 时，缺省写入 validated
            bool has_default = false;
            // string_enum 专用：validated 写合法值下标（uint）而非字符串——
            // 调用方按枚举序消费（如 debug.set_view 对齐 apps::debug_view_mode）。
            bool enum_as_index = false;
        };

        struct method_entry
        {
            std::string_view name;
            command_kind kind;               // immediate 时无效
            std::span<const param_rule> rules;
            bool at_least_one = false;       // 可选字段至少提供一个
            bool immediate = false;          // session.init：IO 线程直接响应
            std::string_view result_hint;    // schema 文档用：result 负载描述
        };

        inline constexpr param_rule rules_echo[]{
            {.name = "message", .kind = param_kind::string, .required = true},
        };
        inline constexpr param_rule rules_frame_step[]{
            {.name = "count", .kind = param_kind::unsigned_int, .required = false,
             .min_value = 1.0, .max_value = 64.0, .default_value = 1.0, .has_default = true},
        };
        inline constexpr param_rule rules_camera_set_mode[]{
            {.name = "mode", .kind = param_kind::string_enum, .required = true, .choices = "fly|orbit"},
        };
        inline constexpr param_rule rules_camera_set_params[]{
            {.name = "fov", .kind = param_kind::number, .required = false, .min_value = 1.0, .max_value = 175.0},
            {.name = "movement_speed", .kind = param_kind::number, .required = false, .min_value = 0.01, .max_value = 1000.0},
            {.name = "mouse_sensitivity", .kind = param_kind::number, .required = false, .min_value = 0.001, .max_value = 10.0},
            {.name = "zoom_speed", .kind = param_kind::number, .required = false, .min_value = 0.01, .max_value = 100.0},
            {.name = "orbit_distance", .kind = param_kind::number, .required = false, .min_value = 0.1, .max_value = 10000.0},
            {.name = "near_plane", .kind = param_kind::number, .required = false, .min_value = 0.0001, .max_value = 1000.0},
            {.name = "far_plane", .kind = param_kind::number, .required = false, .min_value = 1.0, .max_value = 1000000.0},
        };
        inline constexpr param_rule rules_camera_set_culling[]{
            {.name = "enabled", .kind = param_kind::boolean, .required = true},
        };
        inline constexpr param_rule rules_bookmark[]{
            {.name = "slot", .kind = param_kind::unsigned_int, .required = true, .min_value = 0.0, .max_value = 7.0},
        };
        inline constexpr param_rule rules_scene_load_asset[]{
            {.name = "path", .kind = param_kind::non_empty_string, .required = true},
        };
        inline constexpr param_rule rules_object_id[]{
            {.name = "id", .kind = param_kind::unsigned_int, .required = true,
             .min_value = 0.0, .max_value = 4294967295.0},
        };
        inline constexpr param_rule rules_scene_set_visibility[]{
            {.name = "id", .kind = param_kind::unsigned_int, .required = true,
             .min_value = 0.0, .max_value = 4294967295.0},
            {.name = "visible", .kind = param_kind::boolean, .required = true},
        };
        inline constexpr param_rule rules_scene_set_transform[]{
            {.name = "id", .kind = param_kind::unsigned_int, .required = true,
             .min_value = 0.0, .max_value = 4294967295.0},
            {.name = "position", .kind = param_kind::vec3, .required = false},
            {.name = "rotation", .kind = param_kind::vec3, .required = false},
            {.name = "scale", .kind = param_kind::vec3, .required = false},
        };
        inline constexpr param_rule rules_scene_select[]{
            {.name = "id", .kind = param_kind::uint_or_null, .required = true},
        };
        inline constexpr param_rule rules_debug_set_view[]{
            // 枚举序对齐 apps::debug_view_mode（0=off…4=resolved），引擎只搬运下标
            {.name = "view", .kind = param_kind::string_enum, .required = true,
             .choices = "off|shadow|depth|hdr|resolved", .enum_as_index = true},
        };

        inline constexpr method_entry method_table[]{
            {.name = "debug.echo", .kind = command_kind::echo, .rules = rules_echo,
             .result_hint = "{message: string}"},
            {.name = "frame.pause", .kind = command_kind::frame_pause, .rules = {},
             .result_hint = "{paused: true}"},
            {.name = "frame.resume", .kind = command_kind::frame_resume, .rules = {},
             .result_hint = "{paused: false}"},
            {.name = "frame.step", .kind = command_kind::frame_step, .rules = rules_frame_step,
             .result_hint = "{stepped: count}"},
            {.name = "camera.set_mode", .kind = command_kind::camera_set_mode, .rules = rules_camera_set_mode,
             .result_hint = "{mode: string}"},
            {.name = "camera.set_params", .kind = command_kind::camera_set_params,
             .rules = rules_camera_set_params, .at_least_one = true,
             .result_hint = "{applied: string[]（本帧生效的字段名）}"},
            {.name = "camera.get_state", .kind = command_kind::camera_get_state, .rules = {},
             .result_hint = "相机状态对象，字段同 telemetry.frame.camera"},
            {.name = "camera.set_culling", .kind = command_kind::camera_set_culling, .rules = rules_camera_set_culling,
             .result_hint = "{culling: bool}"},
            {.name = "camera.bookmark.save", .kind = command_kind::camera_bookmark_save, .rules = rules_bookmark,
             .result_hint = "{saved: bool, slot: uint}"},
            {.name = "camera.bookmark.goto", .kind = command_kind::camera_bookmark_goto, .rules = rules_bookmark,
             .result_hint = "{goto: true, slot: uint}；空槽位回 error"},
            {.name = "scene.load_asset", .kind = command_kind::scene_load_asset, .rules = rules_scene_load_asset,
             .result_hint = "异步加载；完成时回 {loaded: id, ...} 或 error，进度见 telemetry.load_progress"},
            {.name = "scene.unload", .kind = command_kind::scene_unload, .rules = rules_object_id,
             .result_hint = "{unloaded: id}"},
            {.name = "scene.set_visibility", .kind = command_kind::scene_set_visibility,
             .rules = rules_scene_set_visibility, .result_hint = "{id: uint, visible: bool}"},
            {.name = "scene.set_transform", .kind = command_kind::scene_set_transform,
             .rules = rules_scene_set_transform, .at_least_one = true,
             .result_hint = "{id: uint, ...应用的字段}"},
            {.name = "scene.select", .kind = command_kind::scene_select, .rules = rules_scene_select,
             .result_hint = "{selected: uint|null}"},
            {.name = "scene.list", .kind = command_kind::scene_list, .rules = {},
             .result_hint = "场景状态对象，同 telemetry.scene 负载"},
            {.name = "debug.set_view", .kind = command_kind::debug_set_view, .rules = rules_debug_set_view,
             .result_hint = "{mode: uint（枚举下标，0=off…4=resolved）}"},
        };

        // capabilities 列表：session.init 响应与 schema 共用。
        inline constexpr std::string_view capability_names[]{
            "telemetry.frame", "telemetry.scene", "telemetry.load", "telemetry.load_progress",
            "debug.echo", "debug.set_view",
            "frame.pause", "frame.resume", "frame.step",
            "camera.set_mode", "camera.set_params", "camera.get_state", "camera.set_culling",
            "camera.bookmark.save", "camera.bookmark.goto",
            "scene.load_asset", "scene.unload", "scene.set_visibility",
            "scene.set_transform", "scene.select", "scene.list",
        };

        // 合法值下标（'|' 分隔序），未命中返回 -1。enum_as_index 的参数以此值入 validated。
        int choices_index(std::string_view choices, const std::string& value)
        {
            int index = 0;
            std::size_t begin = 0;
            while (begin <= choices.size())
            {
                const std::size_t sep = choices.find('|', begin);
                const std::string_view item =
                    choices.substr(begin, sep == std::string_view::npos ? sep : sep - begin);
                if (item == value)
                {
                    return index;
                }
                if (sep == std::string_view::npos)
                {
                    break;
                }
                begin = sep + 1;
                ++index;
            }
            return -1;
        }

        bool choices_contains(std::string_view choices, const std::string& value)
        {
            return choices_index(choices, value) >= 0;
        }

        // 按规则表校验 params；通过时填充 validated（只含表中字段），失败时写 error。
        bool validate_params(const method_entry& entry, const nlohmann::json& params,
                             nlohmann::json& validated, std::string& error)
        {
            std::size_t optional_present = 0;
            for (const param_rule& rule : entry.rules)
            {
                const auto it = params.find(std::string(rule.name));
                if (it == params.end())
                {
                    if (rule.required)
                    {
                        error = std::string(entry.name) + " requires \"" + std::string(rule.name) + "\"";
                        return false;
                    }
                    if (rule.has_default)
                    {
                        validated[std::string(rule.name)] = rule.kind == param_kind::unsigned_int
                                                                ? nlohmann::json(static_cast<std::uint32_t>(rule.default_value))
                                                                : nlohmann::json(rule.default_value);
                    }
                    continue;
                }

                const nlohmann::json& value = *it;
                bool ok = true;
                switch (rule.kind)
                {
                case param_kind::string:
                    ok = value.is_string();
                    break;
                case param_kind::non_empty_string:
                    ok = value.is_string() && !value.get<std::string>().empty();
                    break;
                case param_kind::unsigned_int:
                    ok = value.is_number_unsigned() && value.get<std::uint64_t>() >= rule.min_value &&
                         value.get<std::uint64_t>() <= rule.max_value;
                    break;
                case param_kind::number:
                    ok = value.is_number() && value.get<double>() >= rule.min_value &&
                         value.get<double>() <= rule.max_value;
                    break;
                case param_kind::boolean:
                    ok = value.is_boolean();
                    break;
                case param_kind::vec3:
                    ok = value.is_array() && value.size() == 3 && value[0].is_number() &&
                         value[1].is_number() && value[2].is_number();
                    break;
                case param_kind::string_enum:
                    ok = value.is_string() && choices_contains(rule.choices, value.get<std::string>());
                    break;
                case param_kind::uint_or_null:
                    ok = value.is_null() || value.is_number_unsigned();
                    break;
                }
                if (!ok)
                {
                    error = std::string(entry.name) + " \"" + std::string(rule.name) +
                            "\" failed validation (type/range/enum)";
                    return false;
                }

                if (rule.kind == param_kind::string_enum && rule.enum_as_index)
                {
                    validated[std::string(rule.name)] =
                        static_cast<std::uint32_t>(choices_index(rule.choices, value.get<std::string>()));
                }
                else
                {
                    validated[std::string(rule.name)] = value;
                }
                if (!rule.required)
                {
                    ++optional_present;
                }
            }

            if (entry.at_least_one && optional_present == 0)
            {
                error = std::string(entry.name) + " requires at least one optional parameter";
                return false;
            }
            return true;
        }

        const method_entry* find_method(std::string_view name)
        {
            for (const method_entry& entry : method_table)
            {
                if (entry.name == name)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        // --- schema 发射辅助 ---

        nlohmann::json param_rule_schema(const param_rule& rule)
        {
            switch (rule.kind)
            {
            case param_kind::string:
                return {{"type", "string"}};
            case param_kind::non_empty_string:
                return {{"type", "string"}, {"minLength", 1}};
            case param_kind::unsigned_int:
                return {{"type", "integer"},
                        {"minimum", static_cast<std::uint64_t>(rule.min_value)},
                        {"maximum", static_cast<std::uint64_t>(rule.max_value)}};
            case param_kind::number:
                return {{"type", "number"}, {"minimum", rule.min_value}, {"maximum", rule.max_value}};
            case param_kind::boolean:
                return {{"type", "boolean"}};
            case param_kind::vec3:
                return {{"type", "array"},
                        {"minItems", 3},
                        {"maxItems", 3},
                        {"items", {{"type", "number"}}}};
            case param_kind::string_enum:
            {
                nlohmann::json values = nlohmann::json::array();
                std::size_t begin = 0;
                while (begin <= rule.choices.size())
                {
                    const std::size_t sep = rule.choices.find('|', begin);
                    values.push_back(std::string(
                        rule.choices.substr(begin, sep == std::string_view::npos ? sep : sep - begin)));
                    if (sep == std::string_view::npos)
                    {
                        break;
                    }
                    begin = sep + 1;
                }
                return {{"type", "string"}, {"enum", std::move(values)}};
            }
            case param_kind::uint_or_null:
                return {{"type", {"integer", "null"}}, {"minimum", 0}};
            }
            return {};
        }

        nlohmann::json method_schema(const method_entry& entry)
        {
            nlohmann::json properties = nlohmann::json::object();
            nlohmann::json required   = nlohmann::json::array();
            nlohmann::json any_of     = nlohmann::json::array();
            for (const param_rule& rule : entry.rules)
            {
                properties[std::string(rule.name)] = param_rule_schema(rule);
                if (rule.required)
                {
                    required.push_back(rule.name);
                }
                else
                {
                    any_of.push_back({{"required", {std::string(rule.name)}}});
                }
            }
            nlohmann::json params = {{"type", "object"},
                                     {"properties", std::move(properties)},
                                     {"required", std::move(required)},
                                     {"additionalProperties", true}};
            if (entry.at_least_one)
            {
                params["anyOf"] = std::move(any_of);
            }
            return {{"name", entry.name},
                    {"kind", entry.immediate ? "immediate" : "command"},
                    {"params", std::move(params)},
                    {"result", entry.result_hint}};
        }

        // 通知负载表（telemetry 由 engine_runtime 拼装在引擎侧，本表是协议契约）：
        // 字段（name, JSON 类型, 对象成员列表/说明）。
        struct notification_field
        {
            std::string_view name;
            std::string_view type;
            std::string_view members; // object/array 的成员说明，可空
        };

        struct notification_entry
        {
            std::string_view name;
            std::string_view description;
            std::span<const notification_field> fields;
        };

        inline constexpr notification_field fields_hello[]{
            {"protocol_version", "integer", ""},
            {"server", "string", ""},
        };
        inline constexpr notification_field fields_frame[]{
            {"fps", "number", ""},
            {"frame_time_ms", "number", ""},
            {"presented_frames", "integer", ""},
            {"draw_pass_executions", "integer", ""},
            {"upload_pass_executions", "integer", ""},
            {"steady_frame_descriptor_updates", "integer", ""},
            {"pipeline_creations", "integer", ""},
            {"indirect_groups", "integer", ""},
            {"validation_errors", "integer", ""},
            {"paused", "boolean", ""},
            {"camera", "object",
             "mode|position|yaw|pitch|fov|orbit_distance|focus_point|movement_speed|mouse_sensitivity|"
             "zoom_speed|near_plane|far_plane|bookmarks_valid|blending|culling"},
            {"phase_us", "object",
             "poll_events|consume_control_commands|merge_asset_results|update_scene_transforms|"
             "update_cameras|run_sample_systems|extract_render_packet|apply_resource_changes|"
             "submit_render_packet|publish_telemetry（各阶段 p50 微秒）"},
            {"quantiles", "object", "frame_p50_ms|frame_p95_ms|frame_p99_ms"},
            {"counters", "object",
             "instances|visible|culled|draws|buffer_uploads|image_uploads|"
             "shadow_draws|main_draws|debug_draws|resolve_draws"},
        };
        inline constexpr notification_field fields_scene[]{
            {"revision", "integer", "场景修订号，仅在变化时推送"},
            {"selected", "integer|null", "当前选中对象 id"},
            {"objects", "array",
             "元素 {id, name, visible, read_only, draw_count, bounds{min,max}, "
             "transform{position,rotation,scale}}"},
        };
        inline constexpr notification_field fields_load[]{
            {"path", "string", ""},
            {"load_us", "integer", ""},
            {"merge_us", "integer", ""},
            {"upload_us", "integer", ""},
            {"parse_us", "integer", ""},
            {"convert_us", "integer", ""},
            {"decode_us", "integer", ""},
            {"vertex_bytes", "integer", ""},
            {"index_bytes", "integer", ""},
            {"images", "integer", ""},
            {"geometry_arena", "object",
             "count|created|reserved_bytes|used_bytes|allocation_us|plan_us|transfer_us"},
        };
        inline constexpr notification_field fields_load_progress[]{
            {"path", "string", ""},
            {"uploaded_bytes", "integer", ""},
            {"total_bytes", "integer", ""},
            {"fraction", "number", "0..1"},
            {"meshes", "object", "done|total"},
        };

        inline constexpr notification_entry notification_table[]{
            {.name = "session.hello", .description = "连接建立时服务端主动推送的握手",
             .fields = fields_hello},
            {.name = "telemetry.frame", .description = "10Hz 帧遥测（fps/阶段耗时/计数/相机状态）",
             .fields = fields_frame},
            {.name = "telemetry.scene", .description = "场景状态（修订号变化时推送）",
             .fields = fields_scene},
            {.name = "telemetry.load", .description = "资产加载完成后的分段耗时与资源统计",
             .fields = fields_load},
            {.name = "telemetry.load_progress", .description = "分块流式上传进度（M7）",
             .fields = fields_load_progress},
        };
    } // namespace

    dispatch_result dispatch_request(std::string_view client_id,
                                     const rpc_request& request,
                                     std::string_view server)
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
            nlohmann::json capabilities = nlohmann::json::array();
            for (const std::string_view name : capability_names)
            {
                capabilities.push_back(std::string(name));
            }
            return immediate(make_result(request.id,
                                         {{"protocol_version", protocol_version},
                                          {"server", server},
                                          {"capabilities", std::move(capabilities)}}));
        }

        const method_entry* entry = find_method(method);
        if (entry == nullptr)
        {
            return immediate(make_error(request.id, error_method_not_found, "Method not found: " + method));
        }

        nlohmann::json validated = nlohmann::json::object();
        std::string error;
        if (!validate_params(*entry, request.params, validated, error))
        {
            return invalid_params(request.id, error);
        }

        engine_command command = base_command(client_id, request, entry->kind);
        command.params         = std::move(validated);
        return queued(std::move(command));
    }

    nlohmann::json make_hello_notification(std::string_view server)
    {
        return make_notification("session.hello",
                                 {{"protocol_version", protocol_version}, {"server", server}});
    }

    nlohmann::json build_protocol_schema()
    {
        nlohmann::json methods = nlohmann::json::array();
        methods.push_back({{"name", "session.init"},
                           {"kind", "immediate"},
                           {"params",
                            {{"type", "object"},
                             {"properties",
                              {{"protocol_version",
                                {{"type", "integer"},
                                 {"minimum", protocol_version},
                                 {"maximum", protocol_version}}}}},
                             {"required", {"protocol_version"}},
                             {"additionalProperties", true}}},
                           {"result", "{protocol_version, server, capabilities: string[]}"}});
        for (const method_entry& entry : method_table)
        {
            methods.push_back(method_schema(entry));
        }

        nlohmann::json notifications = nlohmann::json::array();
        for (const notification_entry& entry : notification_table)
        {
            nlohmann::json fields = nlohmann::json::array();
            for (const notification_field& field : entry.fields)
            {
                nlohmann::json item = {{"name", field.name}, {"type", field.type}};
                if (!field.members.empty())
                {
                    item["members"] = field.members;
                }
                fields.push_back(std::move(item));
            }
            notifications.push_back({{"name", entry.name},
                                     {"description", entry.description},
                                     {"fields", std::move(fields)}});
        }

        nlohmann::json capabilities = nlohmann::json::array();
        for (const std::string_view name : capability_names)
        {
            capabilities.push_back(std::string(name));
        }

        return {{"protocol", "jsonrpc-2.0"},
                {"protocol_version", protocol_version},
                {"transport", "websocket"},
                {"endpoint", "ws://127.0.0.1:17381（默认；--ui-port 可改）"},
                {"convention",
                 "命令全部帧边界生效；UI 永不直接触碰引擎内存；错误码遵循 JSON-RPC 2.0 标准；"
                 "HTTP 静态服务与 WS 同端口（GET / 提供 dev console）"},
                {"capabilities", std::move(capabilities)},
                {"methods", std::move(methods)},
                {"notifications", std::move(notifications)}};
    }

} // namespace control_plane
