#pragma once

#include "CliCommands.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ComputerCpp::Cli {

inline CommandRequest Error(std::string message, int code = 2) {
    CommandRequest request;
    request.error = std::move(message);
    request.errorCode = code;
    return request;
}

inline CommandRequest Ok(std::string method, nlohmann::json params) {
    CommandRequest request;
    request.method = std::move(method);
    request.params = std::move(params);
    return request;
}

inline bool IsOneOf(std::string_view value, std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

inline bool HasFlag(const std::vector<std::string>& args, std::string_view flag) {
    return std::any_of(args.begin(), args.end(), [&](const std::string& arg) {
        return arg == flag;
    });
}

inline bool TakeOptionValue(
    const std::vector<std::string>& args,
    size_t& index,
    std::initializer_list<std::string_view> names,
    std::string& value
) {
    if (!IsOneOf(args[index], names) || index + 1 >= args.size()) {
        return false;
    }
    value = args[++index];
    return true;
}

inline std::optional<int> ParseInt(std::string_view value) {
    int parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

inline std::optional<double> ParseDouble(std::string_view value) {
    double parsed = 0.0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size() || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

inline bool SetParsedInt(nlohmann::json& params, std::string_view key, std::string_view value) {
    auto parsed = ParseInt(value);
    if (!parsed) {
        return false;
    }
    params[std::string(key)] = *parsed;
    return true;
}

inline bool SetParsedDouble(nlohmann::json& params, std::string_view key, std::string_view value) {
    auto parsed = ParseDouble(value);
    if (!parsed) {
        return false;
    }
    params[std::string(key)] = *parsed;
    return true;
}

inline bool SetStringOption(
    nlohmann::json& params,
    std::string_view key,
    const std::vector<std::string>& args,
    size_t& index,
    std::initializer_list<std::string_view> names
) {
    std::string value;
    if (!TakeOptionValue(args, index, names, value)) {
        return false;
    }
    params[std::string(key)] = std::move(value);
    return true;
}

inline bool SetIntOption(
    nlohmann::json& params,
    std::string_view key,
    const std::vector<std::string>& args,
    size_t& index,
    std::initializer_list<std::string_view> names
) {
    if (!IsOneOf(args[index], names) || index + 1 >= args.size()) {
        return false;
    }
    auto parsed = ParseInt(args[index + 1]);
    if (!parsed) {
        return false;
    }
    ++index;
    params[std::string(key)] = *parsed;
    return true;
}

inline bool SetDoubleOption(
    nlohmann::json& params,
    std::string_view key,
    const std::vector<std::string>& args,
    size_t& index,
    std::initializer_list<std::string_view> names
) {
    if (!IsOneOf(args[index], names) || index + 1 >= args.size()) {
        return false;
    }
    auto parsed = ParseDouble(args[index + 1]);
    if (!parsed) {
        return false;
    }
    ++index;
    params[std::string(key)] = *parsed;
    return true;
}

} // namespace ComputerCpp::Cli
