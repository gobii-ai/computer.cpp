#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <mutex>
#include <string>

namespace ComputerCpp {

class DaemonObservedEvent {
public:
    DaemonObservedEvent(
        bool enabled,
        const std::string& session,
        std::string name,
        const nlohmann::json& params,
        const nlohmann::json& controlGate
    );

    DaemonObservedEvent(const DaemonObservedEvent&) = delete;
    DaemonObservedEvent& operator=(const DaemonObservedEvent&) = delete;

    ~DaemonObservedEvent();

    [[nodiscard]] int64_t id() const;

    void Capture(const std::string& label);
    void Finish();
    void AddTo(nlohmann::json& data) const;

private:
    bool enabled_;
    const std::string& session_;
    int64_t eventId_;
    nlohmann::json frames_ = nlohmann::json::array();
    mutable std::mutex framesMutex_;
};

} // namespace ComputerCpp
