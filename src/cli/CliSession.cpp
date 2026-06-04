#include "CliSession.h"

#include "CliOutput.h"
#include "CliSessionInspect.h"
#include "CliSessionParsing.h"

#include "computer_cpp/ControlSession.h"
#include "computer_cpp/StringUtils.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace ComputerCpp::Cli {

int HandleSessionRun(const CliOptions& options, const std::vector<std::string>& args);
int HandleSessionExec(const CliOptions& options, const std::vector<std::string>& args);

namespace {

constexpr int64_t kDefaultLeaseTtlMs = 10 * 60 * 1000;

int ErrorExit(const std::string& message, int code = 1) {
    std::cerr << "Error: " << message << std::endl;
    return code;
}

std::optional<int64_t> Int64Param(const json& params, const char* key, int64_t fallback) {
    if (!params.contains(key)) {
        return fallback;
    }
    const auto& value = params.at(key);
    if (!value.is_number()) {
        return std::nullopt;
    }
    double number = value.get<double>();
    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
        std::trunc(number) != number) {
        return std::nullopt;
    }
    return static_cast<int64_t>(number);
}

ControlSessionAcquireOptions AcquireOptionsFromJson(const CliOptions& options, const json& acquireParams) {
    ControlSessionAcquireOptions localAcquire;
    localAcquire.scope = acquireParams.value("scope", options.controlScope);
    localAcquire.daemonSession = acquireParams.value("daemonSession", options.session);
    localAcquire.owner = acquireParams.value("owner", std::string());
    localAcquire.purpose = acquireParams.value("purpose", std::string());
    localAcquire.ttlMs = Int64Param(acquireParams, "ttlMs", kDefaultLeaseTtlMs).value_or(kDefaultLeaseTtlMs);
    localAcquire.waitMs = Int64Param(acquireParams, "waitMs", static_cast<int64_t>(0)).value_or(0);
    localAcquire.maxRuntimeMs = Int64Param(acquireParams, "maxRuntimeMs", static_cast<int64_t>(0)).value_or(0);
    return localAcquire;
}

using SessionHandler = int (*)(const CliOptions&, const std::vector<std::string>&);

struct SessionRoute {
    std::string_view subcommand;
    SessionHandler run;
};

int HandleSessionAcquireOrResume(const CliOptions& options, const std::vector<std::string>& args, const std::string& sub) {
    json params = {
        {"scope", options.controlScope},
        {"daemonSession", options.session},
        {"ttlMs", kDefaultLeaseTtlMs},
        {"waitMs", 0},
    };
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--max" || args[i] == "--max-ms") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session " + sub + " " + args[i] + " requires a value", 2);
            }
            if (!SetDurationParam(params, "maxRuntimeMs", args, i, args[i] == "--max-ms")) {
                return ErrorExit("session " + sub + " --max requires a valid duration", 2);
            }
            continue;
        }
        std::string optionError;
        if (!ParseSessionCommonOption(args, i, params, sub, optionError)) {
            if (!optionError.empty()) {
                return ErrorExit(optionError, 2);
            }
            return ErrorExit("unknown session " + sub + " option: " + args[i], 2);
        }
    }

    auto localAcquire = AcquireOptionsFromJson(options, params);
    if (sub == "resume") {
        return PrintSessionCliResult(options, "control_session_resume", AcquireOrResumeControlSession(localAcquire), true);
    }
    return PrintSessionCliResult(options, "control_session_acquire", AcquireControlSession(localAcquire), true);
}

int HandleSessionAcquire(const CliOptions& options, const std::vector<std::string>& args) {
    return HandleSessionAcquireOrResume(options, args, "acquire");
}

int HandleSessionResume(const CliOptions& options, const std::vector<std::string>& args) {
    return HandleSessionAcquireOrResume(options, args, "resume");
}

int HandleSessionRenew(const CliOptions& options, const std::vector<std::string>& args) {
    json params = {
        {"scope", options.controlScope},
        {"daemonSession", options.session},
        {"token", args.size() > 2 && args[2].rfind("--", 0) != 0 ? args[2] : options.controlSessionToken},
        {"ttlMs", kDefaultLeaseTtlMs},
    };
    for (size_t i = params["token"].get<std::string>().empty() ? 2 : 3; i < args.size(); ++i) {
        std::string optionError;
        if (!ParseSessionCommonOption(args, i, params, "renew", optionError)) {
            if (!optionError.empty()) {
                return ErrorExit(optionError, 2);
            }
            return ErrorExit("unknown session renew option: " + args[i], 2);
        }
    }
    if (IsBlank(params.value("token", std::string()))) {
        return ErrorExit("session renew token must be non-empty", 2);
    }
    auto ttlMs = Int64Param(params, "ttlMs", kDefaultLeaseTtlMs);
    if (!ttlMs) {
        return ErrorExit("session renew --ttl requires an integer duration", 2);
    }
    return PrintSessionCliResult(
        options,
        "control_session_renew",
        RenewControlSession(params.value("token", std::string()), *ttlMs),
        true);
}

