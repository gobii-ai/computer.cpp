#pragma once

#include "computer_cpp/AppConfig.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace ComputerCpp {

struct ManagedBrowserSession {
    bool ok = false;
    bool managed = true;
    bool launched = false;
    bool proxyConfigured = false;
    std::string browser;
    std::string profile;
    std::string applicationName;
    std::string windowQuery;
    std::string host = "127.0.0.1";
    int port = 0;
    int pid = -1;
    std::string code;
    std::string error;
};

AppConfig LoadDaemonAppConfig(std::string* error);

std::optional<int> ReadManagedBrowserCompatibilityPort(
    const std::filesystem::path& userDataDir);
bool WriteManagedBrowserCompatibilityPort(
    const std::filesystem::path& userDataDir,
    int port);
void RemoveManagedBrowserCompatibilityPort(
    const std::filesystem::path& userDataDir);

ManagedBrowserSession ResolveManagedBrowserSession(
    const nlohmann::json& params,
    bool launch,
    bool includePid = false);

nlohmann::json RunBrowserEvalCommand(const nlohmann::json& params);

} // namespace ComputerCpp
