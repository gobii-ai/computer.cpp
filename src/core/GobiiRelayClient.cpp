#include "computer_cpp/GobiiRelayClient.h"

#include "computer_cpp/GobiiApiClient.h"
#include "computer_cpp/GobiiRelayProtocol.h"

#include "CurlHandle.h"

#include <array>
#include <chrono>
#include <curl/curl.h>
#include <thread>

namespace ComputerCpp {

GobiiRelayClient::GobiiRelayClient() = default;

GobiiRelayClient::~GobiiRelayClient() {
    Stop();
}

bool GobiiRelayClient::Start(
    GobiiRelayConnectOptions options,
    MessageHandler onMessage,
    DisconnectHandler onDisconnect,
    HeartbeatHandler onHeartbeat,
    std::string& error
) {
    Stop();
    if (!GobiiWebSocketRuntimeSupported(&error)) {
        return false;
    }
    if (options.url.rfind("wss://", 0) != 0) {
#if defined(COMPUTER_CPP_GOBII_DEV_INLINE_IMAGES)
        if (options.url.rfind("ws://", 0) != 0) {
            error = "relay URL must use wss";
            return false;
        }
#else
        error = "relay URL must use wss";
        return false;
#endif
    }
    {
        std::lock_guard lock(mutex_);
        options_ = std::move(options);
        onMessage_ = std::move(onMessage);
        onDisconnect_ = std::move(onDisconnect);
        onHeartbeat_ = std::move(onHeartbeat);
        outgoing_.clear();
        running_ = true;
    }
    thread_ = std::jthread(
        [this](std::stop_token stop) { Run(stop); });
    return true;
}

bool GobiiRelayClient::Send(std::string message) {
    if (message.size() > kGobiiRelayFrameLimit) {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!running_) {
        return false;
    }
    outgoing_.push_back(std::move(message));
    condition_.notify_all();
    return true;
}

void GobiiRelayClient::Stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        condition_.notify_all();
        if (thread_.get_id() != std::this_thread::get_id()) {
            thread_.join();
        }
    }
    std::lock_guard lock(mutex_);
    running_ = false;
    outgoing_.clear();
}

bool GobiiRelayClient::Running() const {
    std::lock_guard lock(mutex_);
    return running_;
}

void GobiiRelayClient::Run(std::stop_token stop) {
#if LIBCURL_VERSION_NUM >= 0x075600
    GobiiRelayConnectOptions options;
    {
        std::lock_guard lock(mutex_);
        options = options_;
    }
    CurlHandle curl;
    CurlHeaders headers;
    headers.append("Authorization: Bearer " + options.accessToken);
    headers.append(
        "Sec-WebSocket-Protocol: gobii-computer-relay.v1");
    curl_easy_setopt(curl.get(), CURLOPT_URL, options.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, options.userAgent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 20000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    CURLcode code = curl_easy_perform(curl.get());
    std::string disconnectError;
    if (code != CURLE_OK) {
        disconnectError =
            std::string("relay connection failed: ") +
            curl_easy_strerror(code);
    } else {
        auto lastTraffic = std::chrono::steady_clock::now();
        auto nextPing =
            lastTraffic + std::chrono::seconds(25);
        std::string incoming;
        std::array<char, 16384> buffer{};
        while (!stop.stop_requested()) {
            std::string outgoing;
            {
                std::lock_guard lock(mutex_);
                if (!outgoing_.empty()) {
                    outgoing = std::move(outgoing_.front());
                    outgoing_.pop_front();
                }
            }
            if (!outgoing.empty()) {
                size_t sent = 0;
                code = curl_ws_send(
                    curl.get(),
                    outgoing.data(),
                    outgoing.size(),
                    &sent,
                    0,
                    CURLWS_TEXT);
                if (code != CURLE_OK || sent != outgoing.size()) {
                    disconnectError = "relay send failed";
                    break;
                }
                lastTraffic = std::chrono::steady_clock::now();
            }

            const curl_ws_frame* meta = nullptr;
            size_t received = 0;
            code = curl_ws_recv(
                curl.get(),
                buffer.data(),
                buffer.size(),
                &received,
                &meta);
            if (code == CURLE_OK && meta) {
                lastTraffic = std::chrono::steady_clock::now();
                HeartbeatHandler heartbeat;
                {
                    std::lock_guard lock(mutex_);
                    heartbeat = onHeartbeat_;
                }
                if (heartbeat) heartbeat();
                if ((meta->flags & CURLWS_TEXT) != 0 ||
                    !incoming.empty()) {
                    if (incoming.size() + received >
                        kGobiiRelayFrameLimit) {
                        disconnectError =
                            "relay frame exceeded size limit";
                        break;
                    }
                    incoming.append(buffer.data(), received);
                    if (meta->bytesleft == 0) {
                        MessageHandler handler;
                        {
                            std::lock_guard lock(mutex_);
                            handler = onMessage_;
                        }
                        if (handler) handler(incoming);
                        incoming.clear();
                    }
                }
            } else if (code != CURLE_AGAIN) {
                disconnectError =
                    std::string("relay receive failed: ") +
                    curl_easy_strerror(code);
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= nextPing) {
                size_t sent = 0;
                code = curl_ws_send(
                    curl.get(), "", 0, &sent, 0, CURLWS_PING);
                if (code != CURLE_OK) {
                    disconnectError = "relay ping failed";
                    break;
                }
                nextPing = now + std::chrono::seconds(25);
            }
            if (now - lastTraffic >= std::chrono::seconds(60)) {
                disconnectError = "relay heartbeat timed out";
                break;
            }
            std::unique_lock lock(mutex_);
            condition_.wait_for(
                lock,
                stop,
                std::chrono::milliseconds(50),
                [this] { return !outgoing_.empty(); });
        }
    }
    DisconnectHandler disconnect;
    {
        std::lock_guard lock(mutex_);
        running_ = false;
        disconnect = onDisconnect_;
    }
    if (!stop.stop_requested() && disconnect) {
        disconnect(
            disconnectError.empty()
                ? "relay disconnected"
                : disconnectError);
    }
#else
    (void)stop;
#endif
}

} // namespace ComputerCpp
