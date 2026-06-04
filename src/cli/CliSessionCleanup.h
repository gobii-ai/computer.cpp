#pragma once

#include "CliOptions.h"

#include <string>

namespace ComputerCpp::Cli {

bool CleanupControlSessionResources(const CliOptions& options, const std::string& token, const std::string& scope);
bool CleanupExpiredControlSessionResources(const CliOptions& options, const std::string& cleanupToken, const std::string& scope);

}
