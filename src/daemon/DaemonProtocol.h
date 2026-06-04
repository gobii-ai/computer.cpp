#pragma once

#include "computer_cpp/ControlSession.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace ComputerCpp {

using json = nlohmann::json;

json Error(const std::string& message, const std::string& code = "error");
json ErrorWithData(const std::string& message, const std::string& code, json data);
json Ok(json data = json::object());

bool MethodRequiresControlSession(const std::string& method, const json& params);
std::string ControlSessionTokenFromRequest(const json& request, const json& params);
std::optional<std::string> InvalidControlSessionTokenError(const json& request, const json& params);
std::optional<std::string> InvalidControlScopeError(const json& params);
std::string ControlScopeFromParams(const json& params);
json ControlSessionError(const ControlSessionResult& result);
json ControlSessionResultOk(const ControlSessionResult& result, bool includeToken = true);
json RequireControlSessionForRequest(const json& request, const std::string& method, const json& params);
json TimelineParamsWithControlSession(const json& params, const json& controlGate);

} // namespace ComputerCpp
