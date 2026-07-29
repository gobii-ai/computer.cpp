#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ComputerCpp {

struct TrayAppServerState {
    long pid = 0;
    std::string host;
    int port = 0;
    std::string url;
    std::string appPath;
    std::string appId;
    std::string configName;
    std::string displayName;
    std::string startedAt;
};

// Legacy single-server state path. Kept for migration from older releases.
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
