#include "control_plane_server.h"

#include <unordered_map>
#include <utility>

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include "utility/logger.h"

namespace control_plane
{
    struct control_plane_server::impl
    {
        explicit impl(control_plane_server& owner) : owner(owner) {}

        void on_client_message(const std::shared_ptr<ix::ConnectionState>& connection_state,
                               ix::WebSocket& web_socket,
                               const ix::WebSocketMessagePtr& message);

        void track_client_open(const std::string& client_id, ix::WebSocket& web_socket);
        void track_client_close(const std::string& client_id, ix::WebSocket& web_socket);

        // 发送快照：client_id → WebSocket。仅用于发送，生命周期由 ixwebsocket 管理。
        [[nodiscard]] std::vector<std::pair<std::string, std::shared_ptr<ix::WebSocket>>> client_snapshot() const;
        [[nodiscard]] std::shared_ptr<ix::WebSocket> find_client(const std::string& client_id) const;

        control_plane_server& owner;
        std::unique_ptr<ix::WebSocketServer> ws_server;

        mutable std::mutex clients_mutex;
        std::unordered_map<std::string, std::shared_ptr<ix::WebSocket>> clients;

        static constexpr std::size_t max_tracked_clients = 32;
    };

    namespace
    {
        void send_payload(const std::shared_ptr<ix::WebSocket>& client, const std::string& text,
                          std::uint64_t* failure_counter)
        {
            if (!client)
            {
                return;
            }
            const ix::WebSocketSendInfo info = client->sendText(text);
            if (!info.success && failure_counter != nullptr)
            {
                ++(*failure_counter);
            }
        }
    } // namespace

