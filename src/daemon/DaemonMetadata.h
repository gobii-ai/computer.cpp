#pragma once

#include "computer_cpp/Platform.h"

#include <nlohmann/json.hpp>

namespace ComputerCpp {

nlohmann::json CapabilitiesJson();
nlohmann::json SchemaJson();
nlohmann::json PermissionToJson(const Platform::PermissionStatus& status);

} // namespace ComputerCpp
