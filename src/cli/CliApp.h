#pragma once

#include "CliOptions.h"

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

bool IsSemanticAppCommand(const std::vector<std::string>& args);
int HandleSemanticAppCommand(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath
);

} // namespace ComputerCpp::Cli