    void control_plane_server::impl::on_client_message(const std::shared_ptr<ix::ConnectionState>& connection_state,
                                                       ix::WebSocket& web_socket,
                                                       const ix::WebSocketMessagePtr& message)
    {
        const std::string client_id = connection_state ? connection_state->getId() : std::string{};

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
        {
            track_client_open(client_id, web_socket);
            const std::string hello = make_hello_notification().dump();
            web_socket.sendText(hello);
            Logger::LogInfo("Control plane client connected: " + client_id);
            break;
        }
        case ix::WebSocketMessageType::Close:
        {
            track_client_close(client_id, web_socket);
            Logger::LogInfo("Control plane client disconnected: " + client_id);
            break;
        }
        case ix::WebSocketMessageType::Message:
        {
            if (message->binary)
            {
                // P0 仅接受文本帧（JSON）
                web_socket.sendText(make_error(nullptr, error_invalid_request, "Binary frames are not supported").dump());
                break;
            }
            owner.handle_message(client_id, message->str);
            break;
        }
        default:
            break;
        }
    }

    void control_plane_server::impl::track_client_open(const std::string& client_id, ix::WebSocket& web_socket)
    {
        std::lock_guard lock(clients_mutex);
        if (clients.size() >= max_tracked_clients)
        {
            Logger::LogWarning("Control plane client tracking is full; dropping client " + client_id);
            return;
        }
        // Open 回调拿的是 WebSocket&，从 server 的客户端集合里找到对应 shared_ptr 以便跨线程发送
        for (const auto& candidate : ws_server->getClients())
        {
            if (candidate.get() == &web_socket)
            {
                clients[client_id] = candidate;
                return;
            }
        }
    }

    void control_plane_server::impl::track_client_close(const std::string& client_id, ix::WebSocket&)
    {
        std::lock_guard lock(clients_mutex);
        clients.erase(client_id);
    }

    std::vector<std::pair<std::string, std::shared_ptr<ix::WebSocket>>> control_plane_server::impl::client_snapshot() const
    {
        std::lock_guard lock(clients_mutex);
        return {clients.begin(), clients.end()};
    }

    std::shared_ptr<ix::WebSocket> control_plane_server::impl::find_client(const std::string& client_id) const
    {
        std::lock_guard lock(clients_mutex);
        const auto it = clients.find(client_id);
        return it != clients.end() ? it->second : nullptr;
    }

    control_plane_server::control_plane_server() = default;

    control_plane_server::~control_plane_server()
    {
        stop();
    }

    bool control_plane_server::start(const control_plane_config& config)
    {
        if (server_impl)
        {
            return server_impl->ws_server != nullptr;
        }

        // Windows 必须先初始化 Winsock（WSAStartup），否则 socket 调用静默失败
        if (!ix::initNetSystem())
        {
            Logger::LogError("Control plane failed to initialize the network system");
            return false;
        }

        server_impl              = std::make_unique<impl>(*this);
        server_impl->ws_server   = std::make_unique<ix::WebSocketServer>(config.port, std::string("127.0.0.1"));
        ix::WebSocketServer& ws  = *server_impl->ws_server;
        ws.disablePerMessageDeflate(); // 本地小报文，关掉压缩降低时延抖动
        impl* impl_ptr = server_impl.get();
        ws.setOnClientMessageCallback(
            [impl_ptr](const std::shared_ptr<ix::ConnectionState>& connection_state, ix::WebSocket& web_socket,
                       const ix::WebSocketMessagePtr& message)
            {
                impl_ptr->on_client_message(connection_state, web_socket, message);
            });

        const auto [listen_ok, error] = ws.listen();
        if (!listen_ok)
        {
            Logger::LogError("Control plane failed to listen on port " + std::to_string(config.port) + ": " + error);
            server_impl.reset();
            return false;
        }
        ws.start(); // 后台线程 accept + 收发
        Logger::LogInfo("Control plane listening on ws://127.0.0.1:" + std::to_string(config.port));
        return true;
    }

    void control_plane_server::stop() noexcept
    {
        if (!server_impl)
        {
            return;
        }
        if (server_impl->ws_server)
        {
            server_impl->ws_server->stop();
        }
        server_impl.reset();
        ix::uninitNetSystem();
    }

    bool control_plane_server::running() const
    {
        return server_impl && server_impl->ws_server != nullptr;
    }

    std::size_t control_plane_server::client_count() const
    {
        if (!server_impl)
        {
            return 0;
        }
        std::lock_guard lock(server_impl->clients_mutex);
        return server_impl->clients.size();
    }

    control_plane_statistics control_plane_server::statistics() const
    {
        std::lock_guard lock(statistics_mutex);
        return counters;
    }

    void control_plane_server::handle_message(const std::string& client_id, const std::string& text)
    {
        const parse_result parsed = parse_request(text);
        if (parsed.status != parse_status::ok)
        {
            post_response(client_id, parsed.error_response);
            return;
        }

        const dispatch_result dispatched = dispatch_request(client_id, parsed.request);
        if (dispatched.outcome == dispatch_outcome::immediate_response)
        {
            post_response(client_id, dispatched.response);
            return;
        }

        std::lock_guard lock(command_mutex);
        if (pending_commands.size() >= max_pending_commands)
        {
            std::lock_guard stats_lock(statistics_mutex);
            ++counters.commands_dropped;
            // 队列满也要给出响应，避免客户端永久等待。
            // post_response 只取 clients_mutex/statistics_mutex，与 command_mutex 无嵌套死锁。
            const nlohmann::json busy =
                make_error(parsed.request.id, error_invalid_request, "Command queue is full; try again later");
            post_response(client_id, busy);
            return;
        }
        pending_commands.push_back(dispatched.command);
        std::lock_guard stats_lock(statistics_mutex);
        ++counters.commands_received;
    }

    std::vector<engine_command> control_plane_server::drain_commands()
    {
        std::lock_guard lock(command_mutex);
        std::vector<engine_command> drained(pending_commands.begin(), pending_commands.end());
        pending_commands.clear();
        return drained;
    }

    void control_plane_server::publish(const nlohmann::json& notification)
    {
        if (!server_impl)
        {
            return;
        }
        const std::string text = notification.dump();
        std::uint64_t failures = 0;
        std::size_t delivered  = 0;
        for (const auto& [client_id, client] : server_impl->client_snapshot())
        {
            send_payload(client, text, &failures);
            ++delivered;
        }
        std::lock_guard stats_lock(statistics_mutex);
        counters.telemetry_sent += delivered;
        counters.sends_failed += failures;
    }

    void control_plane_server::post_response(const std::string& client_id, const nlohmann::json& response)
    {
        if (!server_impl)
        {
            return;
        }
        std::uint64_t failures = 0;
        send_payload(server_impl->find_client(client_id), response.dump(), &failures);
        std::lock_guard stats_lock(statistics_mutex);
        ++counters.responses_sent;
        counters.sends_failed += failures;
    }

} // namespace control_plane
