#pragma once

#include "CliOptions.h"

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

int HandleSessionMetrics(const CliOptions& options, const std::vector<std::string>& args);
int HandleSessionEvents(const CliOptions& options, const std::vector<std::string>& args);

} // namespace ComputerCpp::Cli
