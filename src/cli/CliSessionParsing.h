#pragma once

#include "CliOptions.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ComputerCpp::Cli {

std::optional<int> ParseInt(std::string_view value);
std::optional<int64_t> ParseDurationOption(std::string_view value, bool millisecondsOnly);

bool SetDurationParam(
    nlohmann::json& params,
    std::string_view key,
    const std::vector<std::string>& args,
    size_t& i,
    bool millisecondsOnly
);

bool ParseSessionCommonOption(
    const std::vector<std::string>& args,
    size_t& i,
    nlohmann::json& params,
    std::string_view subcommand,
    std::string& error
);

struct SessionChildCommand {
    nlohmann::json acquireParams;
    std::vector<std::string> command;
    int64_t maxMs = 4 * 60 * 60 * 1000LL;
    bool releaseAfter = false;
    bool cleanupBeforeRelease = false;
};

struct SessionChildCommandParseResult {
    std::optional<SessionChildCommand> command;
    std::string error;
    int exitCode = 0;
};

SessionChildCommandParseResult ParseSessionChildCommand(
    const CliOptions& options,
    const std::vector<std::string>& args,
    const std::string& subcommand,
    bool allowPersistentOptions,
    int64_t defaultLeaseTtlMs
);

} // namespace ComputerCpp::Cli
