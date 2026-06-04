#pragma once

#include "CliOptions.h"

#include "computer_cpp/LuaRunner.h"

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

bool ParseLuaRunOptions(
    const std::vector<std::string>& args,
    const CliOptions& options,
    const std::string& executablePath,
    LuaRunOptions& runOptions,
    std::string& error
);

std::vector<std::string> BuildLuaRunChildCommand(const CliOptions& options, const LuaRunOptions& runOptions);

} // namespace ComputerCpp::Cli
