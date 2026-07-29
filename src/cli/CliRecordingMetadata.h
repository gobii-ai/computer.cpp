#pragma once

#include <cctype>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace ComputerCpp::Cli {

inline std::string PercentEncodeRecordingHeaderValue(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[ch >> 4]);
            encoded.push_back(kHex[ch & 0x0f]);
        }
    }
    return encoded;
}

inline std::map<std::string, std::string> RecordingHttpHeaders(
    const nlohmann::json& recording
) {
    if (!recording.is_object() || recording.empty()) {
        return {};
    }
    return {
        {"X-ComputerCpp-Recording-Id", PercentEncodeRecordingHeaderValue(
            recording.value("recordingId", ""))},
        {"X-ComputerCpp-Recording-Status", PercentEncodeRecordingHeaderValue(
            recording.value("status", ""))},
        {"X-ComputerCpp-Recording-Path", PercentEncodeRecordingHeaderValue(
            recording.contains("path") && recording["path"].is_string()
                ? recording["path"].get<std::string>()
                : "")},
    };
}

inline void AttachMcpRecordingMetadata(
    nlohmann::json& result,
    const nlohmann::json& recording
) {
    if (!recording.is_object() || recording.empty()) {
        return;
    }
    result["_meta"]["org.computercpp/recording"] = recording;
}

inline nlohmann::json McpRecordingErrorData(const nlohmann::json& recording) {
    if (!recording.is_object() || recording.empty()) {
        return nullptr;
    }
    return {
        {"_meta", {
            {"org.computercpp/recording", recording},
        }},
    };
}

} // namespace ComputerCpp::Cli
