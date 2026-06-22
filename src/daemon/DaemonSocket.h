#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ComputerCpp {

std::string ReadLineFromFd(int fd);
void WriteJsonLineToFd(int fd, const nlohmann::json& response);
void SetCloseOnExec(int fd);

bool IsSessionNameValid(const std::string& session);
std::filesystem::path SocketPathForSession(const std::string& session);
std::filesystem::path PidPathForSession(const std::string& session);
std::string PipeNameForSession(const std::string& session);
#if defined(_WIN32)
std::string ReadLineFromPipe(HANDLE pipe);
bool WriteAllToPipe(HANDLE pipe, std::string_view payload);
void WriteJsonLineToPipe(HANDLE pipe, const nlohmann::json& response);
#endif

} // namespace ComputerCpp