int HandleSessionRelease(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() > 3) {
        return ErrorExit("session release accepts at most one token", 2);
    }
    if (args.size() > 2 && args[2].rfind("--", 0) == 0) {
        return ErrorExit("unknown session release option: " + args[2], 2);
    }
    const std::string token = args.size() > 2 ? args[2] : options.controlSessionToken;
    if (IsBlank(token)) {
        return ErrorExit("session release token must be non-empty", 2);
    }
    return PrintSessionCliResult(
        options,
        "control_session_release",
        ReleaseControlSession(token),
        true);
}

int HandleSessionReleaseActive(const CliOptions& options, const std::vector<std::string>& args) {
    json params = {
        {"scope", options.controlScope},
        {"owner", ""},
        {"purpose", ""},
        {"reason", ""},
    };
    const auto requireValue = [&](size_t i, const std::string& option) -> std::optional<std::string> {
        if (i + 1 >= args.size()) {
            return "session release-active " + option + " requires a value";
        }
        if (IsBlank(args[i + 1])) {
            return "session release-active " + option + " requires a non-empty value";
        }
        return std::nullopt;
    };
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--scope") {
            if (auto error = requireValue(i, "--scope")) {
                return ErrorExit(*error, 2);
            }
            params["scope"] = args[++i];
        } else if (args[i] == "--owner") {
            if (auto error = requireValue(i, "--owner")) {
                return ErrorExit(*error, 2);
            }
            params["owner"] = args[++i];
        } else if (args[i] == "--purpose") {
            if (auto error = requireValue(i, "--purpose")) {
                return ErrorExit(*error, 2);
            }
            params["purpose"] = args[++i];
        } else if (args[i] == "--reason") {
            if (auto error = requireValue(i, "--reason")) {
                return ErrorExit(*error, 2);
            }
            params["reason"] = args[++i];
        } else {
            return ErrorExit("unknown session release-active option: " + args[i], 2);
        }
    }
    return PrintSessionCliResult(
        options,
        "control_session_release_active",
        ReleaseActiveControlSession(
            params.value("scope", options.controlScope),
            params.value("owner", std::string()),
            params.value("purpose", std::string()),
            params.value("reason", std::string())),
        false);
}

int HandleSessionStatus(const CliOptions& options, const std::vector<std::string>& args) {
    json params = {
        {"scope", options.controlScope},
        {"daemonSession", options.session},
    };
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--scope") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session status --scope requires a value", 2);
            }
            if (IsBlank(args[i + 1])) {
                return ErrorExit("session status --scope requires a non-empty value", 2);
            }
            params["scope"] = args[++i];
        } else if (args[i] == "--token") {
            if (i + 1 >= args.size()) {
                return ErrorExit("session status --token requires a value", 2);
            }
            params["token"] = args[++i];
        } else {
            return ErrorExit("unknown session status option: " + args[i], 2);
        }
    }
    if (params.contains("token") && IsBlank(params.value("token", std::string()))) {
        return ErrorExit("session status token must be non-empty when provided", 2);
    }
    return PrintSessionCliResult(
        options,
        "control_session_status",
        GetControlSessionStatus(params.value("scope", options.controlScope), params.value("token", std::string())),
        false);
}

constexpr auto kSessionRoutes = std::to_array<SessionRoute>({
    {"run", HandleSessionRun},
    {"exec", HandleSessionExec},
    {"acquire", HandleSessionAcquire},
    {"resume", HandleSessionResume},
    {"renew", HandleSessionRenew},
    {"release", HandleSessionRelease},
    {"release-active", HandleSessionReleaseActive},
    {"status", HandleSessionStatus},
    {"metrics", HandleSessionMetrics},
    {"events", HandleSessionEvents},
});

const SessionRoute* FindSessionRoute(std::string_view subcommand) {
    for (const auto& route : kSessionRoutes) {
        if (route.subcommand == subcommand) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace

int HandleSessionCommand(const CliOptions& options, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return ErrorExit("session requires acquire, resume, renew, release, status, events, metrics, run, or exec", 2);
    }
    const std::string& sub = args[1];
    if (const auto* route = FindSessionRoute(sub)) {
        return route->run(options, args);
    }
    return ErrorExit("unknown session subcommand: " + sub, 2);
}

} // namespace ComputerCpp::Cli
