#include "CliRunCommand.h"

#include "CliSession.h"
#include "CliSessionParsing.h"

#include "computer_cpp/StringUtils.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace ComputerCpp::Cli {
namespace {

bool ParseVarAssignment(const std::string& input, std::string& key, std::string& value) {
    const size_t eq = input.find('=');
    if (eq == std::string::npos || eq == 0) {
        return false;
    }
    key = input.substr(0, eq);
    value = input.substr(eq + 1);
    return !key.empty();
}

bool RequireOptionValue(
    const std::vector<std::string>& args,
    size_t i,
    const std::string& optionName,
    std::string& error
) {
    if (i + 1 >= args.size()) {
        error = optionName + " requires a value";
        return false;
    }
    if (IsBlank(args[i + 1])) {
        error = optionName + " requires a non-empty value";
        return false;
    }
    return true;
}

bool ParseCliDuration(
    const std::string& value,
    int64_t& out,
    std::string& error,
    const std::string& optionName,
    bool millisecondsOnly
) {
    auto parsed = ParseDurationOption(value, millisecondsOnly);
    if (!parsed) {
        error = optionName + " requires a valid duration";
        return false;
    }
    out = *parsed;
    return true;
}

void AppendVars(std::vector<std::string>& command, const nlohmann::json& vars) {
    if (!vars.is_object()) {
        return;
    }
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        command.push_back("--var");
        command.push_back(it.key() + "=" + (it.value().is_string() ? it.value().get<std::string>() : it.value().dump()));
    }
}

} // namespace

bool ParseLuaRunOptions(
    const std::vector<std::string>& args,
    const CliOptions& options,
    const std::string& executablePath,
    LuaRunOptions& runOptions,
    std::string& error
) {
    runOptions.session = options.session;
    runOptions.controlSessionToken = options.controlSessionToken;
    runOptions.controlScope = options.controlScope;
    runOptions.executablePath = executablePath;
    runOptions.jsonOutput = options.jsonOutput;
    bool afterDoubleDash = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (!afterDoubleDash && arg == "--") {
            afterDoubleDash = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--dry-run") {
            runOptions.dryRun = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--agent-stdio") {
            runOptions.agentStdio = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--owner") {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            runOptions.leaseOwner = args[++i];
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--purpose") {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            runOptions.leasePurpose = args[++i];
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash && (arg == "--ttl" || arg == "--ttl-ms")) {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            if (!ParseCliDuration(args[++i], runOptions.leaseTtlMs, error, arg, arg == "--ttl-ms")) {
                return false;
            }
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash && (arg == "--wait" || arg == "--wait-ms")) {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            if (!ParseCliDuration(args[++i], runOptions.leaseWaitMs, error, arg, arg == "--wait-ms")) {
                return false;
            }
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash &&
            (arg == "--max" || arg == "--max-ms" || arg == "--max-runtime" || arg == "--max-runtime-ms")) {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            if (!ParseCliDuration(
                    args[++i],
                    runOptions.leaseMaxRuntimeMs,
                    error,
                    arg,
                    arg == "--max-ms" || arg == "--max-runtime-ms")) {
                return false;
            }
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--no-max") {
            runOptions.leaseMaxRuntimeMs = 0;
            runOptions.acquireControlSession = true;
            continue;
        }
        if (!afterDoubleDash && arg == "--var") {
            if (!RequireOptionValue(args, i, arg, error)) {
                return false;
            }
            std::string key;
            std::string value;
            if (!ParseVarAssignment(args[++i], key, value)) {
                error = "--var requires key=value";
                return false;
            }
            runOptions.vars[key] = value;
            continue;
        }
        if (!afterDoubleDash && arg.rfind("--var=", 0) == 0) {
            std::string key;
            std::string value;
            if (!ParseVarAssignment(arg.substr(6), key, value)) {
                error = "--var requires key=value";
                return false;
            }
            runOptions.vars[key] = value;
            continue;
        }
        if (runOptions.scriptPath.empty()) {
            runOptions.scriptPath = arg;
        } else {
            runOptions.scriptArgs.push_back(arg);
        }
    }
    if (runOptions.scriptPath.empty()) {
        error = "run requires a Lua script path";
        return false;
    }
    return true;
}

std::vector<std::string> BuildLuaRunChildCommand(const CliOptions& options, const LuaRunOptions& runOptions) {
    std::vector<std::string> command;
    command.push_back(runOptions.executablePath.string());
    command.push_back("--session");
    command.push_back(options.session);
    command.push_back("--control-scope");
    command.push_back(options.controlScope);
    command.push_back("--no-start");
    if (options.jsonOutput) {
        command.push_back("--json");
    }
    command.push_back("run");
    if (runOptions.dryRun) {
        command.push_back("--dry-run");
    }
    if (runOptions.agentStdio) {
        command.push_back("--agent-stdio");
    }
    AppendVars(command, runOptions.vars);
    command.push_back(runOptions.scriptPath.string());
    if (!runOptions.scriptArgs.empty()) {
        command.push_back("--");
        command.insert(command.end(), runOptions.scriptArgs.begin(), runOptions.scriptArgs.end());
    }
    return command;
}

} // namespace ComputerCpp::Cli
