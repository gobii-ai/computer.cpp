#pragma once

#include <nlohmann/json_fwd.hpp>

namespace ComputerCpp {

nlohmann::json RunBrowserEvalCommand(const nlohmann::json& params);

} // namespace ComputerCpp
