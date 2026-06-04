#include "CliSession.h"

#include "CliOutput.h"
#include "CliSessionCleanup.h"
#include "CliSessionLease.h"
#include "CliSessionParsing.h"
#include "CliSessionProcess.h"

#include "computer_cpp/ControlSession.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace ComputerCpp::Cli {
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

int RunPersistentCommandWithControlSession(
    const CliOptions& options,
    const json& acquireParams,
    const std::vector<std::string>& command,
    int64_t maxMs,
    bool releaseAfter,
    bool cleanupBeforeRelease) {
    auto localAcquire = AcquireOptionsFromJson(options, acquireParams);
    auto acquireResult = AcquireOrResumeControlSession(localAcquire);
    if (!acquireResult.ok) {
        json acquire = ControlSessionCliError(acquireResult);
        if (options.jsonOutput) {
            std::cout << acquire.dump(2) << "\n";
        } else {
            PrintCompact("control_session_resume", acquire);
        }
        return 1;
    }

    std::string token = acquireResult.record.token;
    bool freshLease = acquireResult.record.acquiredAtMs == acquireResult.record.renewedAtMs;
    ScopedControlSessionLease lease(token, releaseAfter);
    std::string scope = acquireResult.record.scope.empty() ? options.controlScope : acquireResult.record.scope;
    int64_t ttlMs = acquireResult.record.expiresAtMs - acquireResult.record.renewedAtMs;
    if (ttlMs <= 0) {
        ttlMs = ClampControlSessionTtlMs(localAcquire.ttlMs);
    }
    if (!CleanupExpiredControlSessionResources(options, token, scope)) {
        if (freshLease) {
            lease.SetReleaseOnDestroy(true);
        }
        return 5;
    }

    int childCode = RunChildWithControlSession(command, token, scope, ttlMs, maxMs);

    bool cleanupOk = true;
    if (cleanupBeforeRelease) {
        cleanupOk = CleanupControlSessionResources(options, token, scope);
    }
    if (releaseAfter) {
        if (!lease.ReleaseNow() && childCode == 0) {
            childCode = 5;
        }
    }
    if (!cleanupOk && childCode == 0) {
        return 5;
    }
    return childCode;
}

} // namespace

int RunManagedCommandWithControlSession(
    const CliOptions& options,
    const json& acquireParams,
    const std::vector<std::string>& command,
    int64_t maxMs) {
    auto localAcquire = AcquireOptionsFromJson(options, acquireParams);
    auto acquireResult = AcquireControlSession(localAcquire);
    if (!acquireResult.ok) {
        json acquire = ControlSessionCliError(acquireResult);
        if (options.jsonOutput) {
            std::cout << acquire.dump(2) << "\n";
        } else {
            PrintCompact("control_session_acquire", acquire);
        }
        return 1;
    }

    std::string token = acquireResult.record.token;
    ScopedControlSessionLease lease(token);
    std::string scope = acquireResult.record.scope.empty() ? options.controlScope : acquireResult.record.scope;
    int64_t ttlMs = acquireResult.record.expiresAtMs - acquireResult.record.renewedAtMs;
    if (ttlMs <= 0) {
        ttlMs = ClampControlSessionTtlMs(localAcquire.ttlMs);
    }
    if (!CleanupExpiredControlSessionResources(options, token, scope)) {
        lease.ReleaseNow();
        return 5;
    }

    int childCode = RunChildWithControlSession(command, token, scope, ttlMs, maxMs);
    bool cleanupOk = CleanupControlSessionResources(options, token, scope);
    lease.ReleaseNow();
    if (!cleanupOk && childCode == 0) {
        return 5;
    }
    return childCode;
}

int HandleSessionRun(const CliOptions& options, const std::vector<std::string>& args) {
    auto parsed = ParseSessionChildCommand(options, args, "run", false, kDefaultLeaseTtlMs);
    if (!parsed.command) {
        return ErrorExit(parsed.error, parsed.exitCode);
    }
    const auto& command = *parsed.command;
    return RunManagedCommandWithControlSession(options, command.acquireParams, command.command, command.maxMs);
}

int HandleSessionExec(const CliOptions& options, const std::vector<std::string>& args) {
    auto parsed = ParseSessionChildCommand(options, args, "exec", true, kDefaultLeaseTtlMs);
    if (!parsed.command) {
        return ErrorExit(parsed.error, parsed.exitCode);
    }
    const auto& command = *parsed.command;
    return RunPersistentCommandWithControlSession(
        options,
        command.acquireParams,
        command.command,
        command.maxMs,
        command.releaseAfter,
        command.cleanupBeforeRelease
    );
}

} // namespace ComputerCpp::Cli
