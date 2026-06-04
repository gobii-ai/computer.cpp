#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ComputerCpp::Cli {

int RunChildWithControlSession(
    const std::vector<std::string>& command,
    const std::string& token,
    const std::string& scope,
    int64_t ttlMs,
    int64_t maxMs
);

} // namespace ComputerCpp::Cli
