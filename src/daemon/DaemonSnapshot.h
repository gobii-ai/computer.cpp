#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

nlohmann::json RunGetCommand(const std::string& session, const nlohmann::json& params);
nlohmann::json RunSnapshotCommand(const std::string& session, const nlohmann::json& params);

} // namespace ComputerCpp
