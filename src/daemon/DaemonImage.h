#pragma once

#include <nlohmann/json.hpp>

namespace ComputerCpp {

nlohmann::json RunImageInfo(const nlohmann::json& params);
nlohmann::json RunImageSplit(const nlohmann::json& params);

} // namespace ComputerCpp
