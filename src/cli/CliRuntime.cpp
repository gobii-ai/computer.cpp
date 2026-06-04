#include "CliRuntime.h"

#include "computer_cpp/RuntimeProvenance.h"
#include "computer_cpp/Transport.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
namespace {

json MakeRequest(const std::string& method, json params = json::object()) {
    return {
        {"id", method},
        {"method", method},
        {"params", std::move(params)},
    };
}

json RuntimeMismatchResponse(const std::string& executablePath, const json& capabilities) {
    json daemon = capabilities.value("data", json::object()).value("runtime", json::object());
    json client = RuntimeProvenanceToJson(RuntimeProvenanceForExecutable(executablePath));
    return {
        {"ok", false},
        {"code", "daemon_client_mismatch"},
        {"error", "computer.cpp daemon binary does not match this CLI; restart the daemon or install the matching binary"},
        {"data", {
            {"client", client},
            {"daemon", daemon},
            {"overrideEnv", "COMPUTER_CPP_ALLOW_DAEMON_MISMATCH=1"},
        }},
    };
}

bool IsExpectedMacTrayDaemonForCli(const std::string& executablePath, const json& daemon) {
#if defined(__APPLE__)
    std::filesystem::path clientPath(executablePath);
    std::filesystem::path daemonPath(daemon.value("executablePath", ""));
    if (clientPath.filename() != "computer.cpp" || daemonPath.filename() != "ComputerCpp") {
        return false;
    }
    if (daemonPath.string().find("ComputerCpp.app/Contents/MacOS/ComputerCpp") == std::string::npos) {
        return false;
    }

    json client = RuntimeProvenanceToJson(RuntimeProvenanceForExecutable(executablePath));
    return daemon.value("buildGitSha", "") == client.value("buildGitSha", "") &&
           daemon.value("featureMapSchemaVersion", "") == client.value("featureMapSchemaVersion", "");
#else
    (void)executablePath;
    (void)daemon;
    return false;
#endif
}

} // namespace

std::string ExecutablePath(char* argv0) {
    std::filesystem::path path(argv0);
    std::error_code ec;
    if (path.is_relative() && path.parent_path().empty()) {
        if (const char* rawPath = std::getenv("PATH")) {
            std::stringstream stream(rawPath);
            std::string dir;
            while (std::getline(stream, dir, ':')) {
                if (dir.empty()) {
                    dir = ".";
                }
                std::filesystem::path candidate = std::filesystem::path(dir) / path;
                if (std::filesystem::exists(candidate, ec) && !ec) {
                    return std::filesystem::absolute(candidate, ec).string();
                }
                ec.clear();
            }
        }
    }
    if (path.is_relative()) {
        path = std::filesystem::absolute(path, ec);
    }
    return path.string();
}

bool RuntimeCheckExempt(const std::vector<std::string>& args) {
    if (args.empty()) {
        return true;
    }
    const std::string& cmd = args[0];
    if (cmd == "daemon" || cmd == "close" || cmd == "ping" || cmd == "capabilities" || cmd == "schema") {
        return true;
    }
    if (cmd == "session" && args.size() > 1) {
        const std::string& sub = args[1];
        return sub == "status" || sub == "metrics" || sub == "events" || sub == "release" ||
               sub == "release-active";
    }
    return false;
}

int VerifyDaemonRuntimeOrExit(const CliOptions& options, const std::string& executablePath) {
    if (EnvFlag("COMPUTER_CPP_ALLOW_DAEMON_MISMATCH")) {
        return 0;
    }
    json capabilities = SendDaemonRequest(options.session, MakeRequest("capabilities"));
    if (!capabilities.value("ok", false)) {
        if (options.jsonOutput) {
            std::cout << capabilities.dump(2) << "\n";
        } else {
            std::cerr << "Error: could not read daemon capabilities: "
                      << capabilities.value("error", "unknown error") << "\n";
        }
        return 1;
    }
    json daemon = capabilities.value("data", json::object()).value("runtime", json::object());
    std::string daemonFingerprint = daemon.value("executableFingerprint", "");
    std::string clientFingerprint = ExecutableFingerprint(executablePath);
    if (daemonFingerprint.empty() || daemonFingerprint == "unavailable" || clientFingerprint.empty() ||
        clientFingerprint == "unavailable" || daemonFingerprint != clientFingerprint) {
        if (IsExpectedMacTrayDaemonForCli(executablePath, daemon)) {
            return 0;
        }
        json response = RuntimeMismatchResponse(executablePath, capabilities);
        if (options.jsonOutput) {
            std::cout << response.dump(2) << "\n";
        } else {
            auto data = response["data"];
            std::cerr << "Error: " << response.value("error", "daemon/client mismatch") << "\n";
            std::cerr << "  client: " << data["client"].value("executablePath", "") << " "
                      << data["client"].value("executableFingerprint", "") << "\n";
            std::cerr << "  daemon: " << data["daemon"].value("executablePath", "") << " "
                      << data["daemon"].value("executableFingerprint", "") << "\n";
            std::cerr << "  override for debugging only: COMPUTER_CPP_ALLOW_DAEMON_MISMATCH=1\n";
        }
        return 9;
    }
    return 0;
}

} // namespace ComputerCpp::Cli
