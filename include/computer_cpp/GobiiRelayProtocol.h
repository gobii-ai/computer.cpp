#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace ComputerCpp {

constexpr int kGobiiRelayProtocolVersion = 1;
constexpr size_t kGobiiRelayFrameLimit = 512 * 1024;

struct GobiiRelayRequest {
    std::string requestId;
    std::string app;
    std::chrono::system_clock::time_point deadline;
    nlohmann::json payload;
};

bool ParseGobiiRelayRequest(
    const std::string& serialized,
    GobiiRelayRequest& request,
    std::string& code,
    std::string& error);

nlohmann::json GobiiRelayResponse(
    const std::string& requestId,
    nlohmann::json payload);
nlohmann::json GobiiRelayError(
    const std::string& requestId,
    const std::string& code,
    const std::string& message,
    nlohmann::json details = nlohmann::json::object());

class GobiiRequestLedger {
public:
    enum class StartResult {
        Started,
        RunningDuplicate,
        CompletedDuplicate,
        CapacityExceeded,
    };

    struct Entry {
        enum class State { Running, Completed, Failed };
        State state = State::Running;
        std::string serializedResponse;
        std::chrono::steady_clock::time_point expiresAt;
    };

    explicit GobiiRequestLedger(
        size_t capacity = 256,
        std::chrono::minutes ttl = std::chrono::minutes(15));
    StartResult Start(
        const std::string& requestId,
        std::string* cachedResponse = nullptr);
    void Complete(
        const std::string& requestId,
        std::string serializedResponse,
        bool failed);
    std::optional<Entry> Find(const std::string& requestId);
    size_t Size() const;

private:
    void PruneLocked(std::chrono::steady_clock::time_point now);

    size_t capacity_;
    std::chrono::minutes ttl_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> order_;
};

} // namespace ComputerCpp
