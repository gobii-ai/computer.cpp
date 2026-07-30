#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace ComputerCpp {

struct GobiiRelayConnectOptions {
    std::string url;
    std::string accessToken;
    std::string userAgent;
};

class GobiiRelayClient {
public:
    using MessageHandler =
        std::function<void(const std::string&)>;
    using DisconnectHandler =
        std::function<void(const std::string&)>;
    using HeartbeatHandler = std::function<void()>;

    GobiiRelayClient();
    ~GobiiRelayClient();

    GobiiRelayClient(const GobiiRelayClient&) = delete;
    GobiiRelayClient& operator=(const GobiiRelayClient&) = delete;

    bool Start(
        GobiiRelayConnectOptions options,
        MessageHandler onMessage,
        DisconnectHandler onDisconnect,
        HeartbeatHandler onHeartbeat,
        std::string& error);
    bool Send(std::string message);
    void Stop();
    bool Running() const;

private:
    void Run(std::stop_token stop);

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    GobiiRelayConnectOptions options_;
    MessageHandler onMessage_;
    DisconnectHandler onDisconnect_;
    HeartbeatHandler onHeartbeat_;
    std::deque<std::string> outgoing_;
    std::jthread thread_;
    bool running_ = false;
};

} // namespace ComputerCpp
