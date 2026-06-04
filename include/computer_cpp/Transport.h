#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

struct DaemonStartResult {
    bool connectedExisting = false;
    bool started = false;
    std::string error;
};

DaemonStartResult EnsureDaemon(const std::string& session, const std::string& executablePath);
nlohmann::json SendDaemonRequest(const std::string& session, const nlohmann::json& request);
bool IsDaemonReady(const std::string& session);
bool StopDaemon(const std::string& session);

}
