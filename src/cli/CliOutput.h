#pragma once

#include "CliOptions.h"

#include "computer_cpp/ControlSession.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace ComputerCpp::Cli {

void PrintCompact(const std::string& method, const nlohmann::json& response);
nlohmann::json ControlSessionCliError(const ControlSessionResult& result);
nlohmann::json ControlSessionCliOk(const ControlSessionResult& result, bool includeToken = true);
int PrintSessionCliResult(
    const CliOptions& options,
    const std::string& method,
    const ControlSessionResult& result,
    bool includeToken = true
);

} // namespace ComputerCpp::Cli
