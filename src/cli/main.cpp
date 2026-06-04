#include "computer_cpp/Daemon.h"
#include "computer_cpp/ControlSession.h"
#include "computer_cpp/LuaRunner.h"
#include "computer_cpp/Transport.h"

#include "CliCommands.h"
#include "CliApp.h"
#include "CliConfig.h"
#include "CliOptions.h"
#include "CliOutput.h"
#include "CliRuntime.h"
#include "CliRunCommand.h"
#include "CliSession.h"
#include "CliUsage.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {

using ComputerCpp::Cli::CliOptions;
using ComputerCpp::Cli::PrintCompact;

json MakeRequest(const std::string& method, json params = json::object()) {
    return {
        {"id", method},
        {"method", method},
        {"params", std::move(params)}
    };
}

void AddControlSessionParams(json& params, const CliOptions& options) {
    if (!options.controlSessionToken.empty()) {
        params["controlSession"] = options.controlSessionToken;
    }
    if (!options.controlScope.empty()) {
        params["controlScope"] = options.controlScope;
    }
}

bool BatchResponseHasFailedSteps(const json& response) {
    if (!response.value("ok", false)) {
        return false;
    }
    const json data = response.value("data", json::object());
    if (!data.contains("failed") || !data.at("failed").is_number_integer()) {
        return false;
    }
    return data.at("failed").get<int>() > 0;
}

std::string ReadAllStdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

int ErrorExit(const std::string& message, int code = 1) {
    std::cerr << "Error: " << message << std::endl;
    return code;
}

int EnsureReadyDaemonForCliCommand(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& executablePath,
    bool verifyRuntime = true
) {
    if (!options.noStart) {
        auto start = ComputerCpp::EnsureDaemon(options.session, executablePath);
        if (!start.error.empty()) {
            return ErrorExit(start.error);
        }
    } else if (!ComputerCpp::IsDaemonReady(options.session)) {
        return ErrorExit("daemon is not running", 1);
    }

    if (verifyRuntime && !ComputerCpp::Cli::RuntimeCheckExempt(args)) {
        if (int verify = ComputerCpp::Cli::VerifyDaemonRuntimeOrExit(options, executablePath); verify != 0) {
            return verify;
        }
    }
    return 0;
}

}

int main(int argc, char** argv) {
    CliOptions options;
    auto args = ComputerCpp::Cli::ParseGlobalOptions(argc, argv, options);
    ComputerCpp::Cli::ApplyEnvDefaults(options);
    if (!options.parseError.empty()) {
        return ErrorExit(options.parseError, 2);
    }
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        ComputerCpp::Cli::PrintUsage();
        return 0;
    }

    if (!ComputerCpp::IsSessionNameValid(options.session)) {
        return ErrorExit("invalid session name", 2);
    }
    std::string executablePath = ComputerCpp::Cli::ExecutablePath(argv[0]);

    if (args[0] == "daemon") {
        ComputerCpp::DaemonOptions daemonOptions;
        daemonOptions.session = options.session;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--session" && i + 1 < args.size()) {
                daemonOptions.session = args[++i];
            }
        }
        return ComputerCpp::RunDaemon(daemonOptions);
    }

    if (args[0] == "open" && args.size() == 1) {
        auto start = ComputerCpp::EnsureDaemon(options.session, executablePath);
        if (!start.error.empty()) {
            return ErrorExit(start.error);
        }
        if (int verify = ComputerCpp::Cli::VerifyDaemonRuntimeOrExit(options, executablePath); verify != 0) {
            return verify;
        }
        std::cout << (start.connectedExisting ? "daemon already running" : "daemon started") << "\n";
        return 0;
    }

    if (args[0] == "close") {
        bool stopped = ComputerCpp::StopDaemon(options.session);
        std::cout << (stopped ? "daemon stopping" : "daemon was not reachable") << "\n";
        return stopped ? 0 : 1;
    }

    if (args[0] == "session") {
        if (int ready = EnsureReadyDaemonForCliCommand(options, args, executablePath); ready != 0) {
            return ready;
        }
        return ComputerCpp::Cli::HandleSessionCommand(options, args);
    }

    if (args[0] == "config") {
        return ComputerCpp::Cli::HandleConfigCommand(options, args);
    }

    if (args[0] == "run") {
        if (args.size() > 1 && (args[1] == "--help" || args[1] == "-h")) {
            ComputerCpp::Cli::PrintRunUsage();
            return 0;
        }
        ComputerCpp::LuaRunOptions runOptions;
        std::string error;
        if (!ComputerCpp::Cli::ParseLuaRunOptions(args, options, executablePath, runOptions, error)) {
            return ErrorExit(error, 2);
        }
        if (!runOptions.controlSessionToken.empty()) {
            runOptions.acquireControlSession = false;
        }
        if (!runOptions.dryRun && runOptions.controlSessionToken.empty() && runOptions.acquireControlSession) {
            if (int ready = EnsureReadyDaemonForCliCommand(options, args, executablePath); ready != 0) {
                return ready;
            }
            json acquireParams = {
                {"scope", options.controlScope},
                {"daemonSession", options.session},
                {"owner", runOptions.leaseOwner},
                {"purpose", runOptions.leasePurpose},
                {"ttlMs", runOptions.leaseTtlMs},
                {"waitMs", runOptions.leaseWaitMs},
                {"maxRuntimeMs", runOptions.leaseMaxRuntimeMs}
            };
            return ComputerCpp::Cli::RunManagedCommandWithControlSession(
                options,
                acquireParams,
                ComputerCpp::Cli::BuildLuaRunChildCommand(options, runOptions),
                runOptions.leaseMaxRuntimeMs);
        }
        if (!runOptions.dryRun && runOptions.controlSessionToken.empty()) {
            return ErrorExit("run requires an active control session; use `computer.cpp run --owner <owner> --purpose <purpose> <script.lua>` or pass --control-session", 6);
        }
        if (!runOptions.dryRun) {
            if (int ready = EnsureReadyDaemonForCliCommand(options, args, executablePath); ready != 0) {
                return ready;
            }
        }
        return ComputerCpp::RunLuaScript(runOptions);
    }

    if (ComputerCpp::Cli::IsSemanticAppCommand(args)) {
        return ComputerCpp::Cli::HandleSemanticAppCommand(options, args, executablePath);
    }

    if (int ready = EnsureReadyDaemonForCliCommand(options, args, executablePath); ready != 0) {
        return ready;
    }

    const bool isBatch = args[0] == "batch";
    auto request = ComputerCpp::Cli::BuildDaemonCommand(args, isBatch ? ReadAllStdin() : "");
    if (!request.ok()) {
        return ErrorExit(request.error, request.errorCode);
    }
    std::string method = std::move(request.method);
    json params = std::move(request.params);

    AddControlSessionParams(params, options);
    json response = ComputerCpp::SendDaemonRequest(options.session, MakeRequest(method, params));
    if (options.jsonOutput) {
        std::cout << response.dump(2) << "\n";
    } else {
        PrintCompact(method, response);
    }

    if (!response.value("ok", false)) {
        std::string code = response.value("code", "");
        if (code == "target_not_found") return 2;
        if (code == "wait_timeout") return 4;
        if (code.rfind("control_session", 0) == 0) return 6;
        if (code.find("permission") != std::string::npos) return 7;
        return 1;
    }
    if (method == "batch" && BatchResponseHasFailedSteps(response)) {
        return 1;
    }
    return 0;
}
