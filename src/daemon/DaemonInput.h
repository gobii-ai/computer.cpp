#pragma once

#include "DaemonTextInput.h"

#include <nlohmann/json.hpp>

#include <string>

namespace ComputerCpp {

nlohmann::json RunClickCommand(const std::string& session, const nlohmann::json& params);
nlohmann::json RunMouseMoveCommand(const std::string& session, const nlohmann::json& params, const nlohmann::json& controlGate);
nlohmann::json RunMouseDragCommand(const std::string& session, const nlohmann::json& params, const nlohmann::json& controlGate);
nlohmann::json RunMouseDownCommand(const nlohmann::json& params);
nlohmann::json RunMouseUpCommand(const nlohmann::json& params);
nlohmann::json RunScrollCommand(const std::string& session, const nlohmann::json& params, const nlohmann::json& controlGate);

} // namespace ComputerCpp
