#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ComputerCpp {

struct TrayAppServerState {
    long pid = 0;
    std::string host;
    int port = 0;
    std::string url;
    std::string appPath;
    std::string appId;
    std::string displayName;
    std::string startedAt;
};

std::filesystem::path TrayAppServerStatePath();
bool SaveTrayAppServerState(const TrayAppServerState& state, const std::filesystem::path& path, std::string* error = nullptr);
std::optional<TrayAppServerState> LoadTrayAppServerState(const std::filesystem::path& path, std::string* error = nullptr);
bool RemoveTrayAppServerState(const std::filesystem::path& path, std::string* error = nullptr);
bool RemoveTrayAppServerStateForPid(const std::filesystem::path& path, long pid, std::string* error = nullptr);
bool IsProcessAlive(long pid);

} // namespace ComputerCpp
