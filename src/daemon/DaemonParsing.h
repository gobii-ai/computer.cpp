#pragma once

#include "DaemonTargetGeometry.h"

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace ComputerCpp {

inline bool IsAllowedParamName(std::string_view key, std::initializer_list<std::string_view> allowed) {
    for (std::string_view name : allowed) {
        if (key == name) {
            return true;
        }
    }
    return false;
}

inline std::optional<std::string> UnknownParam(
    const nlohmann::json& params,
    std::initializer_list<std::string_view> allowed
) {
    for (const auto& item : params.items()) {
        if (!IsAllowedParamName(item.key(), allowed)) {
            return item.key();
        }
    }
    return std::nullopt;
}

template <typename Integer>
std::optional<Integer> ParseIntegerStrict(std::string_view value) {
    Integer parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

inline std::optional<double> NumberParam(const nlohmann::json& params, const char* key, double fallback) {
    if (!params.contains(key)) {
        return fallback;
    }
    return NumberFromJson(params.at(key));
}

inline bool ReadOptionalNumber(
    const nlohmann::json& params,
    std::initializer_list<const char*> keys,
    std::optional<double>& out
) {
    for (const char* key : keys) {
        if (!params.contains(key)) {
            continue;
        }
        auto parsed = NumberFromJson(params.at(key));
        if (!parsed) {
            return false;
        }
        out = *parsed;
        return true;
    }
    out = std::nullopt;
    return true;
}

inline std::optional<double> NumberParam(
    const nlohmann::json& params,
    std::initializer_list<const char*> keys,
    double fallback
) {
    std::optional<double> value;
    if (!ReadOptionalNumber(params, keys, value)) {
        return std::nullopt;
    }
    return value.value_or(fallback);
}

inline std::optional<double> RequiredNumberParam(
    const nlohmann::json& params,
    std::initializer_list<const char*> keys
) {
    std::optional<double> value;
    if (!ReadOptionalNumber(params, keys, value)) {
        return std::nullopt;
    }
    return value;
}

inline std::optional<int> IntParam(const nlohmann::json& params, const char* key, int fallback) {
    auto value = NumberParam(params, key, static_cast<double>(fallback));
    if (!value ||
        *value < static_cast<double>(std::numeric_limits<int>::min()) ||
        *value > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::trunc(*value) != *value) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

inline std::optional<int> IntParam(
    const nlohmann::json& params,
    std::initializer_list<const char*> keys,
    int fallback
) {
    auto value = NumberParam(params, keys, static_cast<double>(fallback));
    if (!value ||
        *value < static_cast<double>(std::numeric_limits<int>::min()) ||
        *value > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::trunc(*value) != *value) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

inline std::optional<int64_t> Int64Param(const nlohmann::json& params, const char* key, int64_t fallback) {
    auto value = NumberParam(params, key, static_cast<double>(fallback));
    if (!value ||
        *value < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        *value > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
        std::trunc(*value) != *value) {
        return std::nullopt;
    }
    return static_cast<int64_t>(*value);
}

inline std::optional<bool> BoolParam(const nlohmann::json& params, const char* key, bool fallback) {
    if (!params.contains(key)) {
        return fallback;
    }
    if (!params.at(key).is_boolean()) {
        return std::nullopt;
    }
    return params.at(key).get<bool>();
}

inline std::optional<std::string> StringParam(const nlohmann::json& params, const char* key, std::string fallback = "") {
    if (!params.contains(key)) {
        return fallback;
    }
    if (!params.at(key).is_string()) {
        return std::nullopt;
    }
    return params.at(key).get<std::string>();
}

inline std::optional<int> PositiveIntParam(
    const nlohmann::json& params,
    std::initializer_list<const char*> keys
) {
    auto value = IntParam(params, keys, 0);
    if (!value || *value <= 0) {
        return std::nullopt;
    }
    return value;
}

} // namespace ComputerCpp
