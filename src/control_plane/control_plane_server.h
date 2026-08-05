#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "json_rpc.h"

// control_plane::control_plane_server
// - P0 控制平面：localhost WebSocket(JSON-RPC 2.0) 服务。
// - 线程模型：
//   * IO 线程（ixwebsocket 内部线程）：协议解析/校验，合法命令入队，即时响应直接发回。
//   * 引擎主线程：帧边界 drain_commands() 消费命令；publish()/post_response() 直接发送。
// - 跨线程只交换值类型（JSON / engine_command 副本），IO 线程绝不持有引擎指针。
// - 背压：命令队列有界，溢出丢弃并计数；ixwebsocket 发送本身非阻塞，不拖慢渲染线程。

namespace control_plane
{
    inline constexpr std::uint16_t default_port = 17381;

    struct control_plane_config
    {
        bool enabled = true;
        std::uint16_t port = default_port;
        bool open_browser = false; // P3 预留：当前无 HTTP 静态服务，暂为空操作
    };

    struct control_plane_statistics
    {
        std::uint64_t commands_received = 0;
        std::uint64_t commands_dropped  = 0; // 队列溢出
        std::uint64_t responses_sent    = 0;
        std::uint64_t telemetry_sent    = 0;
        std::uint64_t sends_failed      = 0; // 客户端发送途中断开等
    };

    class control_plane_server
    {
    public:
        control_plane_server();
        ~control_plane_server();

        control_plane_server(const control_plane_server&)            = delete;
        control_plane_server& operator=(const control_plane_server&) = delete;

        // 在独立线程启动 WS 服务。端口被占用等失败返回 false（调用方可降级为无 UI 运行）。
        [[nodiscard]] bool start(const control_plane_config& config);
        void stop() noexcept;

        [[nodiscard]] bool running() const;
        [[nodiscard]] std::size_t client_count() const;
        [[nodiscard]] control_plane_statistics statistics() const;

        // --- 引擎主线程调用 ---

        // 帧边界取走全部待处理命令。
        [[nodiscard]] std::vector<engine_command> drain_commands();

        // 广播遥测通知（telemetry.frame 等）。
        void publish(const nlohmann::json& notification);

        // 向发起请求的客户端回响应。
        void post_response(const std::string& client_id, const nlohmann::json& response);

    private:
        // IO 线程入口：解析 + 校验 + 入队/即时响应
        void handle_message(const std::string& client_id, const std::string& text);

        struct impl;
        std::unique_ptr<impl> server_impl;

        mutable std::mutex command_mutex;
        std::deque<engine_command> pending_commands;

        mutable std::mutex statistics_mutex;
        control_plane_statistics counters;

        static constexpr std::size_t max_pending_commands = 256;
    };

} // namespace control_plane
