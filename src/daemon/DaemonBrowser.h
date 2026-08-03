#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace ComputerCpp {

struct ManagedBrowserSession {
    bool ok = false;
    bool managed = true;
    std::string browser;
    std::string profile;
    std::string applicationName;
    std::string host = "127.0.0.1";
    int port = 0;
    int pid = -1;
    std::string code;
    std::string error;
};

ManagedBrowserSession ResolveManagedBrowserSession(
    const nlohmann::json& params,
    bool launch,
    bool includePid = false);

nlohmann::json RunBrowserEvalCommand(const nlohmann::json& params);

} // namespace ComputerCpp
