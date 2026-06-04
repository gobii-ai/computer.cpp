#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace ComputerCpp {

bool IsTargetCommand(const std::string& method);
nlohmann::json RunTargetCommand(const std::string& session, const std::string& method, const nlohmann::json& params);

} // namespace ComputerCpp
