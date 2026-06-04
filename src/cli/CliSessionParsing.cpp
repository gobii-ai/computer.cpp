#include "CliSessionParsing.h"

#include "CliSession.h"

#include "computer_cpp/StringUtils.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace ComputerCpp::Cli {
namespace {

std::optional<int64_t> ParseInt64(std::string_view value) {
    int64_t parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<double> ParseDouble(std::string_view value) {
    double parsed = 0.0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

int64_t ScaleDurationMs(double amount, double multiplier) {
    if (!std::isfinite(amount) || amount < 0.0) {
        throw std::runtime_error("duration requires a non-negative finite amount");
    }
    const double max = static_cast<double>(std::numeric_limits<int64_t>::max());
    if (amount > max / multiplier) {
        throw std::runtime_error("duration is too large");
    }
    return static_cast<int64_t>(amount * multiplier);
}

} // namespace

std::optional<int> ParseInt(std::string_view value) {
    int parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int64_t> ParseDurationOption(std::string_view value, bool millisecondsOnly) {
    if (millisecondsOnly) {
        auto parsed = ParseInt64(value);
        if (!parsed || *parsed < 0) {
            return std::nullopt;
        }
        return parsed;
    }
    try {
        return ParseDurationMs(value);
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

bool SetDurationParam(
    nlohmann::json& params,
    std::string_view key,
    const std::vector<std::string>& args,
    size_t& i,
    bool millisecondsOnly
) {
    if (i + 1 >= args.size()) {
        return false;
    }
    if (IsBlank(args[i + 1])) {
        return false;
    }
    auto parsed = ParseDurationOption(args[i + 1], millisecondsOnly);
    if (!parsed) {
        return false;
    }
    params[std::string(key)] = *parsed;
    ++i;
    return true;
}

bool ParseSessionCommonOption(
    const std::vector<std::string>& args,
    size_t& i,
    nlohmann::json& params,
    std::string_view subcommand,
    std::string& error
) {
    const auto sessionOptionError = [&](std::string_view option, std::string_view message) {
        error = "session " + std::string(subcommand) + " " + std::string(option) + " " + std::string(message);
    };
    const auto hasValue = [&](std::string_view option) {
        if (i + 1 < args.size()) {
            return true;
        }
        sessionOptionError(option, "requires a value");
        return false;
    };

    if (args[i] == "--scope") {
        if (!hasValue(args[i])) {
            return false;
        }
        params["scope"] = args[++i];
        return true;
    }
    if (args[i] == "--owner") {
        if (!hasValue(args[i])) {
            return false;
        }
        params["owner"] = args[++i];
        return true;
    }
    if (args[i] == "--purpose") {
        if (!hasValue(args[i])) {
            return false;
        }
        params["purpose"] = args[++i];
        return true;
    }
    if (args[i] == "--ttl" || args[i] == "--ttl-ms") {
        if (!hasValue(args[i])) {
            return false;
        }
        if (!SetDurationParam(params, "ttlMs", args, i, args[i] == "--ttl-ms")) {
            sessionOptionError(args[i], "requires a valid duration");
            return false;
        }
        return true;
    }
    if (args[i] == "--wait" || args[i] == "--wait-ms") {
        if (!hasValue(args[i])) {
            return false;
        }
        if (!SetDurationParam(params, "waitMs", args, i, args[i] == "--wait-ms")) {
            sessionOptionError(args[i], "requires a valid duration");
            return false;
        }
        return true;
    }
    if (args[i] == "--max-runtime" || args[i] == "--max-runtime-ms") {
        if (!hasValue(args[i])) {
            return false;
        }
        if (!SetDurationParam(params, "maxRuntimeMs", args, i, args[i] == "--max-runtime-ms")) {
            sessionOptionError(args[i], "requires a valid duration");
            return false;
        }
        return true;
    }
    error.clear();
    return false;
}

SessionChildCommandParseResult ParseSessionChildCommand(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& subcommand,
    bool allowPersistentOptions,
    int64_t defaultLeaseTtlMs
) {
    SessionChildCommand parsed;
    parsed.acquireParams = {
        {"scope", options.controlScope},
        {"daemonSession", options.session},
        {"ttlMs", defaultLeaseTtlMs},
        {"waitMs", 0},
    };

    size_t commandStart = args.size();
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--") {
            commandStart = i + 1;
            break;
        }
        if (args[i] == "--max" || args[i] == "--max-ms") {
            if (i + 1 >= args.size()) {
                return {std::nullopt, "session " + subcommand + " " + args[i] + " requires a value", 2};
            }
            if (IsBlank(args[i + 1])) {
                return {std::nullopt, "session " + subcommand + " --max requires a valid duration", 2};
            }
            auto maxMs = ParseDurationOption(args[i + 1], args[i] == "--max-ms");
            if (!maxMs) {
                return {std::nullopt, "session " + subcommand + " --max requires a valid duration", 2};
            }
            parsed.maxMs = *maxMs;
            ++i;
            continue;
        }
        if (args[i] == "--no-max") {
            parsed.maxMs = 0;
            continue;
        }
        if (allowPersistentOptions && args[i] == "--release") {
            parsed.releaseAfter = true;
            continue;
        }
        if (allowPersistentOptions && args[i] == "--cleanup") {
            parsed.cleanupBeforeRelease = true;
            parsed.releaseAfter = true;
            continue;
        }
        std::string optionError;
        if (!ParseSessionCommonOption(args, i, parsed.acquireParams, subcommand, optionError)) {
            if (!optionError.empty()) {
                return {std::nullopt, optionError, 2};
            }
            return {std::nullopt, "unknown session " + subcommand + " option: " + args[i], 2};
        }
    }

    if (commandStart >= args.size()) {
        return {std::nullopt, "session " + subcommand + " requires -- <command>", 2};
    }
    parsed.command.assign(args.begin() + static_cast<long>(commandStart), args.end());
    if (parsed.command.empty()) {
        return {std::nullopt, "session " + subcommand + " requires a command", 2};
    }
    parsed.acquireParams["maxRuntimeMs"] = parsed.maxMs;
    return {std::move(parsed), {}, 0};
}

int64_t ParseDurationMs(std::string_view value) {
    if (value.empty()) {
        return 0;
    }

    size_t unitStart = 0;
    while (unitStart < value.size() &&
           (std::isdigit(static_cast<unsigned char>(value[unitStart])) || value[unitStart] == '.')) {
        ++unitStart;
    }
    if (unitStart == 0) {
        throw std::runtime_error("duration requires a numeric amount");
    }

    auto amount = ParseDouble(value.substr(0, unitStart));
    if (!amount) {
        throw std::runtime_error("duration requires a valid numeric amount");
    }
    std::string_view unit = value.substr(unitStart);
    if (unit.empty() || unit == "ms") {
        return ScaleDurationMs(*amount, 1.0);
    }
    if (unit == "s" || unit == "sec" || unit == "secs") {
        return ScaleDurationMs(*amount, 1000.0);
    }
    if (unit == "m" || unit == "min" || unit == "mins") {
        return ScaleDurationMs(*amount, 60.0 * 1000.0);
    }
    if (unit == "h" || unit == "hr" || unit == "hrs") {
        return ScaleDurationMs(*amount, 60.0 * 60.0 * 1000.0);
    }
    throw std::runtime_error("unknown duration unit: " + std::string(unit));
}

} // namespace ComputerCpp::Cli
