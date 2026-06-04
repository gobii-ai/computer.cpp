#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

bool IsControlSessionCommand(const std::string& method);
nlohmann::json RunControlSessionCommand(const std::string& session, const std::string& method, const nlohmann::json& params);

}
