#pragma once

#include <nlohmann/json.hpp>

namespace ComputerCpp {

nlohmann::json RunScreenshotCommand(const nlohmann::json& params);

} // namespace ComputerCpp
