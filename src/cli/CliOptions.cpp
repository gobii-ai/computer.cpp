#include "CliOptions.h"

#include "computer_cpp/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ComputerCpp::Cli {

std::string EnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::string();
}

bool EnvFlag(const char* name) {
    std::string value = EnvValue(name);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

void ApplyEnvDefaults(CliOptions& options) {
    if (options.controlSessionToken.empty()) {
        std::string token = EnvValue("COMPUTER_CPP_CONTROL_SESSION");
        if (!IsBlank(token)) {
            options.controlSessionToken = token;
        }
    }
    if (options.controlScope.empty() || options.controlScope == "desktop:local") {
        std::string scope = EnvValue("COMPUTER_CPP_CONTROL_SCOPE");
        if (!IsBlank(scope)) {
            options.controlScope = scope;
        }
    }
}

std::vector<std::string> ParseGlobalOptions(int argc, char** argv, CliOptions& options) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--session") {
            if (i + 1 >= argc) {
                options.parseError = "--session requires a value";
                break;
            }
            options.session = argv[++i];
        } else if (arg == "--control-session" || arg == "--control-session-token") {
            if (i + 1 >= argc) {
                options.parseError = arg + " requires a value";
                break;
            }
            options.controlSessionToken = argv[++i];
            if (IsBlank(options.controlSessionToken)) {
                options.parseError = arg + " requires a non-empty value";
                break;
            }
        } else if (arg == "--control-scope") {
            if (i + 1 >= argc) {
                options.parseError = "--control-scope requires a value";
                break;
            }
            options.controlScope = argv[++i];
            if (IsBlank(options.controlScope)) {
                options.parseError = "--control-scope requires a non-empty value";
                break;
            }
        } else if (arg == "--json") {
            options.jsonOutput = true;
        } else if (arg == "--no-start") {
            options.noStart = true;
        } else {
            args.push_back(arg);
        }
    }
    return args;
}

} // namespace ComputerCpp::Cli
