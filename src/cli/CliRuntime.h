#pragma once

#include "CliOptions.h"

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

std::string ExecutablePath(char* argv0);
bool RuntimeCheckExempt(const std::vector<std::string>& args);
int VerifyDaemonRuntimeOrExit(const CliOptions& options, const std::string& executablePath);

} // namespace ComputerCpp::Cli
