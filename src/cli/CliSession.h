#pragma once

#include "CliOptions.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace ComputerCpp::Cli {

int64_t ParseDurationMs(std::string_view value);
int HandleSessionCommand(const CliOptions& options, const std::vector<std::string>& args);
int RunManagedCommandWithControlSession(
    const CliOptions& options,
    const nlohmann::json& acquireParams,
    const std::vector<std::string>& command,
    int64_t maxMs
);

} // namespace ComputerCpp::Cli
