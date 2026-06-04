#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace ComputerCpp {

std::string ReadLineFromFd(int fd);
void WriteJsonLineToFd(int fd, const nlohmann::json& response);
void SetCloseOnExec(int fd);

bool IsSessionNameValid(const std::string& session);
std::filesystem::path SocketPathForSession(const std::string& session);
std::filesystem::path PidPathForSession(const std::string& session);

} // namespace ComputerCpp
