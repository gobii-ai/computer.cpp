#pragma once

#include "computer_cpp/Platform.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace ComputerCpp {

std::set<std::string> VisibleWindowIds(const std::vector<Platform::WindowInfo>& windows);
nlohmann::json RunPermissionsCommand(const nlohmann::json& params);
nlohmann::json RunOpenPermissionsCommand(const nlohmann::json& params);
nlohmann::json RunStateCommand(const std::string& session);
nlohmann::json RunWindowBoundsCommand(const nlohmann::json& params);
nlohmann::json RunWindowActiveCommand();
nlohmann::json RunWindowListCommand(const nlohmann::json& params);
nlohmann::json RunWindowCloseCommand(const nlohmann::json& params, const std::string& activeControlToken);
nlohmann::json RunAppLaunchCommand(const nlohmann::json& params, const std::string& activeControlToken);
nlohmann::json RunAppActivatePidCommand(const nlohmann::json& params);
nlohmann::json RunAppActiveCommand();
nlohmann::json RunOpenUrlCommand(const nlohmann::json& params, const std::string& activeControlToken);

} // namespace ComputerCpp
