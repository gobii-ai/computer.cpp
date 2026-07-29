#pragma once

#include "computer_cpp/AppPaths.h"

#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace ComputerCpp::Cli {

inline bool IsAsciiAlphaNumeric(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
}

inline std::string PercentEncodeRecordingHeaderValue(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char ch : value) {
        if (IsAsciiAlphaNumeric(ch) ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[ch >> 4]);
            encoded.push_back(kHex[ch & 0x0f]);
        }
    }
    return encoded;
}

inline nlohmann::json RecordingMetadataForExternalCaller(
    const nlohmann::json& recording
) {
    if (!recording.is_object() || recording.empty()) {
        return recording;
    }
    nlohmann::json external = recording;
    if (!external.contains("path") || !external["path"].is_string()) {
        return external;
    }

    const std::filesystem::path path =
        external["path"].get<std::string>();
    const std::filesystem::path root = RecordingDir().lexically_normal();
    const std::filesystem::path relative = path.is_absolute()
        ? path.lexically_normal().lexically_relative(root)
        : path.lexically_normal();
    bool safe = !relative.empty() && !relative.is_absolute();
    for (const auto& part : relative) {
        if (part == "..") {
            safe = false;
            break;
        }
    }
    external["path"] = safe
        ? nlohmann::json(relative.generic_string())
        : nlohmann::json(nullptr);
    return external;
}

inline std::string RecordingErrorText(const nlohmann::json& recording) {
    return recording.is_object() &&
        recording.contains("error") &&
        recording["error"].is_string()
        ? recording["error"].get<std::string>()
        : "";
}

inline std::map<std::string, std::string> RecordingHttpHeaders(
    const nlohmann::json& recording
) {
    if (!recording.is_object() || recording.empty()) {
        return {};
    }
    const nlohmann::json external =
        RecordingMetadataForExternalCaller(recording);
    return {
        {"X-ComputerCpp-Recording-Id", PercentEncodeRecordingHeaderValue(
            external.value("recordingId", ""))},
        {"X-ComputerCpp-Recording-Status", PercentEncodeRecordingHeaderValue(
            external.value("status", ""))},
        {"X-ComputerCpp-Recording-Path", PercentEncodeRecordingHeaderValue(
            external.contains("path") && external["path"].is_string()
                ? external["path"].get<std::string>()
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
    result["_meta"]["org.computercpp/recording"] =
        RecordingMetadataForExternalCaller(recording);
}

inline nlohmann::json McpRecordingErrorData(const nlohmann::json& recording) {
    if (!recording.is_object() || recording.empty()) {
        return nullptr;
    }
    return {
        {"_meta", {
            {"org.computercpp/recording",
             RecordingMetadataForExternalCaller(recording)},
        }},
    };
}

} // namespace ComputerCpp::Cli
