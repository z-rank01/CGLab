#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// control_plane::json_rpc
// - P0 控制平面协议层：JSON-RPC 2.0 编解码 + 方法分派。
// - 全部为纯函数，不触碰 socket / 引擎状态，便于 CTest 单测。
// - 协议版本握手 + 方法级参数校验在这里完成；非法请求只产生 error response，
//   不会进入引擎命令队列。

namespace control_plane
{
    inline constexpr std::uint32_t protocol_version = 1;
    inline constexpr std::string_view server_name   = "VulkanSample";

    // JSON-RPC 2.0 标准错误码
    inline constexpr int error_parse_error     = -32700;
    inline constexpr int error_invalid_request = -32600;
    inline constexpr int error_method_not_found = -32601;
    inline constexpr int error_invalid_params  = -32602;

    struct rpc_request
    {
        nlohmann::json id; // 可能为 null（notification）
        std::string method;
        nlohmann::json params; // 缺省为 object
        bool is_notification = false;
    };

    enum class parse_status
    {
        ok,
        parse_error,
        invalid_request,
    };

    struct parse_result
    {
        parse_status status = parse_status::ok;
        rpc_request request;
        nlohmann::json error_response; // status != ok 时有效
    };

    // --- 编解码 ---

    [[nodiscard]] nlohmann::json make_result(const nlohmann::json& id, const nlohmann::json& result);
    [[nodiscard]] nlohmann::json make_error(const nlohmann::json& id, int code, std::string_view message);
    [[nodiscard]] nlohmann::json make_notification(std::string_view method, const nlohmann::json& params);

    [[nodiscard]] parse_result parse_request(std::string_view text);

    // --- 命令模型（UI → 引擎，帧边界消费） ---

    enum class command_kind
    {
        echo,        // debug.echo：验证命令回环
        frame_pause, // frame.pause
        frame_resume, // frame.resume
        frame_step,  // frame.step {count?: 1..64}
    };

    struct engine_command
    {
        command_kind kind;
        std::string client_id;
        nlohmann::json id;              // 响应用的 rpc id
        std::string message;            // echo 载荷
        std::uint32_t step_count = 1;   // frame_step 参数
    };

    enum class dispatch_outcome
    {
        immediate_response, // IO 线程直接回（session.init / 错误 / 未知方法）
        queue_command,      // 进入引擎命令队列，引擎执行后回响应
    };

    struct dispatch_result
    {
        dispatch_outcome outcome = dispatch_outcome::immediate_response;
        nlohmann::json response;   // immediate_response 时有效
        engine_command command;    // queue_command 时有效
    };

    // 分派一个已解析的请求。client_id 仅用于命令路由，不参与校验。
    [[nodiscard]] dispatch_result dispatch_request(std::string_view client_id, const rpc_request& request);

    // 连接建立时服务端主动推送的握手通知
    [[nodiscard]] nlohmann::json make_hello_notification();

} // namespace control_plane
