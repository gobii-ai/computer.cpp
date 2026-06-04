#pragma once

#include <string>
#include <vector>

namespace ComputerCpp::Cli {

struct CliOptions {
    std::string session = "default";
    std::string controlSessionToken;
    std::string controlScope = "desktop:local";
    std::string parseError;
    bool jsonOutput = false;
    bool noStart = false;
};

std::string EnvValue(const char* name);
bool EnvFlag(const char* name);
void ApplyEnvDefaults(CliOptions& options);
std::vector<std::string> ParseGlobalOptions(int argc, char** argv, CliOptions& options);

} // namespace ComputerCpp::Cli
