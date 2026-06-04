#pragma once

#include "CliOptions.h"

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

int HandleConfigCommand(const CliOptions& options, const std::vector<std::string>& args);

} // namespace ComputerCpp::Cli
