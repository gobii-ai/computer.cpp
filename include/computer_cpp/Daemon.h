#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

struct DaemonOptions {
    std::string session = "default";
    bool foreground = true;
};

int RunDaemon(const DaemonOptions& options);
nlohmann::json HandleDaemonRequest(const std::string& session, const nlohmann::json& request);
std::filesystem::path SocketPathForSession(const std::string& session);
std::filesystem::path PidPathForSession(const std::string& session);
bool IsSessionNameValid(const std::string& session);

}
