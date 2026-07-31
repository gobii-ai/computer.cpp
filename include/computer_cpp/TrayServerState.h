#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

struct TrayAppServerState {
    int version = 1;
    bool configured = false;
    long pid = 0;
    std::string host;
    int port = 0;
    std::string url;
    std::string appPath;
    std::string appId;
    std::string configName;
    std::string displayName;
    std::string startedAt;
    std::string internalControlToken;
};

// Singleton configured-server state path. Version 1 files remain readable for
// migration from the legacy single-app server.
std::filesystem::path TrayAppServerStatePath();
std::filesystem::path TrayAppServerStateDirectory();
std::filesystem::path TrayAppServerStatePath(const std::string& configName);
std::vector<std::filesystem::path> ListTrayAppServerStatePaths(std::string* error = nullptr);
bool SaveTrayAppServerState(const TrayAppServerState& state, const std::filesystem::path& path, std::string* error = nullptr);
std::optional<TrayAppServerState> LoadTrayAppServerState(const std::filesystem::path& path, std::string* error = nullptr);
bool RemoveTrayAppServerState(const std::filesystem::path& path, std::string* error = nullptr);
bool RemoveTrayAppServerStateForPid(const std::filesystem::path& path, long pid, std::string* error = nullptr);
bool IsProcessAlive(long pid);

} // namespace ComputerCpp
