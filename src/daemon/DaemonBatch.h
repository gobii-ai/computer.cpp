#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace ComputerCpp {

using DaemonRequestDispatcher = std::function<nlohmann::json(const std::string&, const nlohmann::json&)>;

nlohmann::json RunBatchCommand(
    const std::string& session,
    const nlohmann::json& request,
    const nlohmann::json& params,
    const DaemonRequestDispatcher& dispatch);

} // namespace ComputerCpp
