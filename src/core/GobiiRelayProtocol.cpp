#include "computer_cpp/GobiiRelayProtocol.h"
#include "computer_cpp/GobiiTypes.h"

using json = nlohmann::json;

namespace ComputerCpp {
namespace {

bool IsValidIdentifier(const std::string& value) {
    if (value.empty() || value.size() > 200) {
        return false;
    }
    for (unsigned char ch : value) {
        if (ch < 0x21 || ch > 0x7e) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ParseGobiiRelayRequest(
    const std::string& serialized,
    GobiiRelayRequest& request,
    std::string& code,
    std::string& error
) {
    code = "invalid_request";
    error.clear();
    if (serialized.size() > kGobiiRelayFrameLimit) {
        code = "payload_too_large";
        error = "relay frame exceeds 512 KiB";
        return false;
    }
    const json value = json::parse(serialized, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        error = "relay frame must be a JSON object";
        return false;
    }
    if (value.value("type", "") != "mcp.request") {
        error = "relay frame is not an mcp.request";
        return false;
    }
    if (!value.contains("request_id") ||
        !value["request_id"].is_string() ||
        !value.contains("app") ||
        !value["app"].is_string() ||
        !value.contains("deadline_ms") ||
        !value["deadline_ms"].is_number_integer() ||
        !value.contains("payload") ||
        !value["payload"].is_object()) {
        error = "relay request is missing required fields";
        return false;
    }
    request.requestId = value["request_id"].get<std::string>();
    request.app = value["app"].get<std::string>();
    if (!IsValidIdentifier(request.requestId) ||
        !IsValidIdentifier(request.app)) {
        error = "request_id or app is invalid";
        return false;
    }
    const auto deadlineMs =
        value["deadline_ms"].get<long long>();
    if (deadlineMs <= 0 || deadlineMs > 3'600'000) {
        error = "deadline_ms must be between 1 and 3600000";
        return false;
    }
    request.deadline =
        std::chrono::system_clock::now() +
        std::chrono::milliseconds(deadlineMs);
    request.payload = value["payload"];
    if (request.payload.value("jsonrpc", "") != "2.0" ||
        !request.payload.contains("id") ||
        !request.payload.contains("method") ||
        !request.payload["method"].is_string() ||
        (request.payload.contains("params") &&
         !request.payload["params"].is_object())) {
        error = "payload must be one JSON-RPC 2.0 request object";
        return false;
    }
    return true;
}

json GobiiRelayResponse(
    const std::string& requestId,
    json payload
) {
    return {
        {"type", "mcp.response"},
        {"request_id", requestId},
        {"payload", std::move(payload)},
    };
}

json GobiiRelayError(
    const std::string& requestId,
    const std::string& code,
    const std::string& message,
    json details
) {
    json error = {
        {"code", code},
        {"message", message},
    };
    if (!details.empty()) {
        error["details"] = std::move(details);
    }
    return {
        {"type", "mcp.response"},
        {"request_id", requestId},
        {"error", std::move(error)},
    };
}

GobiiRequestLedger::GobiiRequestLedger(
    size_t capacity,
    std::chrono::minutes ttl
) : capacity_(capacity), ttl_(ttl) {}

void GobiiRequestLedger::PruneLocked(
    std::chrono::steady_clock::time_point now
) {
    for (auto it = order_.begin(); it != order_.end();) {
        auto entry = entries_.find(*it);
        if (entry == entries_.end()) {
            it = order_.erase(it);
            continue;
        }
        const bool expired =
            entry->second.state != Entry::State::Running &&
            entry->second.expiresAt <= now;
        const bool overCapacity =
            entries_.size() > capacity_ &&
            entry->second.state != Entry::State::Running;
        if (expired || overCapacity) {
            entries_.erase(entry);
            it = order_.erase(it);
        } else {
            ++it;
        }
    }
}

GobiiRequestLedger::StartResult GobiiRequestLedger::Start(
    const std::string& requestId,
    std::string* cachedResponse
) {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    PruneLocked(now);
    auto existing = entries_.find(requestId);
    if (existing != entries_.end()) {
        if (existing->second.state == Entry::State::Running) {
            return StartResult::RunningDuplicate;
        }
        if (cachedResponse) {
            *cachedResponse = existing->second.serializedResponse;
        }
        return StartResult::CompletedDuplicate;
    }
    Entry entry;
    entry.expiresAt = now + ttl_;
    entries_.emplace(requestId, std::move(entry));
    order_.push_back(requestId);
    PruneLocked(now);
    return StartResult::Started;
}

void GobiiRequestLedger::Complete(
    const std::string& requestId,
    std::string serializedResponse,
    bool failed
) {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(requestId);
    if (it == entries_.end()) {
        Entry entry;
        entry.state = failed
            ? Entry::State::Failed
            : Entry::State::Completed;
        entry.serializedResponse = std::move(serializedResponse);
        entry.expiresAt =
            std::chrono::steady_clock::now() + ttl_;
        entries_.emplace(requestId, std::move(entry));
        order_.push_back(requestId);
    } else {
        it->second.state = failed
            ? Entry::State::Failed
            : Entry::State::Completed;
        it->second.serializedResponse =
            std::move(serializedResponse);
        it->second.expiresAt =
            std::chrono::steady_clock::now() + ttl_;
    }
    PruneLocked(std::chrono::steady_clock::now());
}

std::optional<GobiiRequestLedger::Entry>
GobiiRequestLedger::Find(const std::string& requestId) {
    std::lock_guard lock(mutex_);
    PruneLocked(std::chrono::steady_clock::now());
    const auto it = entries_.find(requestId);
    return it == entries_.end()
        ? std::nullopt
        : std::optional<Entry>(it->second);
}

size_t GobiiRequestLedger::Size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

} // namespace ComputerCpp
